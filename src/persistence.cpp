#include "persistence.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace bytecachedb {

namespace {

bool write_all(int fd, const std::string& data, std::string& error) {
    size_t written = 0;
    while (written < data.size()) {
        const ssize_t result = ::write(fd, data.data() + written, data.size() - written);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            error = std::string("write failed: ") + std::strerror(errno);
            return false;
        }
        written += static_cast<size_t>(result);
    }
    return true;
}

std::string quote(const std::string& value) {
    std::string output;
    output.reserve(value.size() + 2);
    output.push_back('"');
    for (const char c : value) {
        switch (c) {
            case '\\':
                output += "\\\\";
                break;
            case '"':
                output += "\\\"";
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                output += "\\r";
                break;
            case '\t':
                output += "\\t";
                break;
            default:
                output.push_back(c);
                break;
        }
    }
    output.push_back('"');
    return output;
}

bool parse_sequence_prefix(const std::string& line,
                           uint64_t& sequence,
                           std::string& command_line) {
    const auto tab = line.find('\t');
    if (tab == std::string::npos || tab == 0) {
        return false;
    }
    const auto [ptr, ec] =
        std::from_chars(line.data(), line.data() + tab, sequence);
    if (ec != std::errc() || ptr != line.data() + tab) {
        return false;
    }
    command_line = line.substr(tab + 1);
    return true;
}

} // namespace

AppendOnlyFile::AppendOnlyFile(bool enabled,
                               std::string path,
                               FsyncPolicy fsync_policy,
                               std::string snapshot_path)
    : enabled_(enabled),
      path_(std::move(path)),
      fsync_policy_(fsync_policy),
      snapshot_path_(std::move(snapshot_path)) {}

AppendOnlyFile::~AppendOnlyFile() {
    close();
}

bool AppendOnlyFile::enabled() const {
    return enabled_;
}

const std::string& AppendOnlyFile::path() const {
    return path_;
}

FsyncPolicy AppendOnlyFile::fsync_policy() const {
    return fsync_policy_;
}

std::string AppendOnlyFile::fsync_policy_name() const {
    switch (fsync_policy_) {
        case FsyncPolicy::Always:
            return "always";
        case FsyncPolicy::EverySec:
            return "everysec";
        case FsyncPolicy::Never:
            return "never";
    }
    return "unknown";
}

bool AppendOnlyFile::open(std::string& error) {
    if (!enabled_) {
        return true;
    }

    std::lock_guard lock(mutex_);
    if (fd_ >= 0) {
        return true;
    }
    try {
        const std::filesystem::path path(path_);
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
    } catch (const std::exception& ex) {
        error = std::string("failed to prepare AOF path: ") + ex.what();
        return false;
    }

    fd_ = ::open(path_.c_str(), O_CREAT | O_APPEND | O_WRONLY, 0644);
    if (fd_ < 0) {
        error = "failed to open AOF file: " + std::string(std::strerror(errno));
        return false;
    }
    stopping_ = false;
    dirty_ = false;
    if (fsync_policy_ == FsyncPolicy::EverySec) {
        sync_thread_ = std::thread([this] { sync_loop(); });
    }
    return true;
}

bool AppendOnlyFile::append(const Command& command, std::string& error) {
    return append_line(serialize(command), error);
}

bool AppendOnlyFile::append_line(const std::string& line, std::string& error) {
    if (!enabled_) {
        return true;
    }

    std::lock_guard lock(mutex_);
    if (fd_ < 0) {
        error = "AOF file is not open";
        return false;
    }
    const uint64_t sequence = sequence_.fetch_add(1, std::memory_order_relaxed) + 1;
    return write_record_locked(std::to_string(sequence) + "\t" + line + "\n", error);
}

bool AppendOnlyFile::recover(StorageEngine& storage, std::string& error) {
    storage.set_eviction_enabled(false);
    uint64_t snapshot_sequence = 0;
    if (!load_snapshot(storage, snapshot_sequence, error)) {
        storage.set_eviction_enabled(true);
        storage.take_evicted_keys();
        return false;
    }
    sequence_.store(snapshot_sequence, std::memory_order_relaxed);
    const bool recovered = replay_after(storage, snapshot_sequence, error);
    storage.set_eviction_enabled(true);
    storage.take_evicted_keys();
    return recovered;
}

