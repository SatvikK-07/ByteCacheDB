#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <string>

namespace bytecachedb {

class Metrics {
public:
    Metrics();

    void client_connected();
    void client_disconnected();
    void command_processed();
    void add_expired_keys(size_t count);

    size_t connected_clients() const;
    size_t total_commands() const;
    size_t expired_keys_removed() const;
    uint64_t uptime_seconds() const;

    std::string info(size_t total_keys,
                     size_t memory_estimate_bytes,
                     size_t worker_threads,
                     bool aof_enabled) const;

private:
    std::chrono::steady_clock::time_point started_at_;
    std::atomic<size_t> connected_clients_{0};
    std::atomic<size_t> total_commands_{0};
    std::atomic<size_t> expired_keys_removed_{0};
};

} // namespace bytecachedb
