#pragma once

#include <mutex>
#include <string>

#include "command_parser.hpp"
#include "metrics.hpp"
#include "persistence.hpp"
#include "storage_engine.hpp"
#include "thread_pool.hpp"

namespace bytecachedb {

class ClientHandler {
public:
    ClientHandler(StorageEngine& storage,
                  CommandParser& parser,
                  AppendOnlyFile& persistence,
                  Metrics& metrics,
                  ThreadPool& command_pool,
                  std::mutex& write_path_mutex,
                  size_t worker_threads,
                  bool aof_enabled,
                  size_t max_line_bytes);

    void handle(int client_fd);

private:
    struct ExecutionResult {
        std::string response;
        bool should_log{false};
        Command log_command;
    };

    ExecutionResult execute(const Command& command);
    ExecutionResult execute_ordered(const Command& command);
    bool send_all(int fd, const std::string& data) const;
    ExecutionResult persist_if_needed(ExecutionResult result);

    StorageEngine& storage_;
    CommandParser& parser_;
    AppendOnlyFile& persistence_;
    Metrics& metrics_;
    ThreadPool& command_pool_;
    std::mutex& write_path_mutex_;
    size_t worker_threads_;
    bool aof_enabled_;
    size_t max_line_bytes_;
};

} // namespace bytecachedb