bool AppendOnlyFile::replay(StorageEngine& storage, std::string& error) const {
    storage.set_eviction_enabled(false);
    const bool replayed = replay_after(storage, 0, error);
    storage.set_eviction_enabled(true);
    storage.take_evicted_keys();
    return replayed;
}

bool AppendOnlyFile::replay_after(StorageEngine& storage,
                                  uint64_t minimum_sequence,
                                  std::string& error) const {
    if (!enabled_) {
        return true;
    }

    std::ifstream input(path_);
    if (!input.good()) {
        return true;
    }

    CommandParser parser;
    std::string line;
    size_t line_number = 0;
    uint64_t maximum_sequence = minimum_sequence;
    while (std::getline(input, line)) {
        ++line_number;
        uint64_t record_sequence = static_cast<uint64_t>(line_number);
        std::string command_line = line;
        parse_sequence_prefix(line, record_sequence, command_line);
        maximum_sequence = std::max(maximum_sequence, record_sequence);
        if (record_sequence <= minimum_sequence) {
            continue;
        }

        const auto parsed = parser.parse(command_line);
        if (!parsed.ok) {
            error = "invalid AOF line " + std::to_string(line_number) + ": " + parsed.error;
            return false;
        }
        if (!apply_replay_command(storage, parsed.command, error)) {
            error = "failed to replay AOF line " + std::to_string(line_number) + ": " + error;
            return false;
        }
    }
    sequence_.store(maximum_sequence, std::memory_order_relaxed);
    return true;
}

bool AppendOnlyFile::save_snapshot(const StorageEngine& storage, std::string& error) {
    if (snapshot_path_.empty()) {
        error = "snapshot path is not configured";
        return false;
    }

    const auto entries = storage.snapshot_entries();
    const uint64_t snapshot_sequence = sequence_.load(std::memory_order_relaxed);
    const std::filesystem::path path(snapshot_path_);
    const std::filesystem::path temporary = path.string() + ".tmp";
    try {
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
    } catch (const std::exception& ex) {
        error = std::string("failed to prepare snapshot path: ") + ex.what();
        return false;
    }

    const int fd = ::open(temporary.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) {
        error = "failed to open snapshot: " + std::string(std::strerror(errno));
        return false;
    }

    bool ok = write_all(fd,
                        "BYTECACHEDB_SNAPSHOT 1 " + std::to_string(snapshot_sequence) + "\n",
                        error);
    for (const auto& entry : entries) {
        if (!ok) {
            break;
        }
        Command set_command{CommandType::SET, {entry.key, entry.value}, {}};
        ok = write_all(fd, serialize(set_command) + "\n", error);
        if (ok && entry.expires_at.has_value()) {
            Command expire_command{
                CommandType::EXPIREAT,
                {entry.key, std::to_string(to_epoch_millis(*entry.expires_at))},
                {}};
            ok = write_all(fd, serialize(expire_command) + "\n", error);
        }
    }
    if (ok && ::fsync(fd) < 0) {
        error = "failed to sync snapshot: " + std::string(std::strerror(errno));
        ok = false;
    }
    if (::close(fd) < 0 && ok) {
        error = "failed to close snapshot: " + std::string(std::strerror(errno));
        ok = false;
    }
    if (!ok) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    try {
        std::filesystem::rename(temporary, path);
    } catch (const std::exception& ex) {
        error = std::string("failed to publish snapshot: ") + ex.what();
        return false;
    }
    return true;
}

