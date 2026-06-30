#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

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
    size_t storage_shards{64};
    FsyncPolicy fsync_policy{FsyncPolicy::EverySec};
    std::string snapshot_path{"data/bytecachedb.snapshot"};
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
    int port() const;

private:
    bool setup_socket(std::string& error);
    void close_listen_socket();
    void shutdown_clients();
    void join_connection_threads();

    ServerConfig config_;
    StorageEngine storage_;
    Metrics metrics_;
    CommandParser parser_;
    AppendOnlyFile persistence_;
    TtlManager ttl_manager_;
    ThreadPool thread_pool_;
    std::atomic<bool> running_{false};
    std::atomic<int> listen_fd_{-1};
    std::atomic<int> bound_port_{0};
    std::mutex write_path_mutex_;
    std::mutex clients_mutex_;
    std::set<int> client_fds_;
    std::vector<std::thread> connection_threads_;
    std::mutex stop_mutex_;
};

} // namespace bytecachedb
