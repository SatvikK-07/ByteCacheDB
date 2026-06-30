#pragma once

#include <chrono>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace bytecachedb {

class StorageEngine {
public:
    using Clock = std::chrono::system_clock;
    using TimePoint = Clock::time_point;

    struct ValueEntry {
        std::string value;
        std::optional<TimePoint> expires_at;
        TimePoint created_at;
        TimePoint last_accessed;
    };

    explicit StorageEngine(size_t max_keys = 0);

    void set(const std::string& key, const std::string& value);
    std::vector<std::string> mset(const std::vector<std::string>& args);
    std::optional<std::string> get(const std::string& key);
    std::vector<std::optional<std::string>> mget(const std::vector<std::string>& keys);
    size_t del(const std::string& key);
    bool exists(const std::string& key);
    bool expire_at(const std::string& key, TimePoint expires_at);
    int64_t ttl(const std::string& key);
    bool persist(const std::string& key);
    std::vector<std::string> keys();
    size_t flush();
    std::optional<long long> increment(const std::string& key, long long delta, std::string& error);
    size_t append(const std::string& key, const std::string& suffix);
    std::optional<size_t> strlen(const std::string& key);

    size_t cleanup_expired();
    size_t size();
    size_t memory_estimate_bytes();
    void set_max_keys(size_t max_keys);

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, ValueEntry> entries_;
    size_t max_keys_{0};

    static bool is_expired(const ValueEntry& entry, TimePoint now);
    void cleanup_expired_locked(TimePoint now, size_t* removed = nullptr);
    void evict_if_needed_locked();
};

} // namespace bytecachedb
