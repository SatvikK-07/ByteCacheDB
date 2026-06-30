#include "persistence.hpp"

#include <chrono>
#include <filesystem>
#include <sstream>
#include <utility>

namespace bytecachedb {

AppendOnlyFile::AppendOnlyFile(bool enabled, std::string path)
    : enabled_(enabled), path_(std::move(path)) {}

AppendOnlyFile::~AppendOnlyFile() {
    close();
}

bool AppendOnlyFile::enabled() const {
    return enabled_;
}

const std::string& AppendOnlyFile::path() const {
    return path_;
}

bool AppendOnlyFile::open(std::string& error) {
    if (!enabled_) {
        return true;
    }

    std::lock_guard lock(mutex_);
    try {
        const std::filesystem::path path(path_);
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        stream_.open(path_, std::ios::app);
    } catch (const std::exception& ex) {
        error = std::string("failed to prepare AOF path: ") + ex.what();
        return false;
    }

    if (!stream_.is_open()) {
        error = "failed to open AOF file: " + path_;
        return false;
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
    if (!stream_.is_open()) {
        error = "AOF stream is not open";
        return false;
    }

    stream_ << line << '\n';
    stream_.flush();
    if (!stream_) {
        error = "failed to write AOF command";
        return false;
    }
    return true;
}

bool AppendOnlyFile::replay(StorageEngine& storage, std::string& error) const {
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
    while (std::getline(input, line)) {
        ++line_number;
        const auto parsed = parser.parse(line);
        if (!parsed.ok) {
            error = "invalid AOF line " + std::to_string(line_number) + ": " + parsed.error;
            return false;
        }
        if (!apply_replay_command(storage, parsed.command, error)) {
            error = "failed to replay AOF line " + std::to_string(line_number) + ": " + error;
            return false;
        }
    }

    return true;
}

void AppendOnlyFile::close() {
    std::lock_guard lock(mutex_);
    if (stream_.is_open()) {
        stream_.close();
    }
}

std::string AppendOnlyFile::serialize(const Command& command) {
    std::ostringstream out;
    out << CommandParser::command_name(command.type);
    for (const auto& arg : command.args) {
        out << ' ' << arg;
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
            case CommandType::EXPIREAT: {
                const auto millis = std::stoll(command.args[1]);
                storage.expire_at(command.args[0], from_epoch_millis(millis));
                return true;
            }
            case CommandType::INCR: {
                std::string increment_error;
                storage.increment(command.args[0], 1, increment_error);
                return true;
            }
            case CommandType::DECR: {
                std::string increment_error;
                storage.increment(command.args[0], -1, increment_error);
                return true;
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