bool AppendOnlyFile::load_snapshot(StorageEngine& storage,
                                   uint64_t& sequence,
                                   std::string& error) const {
    sequence = 0;
    if (snapshot_path_.empty()) {
        return true;
    }
    std::ifstream input(snapshot_path_);
    if (!input.good()) {
        return true;
    }

    std::string header;
    if (!std::getline(input, header)) {
        error = "snapshot is empty";
        return false;
    }
    std::istringstream header_stream(header);
    std::string magic;
    int version = 0;
    if (!(header_stream >> magic >> version >> sequence) ||
        magic != "BYTECACHEDB_SNAPSHOT" || version != 1) {
        error = "invalid snapshot header";
        return false;
    }

    CommandParser parser;
    std::string line;
    size_t line_number = 1;
    while (std::getline(input, line)) {
        ++line_number;
        const auto parsed = parser.parse(line);
        if (!parsed.ok ||
            (parsed.command.type != CommandType::SET &&
             parsed.command.type != CommandType::EXPIREAT)) {
            error = "invalid snapshot line " + std::to_string(line_number);
            return false;
        }
        if (!apply_replay_command(storage, parsed.command, error)) {
            error = "failed to load snapshot line " + std::to_string(line_number) + ": " + error;
            return false;
        }
    }
    return true;
}

void AppendOnlyFile::close() {
    {
        std::lock_guard lock(mutex_);
        if (fd_ < 0) {
            return;
        }
        stopping_ = true;
        sync_condition_.notify_all();
    }
    if (sync_thread_.joinable()) {
        sync_thread_.join();
    }

    std::lock_guard lock(mutex_);
    if (fd_ >= 0) {
        if (dirty_ && fsync_policy_ != FsyncPolicy::Never) {
            ::fsync(fd_);
        }
        ::close(fd_);
        fd_ = -1;
        dirty_ = false;
    }
}

std::string AppendOnlyFile::serialize(const Command& command) {
    std::ostringstream out;
    out << CommandParser::command_name(command.type);
    for (const auto& arg : command.args) {
        out << ' ' << quote(arg);
    }
    return out.str();
}

int64_t AppendOnlyFile::to_epoch_millis(StorageEngine::TimePoint time_point) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               time_point.time_since_epoch())
        .count();
}

StorageEngine::TimePoint AppendOnlyFile::from_epoch_millis(int64_t millis) {
    return StorageEngine::TimePoint(std::chrono::milliseconds(millis));
}

bool AppendOnlyFile::write_record_locked(const std::string& line, std::string& error) {
    if (!write_all(fd_, line, error)) {
        return false;
    }
    dirty_ = true;
    if (fsync_policy_ == FsyncPolicy::Always) {
        return sync_locked(error);
    }
    return true;
}

bool AppendOnlyFile::sync_locked(std::string& error) {
    if (!dirty_) {
        return true;
    }
    if (::fsync(fd_) < 0) {
        error = "fsync failed: " + std::string(std::strerror(errno));
        return false;
    }
    dirty_ = false;
    return true;
}

void AppendOnlyFile::sync_loop() {
    std::unique_lock lock(mutex_);
    while (!stopping_) {
        sync_condition_.wait_for(lock, std::chrono::seconds(1));
        if (dirty_) {
            std::string ignored;
            sync_locked(ignored);
        }
    }
}

bool AppendOnlyFile::apply_replay_command(StorageEngine& storage,
                                          const Command& command,
                                          std::string& error) {
    try {
        switch (command.type) {
            case CommandType::SET:
                storage.set(command.args[0], command.args[1]);
                return true;
            case CommandType::MSET:
                storage.mset(command.args);
                return true;
            case CommandType::DEL:
                storage.del(command.args[0]);
                return true;
            case CommandType::FLUSH:
                storage.flush();
                return true;
            case CommandType::PERSIST:
                storage.persist(command.args[0]);
                return true;
            case CommandType::EXPIRE: {
                const auto seconds = std::stoll(command.args[1]);
                if (seconds < 0) {
                    error = "negative expiration";
                    return false;
                }
                storage.expire_at(command.args[0],
                                  StorageEngine::Clock::now() + std::chrono::seconds(seconds));
                return true;
            }
            case CommandType::EXPIREAT:
                storage.expire_at(command.args[0],
                                  from_epoch_millis(std::stoll(command.args[1])));
                return true;
            case CommandType::INCR: {
                std::string increment_error;
                return storage.increment(command.args[0], 1, increment_error).has_value();
            }
            case CommandType::DECR: {
                std::string increment_error;
                return storage.increment(command.args[0], -1, increment_error).has_value();
            }
            case CommandType::APPEND:
                storage.append(command.args[0], command.args[1]);
                return true;
            default:
                error = "command is not replayable";
                return false;
        }
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

} // namespace bytecachedb
