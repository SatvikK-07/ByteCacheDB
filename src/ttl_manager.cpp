#include "ttl_manager.hpp"

#include <chrono>

namespace bytecachedb {

TtlManager::TtlManager(StorageEngine& storage, Metrics& metrics)
    : storage_(storage), metrics_(metrics) {}

TtlManager::~TtlManager() {
    stop();
}

void TtlManager::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }
    worker_ = std::thread([this] { run(); });
}

void TtlManager::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;
    }
    condition_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void TtlManager::run() {
    std::unique_lock lock(mutex_);
    while (running_.load()) {
        condition_.wait_for(lock, std::chrono::seconds(1));
        if (!running_.load()) {
            break;
        }
        lock.unlock();
        const auto removed = storage_.cleanup_expired();
        if (removed > 0) {
            metrics_.add_expired_keys(removed);
        }
        lock.lock();
    }
}

} // namespace bytecachedb
