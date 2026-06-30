#include "metrics.hpp"

#include <sstream>

namespace bytecachedb {

Metrics::Metrics() : started_at_(std::chrono::steady_clock::now()) {}

void Metrics::client_connected() {
    connected_clients_.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::client_disconnected() {
    size_t current = connected_clients_.load(std::memory_order_relaxed);
    while (current > 0 &&
           !connected_clients_.compare_exchange_weak(current,
                                                     current - 1,
                                                     std::memory_order_relaxed)) {
    }
}

void Metrics::command_processed() {
    total_commands_.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::add_expired_keys(size_t count) {
    expired_keys_removed_.fetch_add(count, std::memory_order_relaxed);
}

size_t Metrics::connected_clients() const {
    return connected_clients_.load(std::memory_order_relaxed);
}

size_t Metrics::total_commands() const {
    return total_commands_.load(std::memory_order_relaxed);
}

size_t Metrics::expired_keys_removed() const {
    return expired_keys_removed_.load(std::memory_order_relaxed);
}

uint64_t Metrics::uptime_seconds() const {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - started_at_)
            .count());
}

std::string Metrics::info(size_t total_keys,
                          size_t memory_estimate_bytes,
                          size_t worker_threads,
                          bool aof_enabled) const {
    std::ostringstream out;
    out << "server_uptime_seconds: " << uptime_seconds() << "\n";
    out << "connected_clients: " << connected_clients() << "\n";
    out << "total_commands: " << total_commands() << "\n";
    out << "total_keys: " << total_keys << "\n";
    out << "expired_keys_removed: " << expired_keys_removed() << "\n";
    out << "memory_estimate_bytes: " << memory_estimate_bytes << "\n";
    out << "worker_threads: " << worker_threads << "\n";
    out << "aof_enabled: " << (aof_enabled ? "true" : "false") << "\n";
    return out.str();
}

} // namespace bytecachedb
