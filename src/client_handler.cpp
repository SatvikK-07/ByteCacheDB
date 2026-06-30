#include "client_handler.hpp"

#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstring>
#include <future>
#include <memory>
#include <optional>
#include <string_view>
#include <sys/socket.h>
#include <unistd.h>

#include "response.hpp"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace bytecachedb {

namespace {

std::optional<int64_t> parse_int64(const std::string& value) {
    int64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto [ptr, ec] = std::from_chars(begin, end, parsed);
    if (ec != std::errc() || ptr != end) {
        return std::nullopt;
    }
    return parsed;
}

Command expire_at_log_command(const std::string& key, StorageEngine::TimePoint expires_at) {
    Command command;
    command.type = CommandType::EXPIREAT;
    command.args = {key, std::to_string(AppendOnlyFile::to_epoch_millis(expires_at))};
    return command;
}

} // namespace

ClientHandler::ClientHandler(StorageEngine& storage,
                             CommandParser& parser,
                             AppendOnlyFile& persistence,
                             Metrics& metrics,
                             ThreadPool& command_pool,
                             std::mutex& write_path_mutex,
                             size_t worker_threads,
                             bool aof_enabled,
                             size_t max_line_bytes)
    : storage_(storage),
      parser_(parser),
      persistence_(persistence),
      metrics_(metrics),
      command_pool_(command_pool),
      write_path_mutex_(write_path_mutex),
      worker_threads_(worker_threads),
      aof_enabled_(aof_enabled),
      max_line_bytes_(max_line_bytes) {}

void ClientHandler::handle(int client_fd) {
    std::string buffer;
    buffer.reserve(4096);
    char chunk[4096];

    while (true) {
        const ssize_t bytes_read = ::recv(client_fd, chunk, sizeof(chunk), 0);
        if (bytes_read == 0) {
            return;
        }
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }

        buffer.append(chunk, static_cast<size_t>(bytes_read));
        if (buffer.size() > max_line_bytes_ && buffer.find('\n') == std::string::npos) {
            send_all(client_fd, response::error("input line too large"));
            return;
        }

        size_t newline_pos = std::string::npos;
        while ((newline_pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, newline_pos);
            buffer.erase(0, newline_pos + 1);

            if (line.size() > max_line_bytes_) {
                if (!send_all(client_fd, response::error("input line too large"))) {
                    return;
                }
                continue;
            }

            metrics_.command_processed();
            auto parsed = parser_.parse(line);
            if (!parsed.ok) {
                if (!send_all(client_fd, response::error(parsed.error))) {
                    return;
                }
                continue;
            }

            auto promise = std::make_shared<std::promise<ExecutionResult>>();
            auto future = promise->get_future();
            const bool queued = command_pool_.enqueue(
                [this, command = std::move(parsed.command), promise] {
                    promise->set_value(execute_ordered(command));
                });
            if (!queued) {
                send_all(client_fd, response::error("server is shutting down"));
                return;
            }
            auto result = future.get();
            if (!send_all(client_fd, result.response)) {
                return;
            }
        }
    }
}

