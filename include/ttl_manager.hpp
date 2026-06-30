#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "metrics.hpp"
#include "storage_engine.hpp"

namespace bytecachedb {

class TtlManager {
public:
    TtlManager(StorageEngine& storage, Metrics& metrics);
    ~TtlManager();

    TtlManager(const TtlManager&) = delete;
    TtlManager& operator=(const TtlManager&) = delete;

    void start();
    void stop();

private:
    void run();

    StorageEngine& storage_;
    Metrics& metrics_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    std::condition_variable condition_;
    std::thread worker_;
};

} // namespace bytecachedb
