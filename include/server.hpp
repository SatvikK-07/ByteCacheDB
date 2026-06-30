#pragma once

#include <atomic>
#include <cstddef>
#include <string>

#include "client_handler.hpp"
#include "command_parser.hpp"
#include "metrics.hpp"
#include "persistence.hpp"
#include "storage_engine.hpp"
#include "thread_pool.hpp"
#include "ttl_manager.hpp"

namespace bytecachedb {

struct ServerConfig {
    std::string host{"127.0.0.1"};
    int port{6379};
    size_t worker_threads{8};
    bool enable_aof{false};
    std::string aof_path{"data/bytecachedb.aof"};
    size_t max_line_bytes{1024 * 1024};
    size_t max_keys{0};
};

class Server {
public:
    explicit Server(ServerConfig config);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool start(std::string& error);
    void stop();
    int listen_fd() const;

private:
    bool setup_socket(std::string& error);
    void close_listen_socket();

    ServerConfig config_;
    StorageEngine storage_;
    Metrics metrics_;
    CommandParser parser_;
    AppendOnlyFile persistence_;
    TtlManager ttl_manager_;
    ThreadPool thread_pool_;
    std::atomic<bool> running_{false};
    int listen_fd_{-1};
};

} // namespace bytecachedb