ClientHandler::ExecutionResult ClientHandler::execute(const Command& command) {
    ExecutionResult result;

    switch (command.type) {
        case CommandType::PING:
            result.response = response::simple_string("PONG");
            return result;

        case CommandType::SET:
            storage_.set(command.args[0], command.args[1]);
            result.response = response::simple_string("OK");
            result.should_log = true;
            result.log_command = command;
            return result;

        case CommandType::GET: {
            const auto value = storage_.get(command.args[0]);
            result.response = value.has_value() ? response::bulk_string(*value)
                                                : response::null_bulk_string();
            return result;
        }

        case CommandType::DEL: {
            const auto removed = storage_.del(command.args[0]);
            result.response = response::integer(static_cast<long long>(removed));
            result.should_log = removed > 0;
            result.log_command = command;
            return result;
        }

        case CommandType::EXISTS:
            result.response = response::integer(storage_.exists(command.args[0]) ? 1 : 0);
            return result;

        case CommandType::EXPIRE: {
            const auto seconds = parse_int64(command.args[1]);
            if (!seconds.has_value() || *seconds < 0) {
                result.response = response::error("invalid TTL value");
                return result;
            }
            const auto expires_at =
                StorageEngine::Clock::now() + std::chrono::seconds(*seconds);
            const bool updated = storage_.expire_at(command.args[0], expires_at);
            result.response = response::integer(updated ? 1 : 0);
            result.should_log = updated;
            result.log_command = expire_at_log_command(command.args[0], expires_at);
            return result;
        }

        case CommandType::EXPIREAT: {
            const auto millis = parse_int64(command.args[1]);
            if (!millis.has_value()) {
                result.response = response::error("invalid expiration timestamp");
                return result;
            }
            const bool updated =
                storage_.expire_at(command.args[0], AppendOnlyFile::from_epoch_millis(*millis));
            result.response = response::integer(updated ? 1 : 0);
            result.should_log = updated;
            result.log_command = command;
            return result;
        }

        case CommandType::TTL:
            result.response = response::integer(storage_.ttl(command.args[0]));
            return result;

        case CommandType::PERSIST: {
            const bool updated = storage_.persist(command.args[0]);
            result.response = response::integer(updated ? 1 : 0);
            result.should_log = updated;
            result.log_command = command;
            return result;
        }

        case CommandType::KEYS:
            result.response = response::array(storage_.keys());
            return result;

        case CommandType::FLUSH:
            storage_.flush();
            result.response = response::simple_string("OK");
            result.should_log = true;
            result.log_command = command;
            return result;

        case CommandType::INFO:
            result.response =
                response::bulk_string(metrics_.info(storage_.size(),
                                                    storage_.memory_estimate_bytes(),
                                                    worker_threads_,
                                                    aof_enabled_,
                                                    storage_.expired_keys_removed(),
                                                    storage_.evicted_keys(),
                                                    storage_.shard_count(),
                                                    persistence_.fsync_policy_name()));
            return result;

        case CommandType::MSET:
            storage_.mset(command.args);
            result.response = response::simple_string("OK");
            result.should_log = true;
            result.log_command = command;
            return result;

        case CommandType::MGET:
            result.response = response::array(storage_.mget(command.args));
            return result;

        case CommandType::INCR:
        case CommandType::DECR: {
            std::string error;
            const long long delta = command.type == CommandType::INCR ? 1 : -1;
            const auto next = storage_.increment(command.args[0], delta, error);
            if (!next.has_value()) {
                result.response = response::error(error);
                return result;
            }
            result.response = response::integer(*next);
            result.should_log = true;
            result.log_command = command;
            return result;
        }

        case CommandType::APPEND: {
            const auto length = storage_.append(command.args[0], command.args[1]);
            result.response = response::integer(static_cast<long long>(length));
            result.should_log = true;
            result.log_command = command;
            return result;
        }

        case CommandType::STRLEN: {
            const auto length = storage_.strlen(command.args[0]).value_or(0);
            result.response = response::integer(static_cast<long long>(length));
            return result;
        }

        case CommandType::SAVE: {
            std::string error;
            if (!persistence_.save_snapshot(storage_, error)) {
                result.response = response::error("snapshot failure: " + error);
                return result;
            }
            result.response = response::simple_string("OK");
            return result;
        }

        case CommandType::UNKNOWN:
            result.response = response::error("unknown command");
            return result;
    }

    result.response = response::error("unknown command");
    return result;
}

ClientHandler::ExecutionResult ClientHandler::execute_ordered(const Command& command) {
    if (command.type == CommandType::SAVE ||
        (CommandParser::is_mutating(command.type) && persistence_.enabled())) {
        std::lock_guard lock(write_path_mutex_);
        return persist_if_needed(execute(command));
    }
    return persist_if_needed(execute(command));
}

bool ClientHandler::send_all(int fd, const std::string& data) const {
    size_t total_sent = 0;
    while (total_sent < data.size()) {
        const ssize_t sent =
            ::send(fd, data.data() + total_sent, data.size() - total_sent, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (sent == 0) {
            return false;
        }
        total_sent += static_cast<size_t>(sent);
    }
    return true;
}

ClientHandler::ExecutionResult ClientHandler::persist_if_needed(ExecutionResult result) {
    if (!result.should_log) {
        return result;
    }
    if (!persistence_.enabled()) {
        storage_.take_evicted_keys();
        return result;
    }

    std::string error;
    bool persisted = true;
    if (result.should_log && !persistence_.append(result.log_command, error)) {
        persisted = false;
    }
    for (const auto& key : storage_.take_evicted_keys()) {
        Command eviction{CommandType::DEL, {key}, {}};
        if (!persistence_.append(eviction, error)) {
            persisted = false;
        }
    }
    if (!persisted) {
        result.response = response::error("persistence failure: " + error);
    }
    return result;
}

} // namespace bytecachedb
