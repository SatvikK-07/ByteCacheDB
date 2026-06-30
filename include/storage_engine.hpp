#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
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
        uint64_t last_access_sequence;
    };

    struct SnapshotEntry {
        std::string key;
        std::string value;
        std::optional<TimePoint> expires_at;
    };

    explicit StorageEngine(size_t max_keys = 0, size_t shard_count = 64);

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
    size_t evicted_keys() const;
    size_t expired_keys_removed() const;
    size_t shard_count() const;
    std::vector<SnapshotEntry> snapshot_entries() const;
    std::vector<std::string> take_evicted_keys();
    void set_max_keys(size_t max_keys);
    void set_eviction_enabled(bool enabled);

private:
    struct Shard {
        mutable std::shared_mutex mutex;
        std::unordered_map<std::string, ValueEntry> entries;
    };

    std::vector<std::unique_ptr<Shard>> shards_;
    std::atomic<size_t> total_keys_{0};
    std::atomic<size_t> max_keys_{0};
    std::atomic<size_t> evicted_keys_{0};
    std::atomic<size_t> expired_keys_removed_{0};
    std::atomic<uint64_t> access_sequence_{0};
    std::atomic<bool> eviction_enabled_{true};
    std::mutex eviction_mutex_;
    std::mutex evicted_keys_mutex_;
    std::vector<std::string> pending_evicted_keys_;

    static bool is_expired(const ValueEntry& entry, TimePoint now);
    size_t shard_index(const std::string& key) const;
    uint64_t next_access_sequence();
    static size_t cleanup_expired_locked(Shard& shard, TimePoint now);
    void enforce_max_keys();
};

} // namespace bytecachedb
