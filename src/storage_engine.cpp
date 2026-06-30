#include "storage_engine.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <mutex>
#include <unordered_set>

namespace bytecachedb {

StorageEngine::StorageEngine(size_t max_keys, size_t shard_count)
    : max_keys_(max_keys) {
    shard_count = std::max<size_t>(1, shard_count);
    shards_.reserve(shard_count);
    for (size_t i = 0; i < shard_count; ++i) {
        shards_.push_back(std::make_unique<Shard>());
    }
}

void StorageEngine::set(const std::string& key, const std::string& value) {
    const auto now = Clock::now();
    auto& shard = *shards_[shard_index(key)];
    {
        std::unique_lock lock(shard.mutex);
        auto it = shard.entries.find(key);
        if (it != shard.entries.end() && is_expired(it->second, now)) {
            shard.entries.erase(it);
            total_keys_.fetch_sub(1, std::memory_order_relaxed);
            expired_keys_removed_.fetch_add(1, std::memory_order_relaxed);
            it = shard.entries.end();
        }
        if (it == shard.entries.end()) {
            shard.entries.emplace(
                key, ValueEntry{value, std::nullopt, now, next_access_sequence()});
            total_keys_.fetch_add(1, std::memory_order_relaxed);
        } else {
            it->second = ValueEntry{value, std::nullopt, now, next_access_sequence()};
        }
    }
    enforce_max_keys();
}

std::vector<std::string> StorageEngine::mset(const std::vector<std::string>& args) {
    const auto now = Clock::now();
    std::vector<size_t> indexes;
    indexes.reserve(args.size() / 2);
    for (size_t i = 0; i < args.size(); i += 2) {
        indexes.push_back(shard_index(args[i]));
    }
    std::sort(indexes.begin(), indexes.end());
    indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());

    std::vector<std::unique_lock<std::shared_mutex>> locks;
    locks.reserve(indexes.size());
    for (const auto index : indexes) {
        locks.emplace_back(shards_[index]->mutex);
    }

    std::vector<std::string> keys;
    keys.reserve(args.size() / 2);
    for (size_t i = 0; i < args.size(); i += 2) {
        auto& entries = shards_[shard_index(args[i])]->entries;
        auto it = entries.find(args[i]);
        if (it != entries.end() && is_expired(it->second, now)) {
            entries.erase(it);
            total_keys_.fetch_sub(1, std::memory_order_relaxed);
            expired_keys_removed_.fetch_add(1, std::memory_order_relaxed);
            it = entries.end();
        }
        if (it == entries.end()) {
            entries.emplace(
                args[i],
                ValueEntry{args[i + 1], std::nullopt, now, next_access_sequence()});
            total_keys_.fetch_add(1, std::memory_order_relaxed);
        } else {
            it->second =
                ValueEntry{args[i + 1], std::nullopt, now, next_access_sequence()};
        }
        keys.push_back(args[i]);
    }
    locks.clear();
    enforce_max_keys();
    return keys;
}

std::optional<std::string> StorageEngine::get(const std::string& key) {
    const auto now = Clock::now();
    auto& shard = *shards_[shard_index(key)];
    std::unique_lock lock(shard.mutex);
    const auto it = shard.entries.find(key);
    if (it == shard.entries.end()) {
        return std::nullopt;
    }
    if (is_expired(it->second, now)) {
        shard.entries.erase(it);
        total_keys_.fetch_sub(1, std::memory_order_relaxed);
        expired_keys_removed_.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }
    it->second.last_access_sequence = next_access_sequence();
    return it->second.value;
}

std::vector<std::optional<std::string>> StorageEngine::mget(
    const std::vector<std::string>& keys) {
    std::vector<std::optional<std::string>> values;
    values.reserve(keys.size());
    for (const auto& key : keys) {
        values.push_back(get(key));
    }
    return values;
}

size_t StorageEngine::del(const std::string& key) {
    auto& shard = *shards_[shard_index(key)];
    std::unique_lock lock(shard.mutex);
    const size_t removed = shard.entries.erase(key);
    if (removed > 0) {
        total_keys_.fetch_sub(removed, std::memory_order_relaxed);
    }
    return removed;
}

bool StorageEngine::exists(const std::string& key) {
    return get(key).has_value();
}

bool StorageEngine::expire_at(const std::string& key, TimePoint expires_at) {
    const auto now = Clock::now();
    auto& shard = *shards_[shard_index(key)];
    std::unique_lock lock(shard.mutex);
    const auto it = shard.entries.find(key);
    if (it == shard.entries.end()) {
        return false;
    }
    if (is_expired(it->second, now) || expires_at <= now) {
        shard.entries.erase(it);
        total_keys_.fetch_sub(1, std::memory_order_relaxed);
        expired_keys_removed_.fetch_add(1, std::memory_order_relaxed);
        return expires_at <= now;
    }
    it->second.expires_at = expires_at;
    it->second.last_access_sequence = next_access_sequence();
    return true;
}

int64_t StorageEngine::ttl(const std::string& key) {
    const auto now = Clock::now();
    auto& shard = *shards_[shard_index(key)];
    std::unique_lock lock(shard.mutex);
    const auto it = shard.entries.find(key);
    if (it == shard.entries.end()) {
        return -2;
    }
    if (is_expired(it->second, now)) {
        shard.entries.erase(it);
        total_keys_.fetch_sub(1, std::memory_order_relaxed);
        expired_keys_removed_.fetch_add(1, std::memory_order_relaxed);
        return -2;
    }
    it->second.last_access_sequence = next_access_sequence();
    if (!it->second.expires_at.has_value()) {
        return -1;
    }
    const auto remaining_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(*it->second.expires_at - now).count();
    return remaining_ms <= 0 ? -2 : (remaining_ms + 999) / 1000;
}

bool StorageEngine::persist(const std::string& key) {
    const auto now = Clock::now();
    auto& shard = *shards_[shard_index(key)];
    std::unique_lock lock(shard.mutex);
    const auto it = shard.entries.find(key);
    if (it == shard.entries.end()) {
        return false;
    }
    if (is_expired(it->second, now)) {
        shard.entries.erase(it);
        total_keys_.fetch_sub(1, std::memory_order_relaxed);
        expired_keys_removed_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    const bool had_expiration = it->second.expires_at.has_value();
    it->second.expires_at.reset();
    it->second.last_access_sequence = next_access_sequence();
    return had_expiration;
}

std::vector<std::string> StorageEngine::keys() {
    const auto entries = snapshot_entries();
    std::vector<std::string> result;
    result.reserve(entries.size());
    for (const auto& entry : entries) {
        result.push_back(entry.key);
    }
    std::sort(result.begin(), result.end());
    return result;
}

size_t StorageEngine::flush() {
    std::vector<std::unique_lock<std::shared_mutex>> locks;
    locks.reserve(shards_.size());
    for (auto& shard : shards_) {
        locks.emplace_back(shard->mutex);
    }
    const size_t count = total_keys_.exchange(0, std::memory_order_relaxed);
    for (auto& shard : shards_) {
        shard->entries.clear();
    }
    return count;
}

std::optional<long long> StorageEngine::increment(const std::string& key,
                                                  long long delta,
                                                  std::string& error) {
    const auto now = Clock::now();
    auto& shard = *shards_[shard_index(key)];
    bool inserted = false;
    long long next = 0;
    {
        std::unique_lock lock(shard.mutex);
        auto it = shard.entries.find(key);
        if (it != shard.entries.end() && is_expired(it->second, now)) {
            shard.entries.erase(it);
            total_keys_.fetch_sub(1, std::memory_order_relaxed);
            expired_keys_removed_.fetch_add(1, std::memory_order_relaxed);
            it = shard.entries.end();
        }

        long long current = 0;
        if (it != shard.entries.end()) {
            const auto& value = it->second.value;
            const auto [ptr, ec] =
                std::from_chars(value.data(), value.data() + value.size(), current);
            if (ec != std::errc() || ptr != value.data() + value.size()) {
                error = "value is not an integer";
                return std::nullopt;
            }
        }
        if ((delta > 0 && current > std::numeric_limits<long long>::max() - delta) ||
            (delta < 0 && current < std::numeric_limits<long long>::min() - delta)) {
            error = "integer overflow";
            return std::nullopt;
        }

        next = current + delta;
        if (it == shard.entries.end()) {
            shard.entries.emplace(
                key,
                ValueEntry{std::to_string(next),
                           std::nullopt,
                           now,
                           next_access_sequence()});
            total_keys_.fetch_add(1, std::memory_order_relaxed);
            inserted = true;
        } else {
            it->second.value = std::to_string(next);
            it->second.last_access_sequence = next_access_sequence();
        }
    }
    if (inserted) {
        enforce_max_keys();
    }
    return next;
}

size_t StorageEngine::append(const std::string& key, const std::string& suffix) {
    const auto now = Clock::now();
    auto& shard = *shards_[shard_index(key)];
    bool inserted = false;
    size_t length = 0;
    {
        std::unique_lock lock(shard.mutex);
        auto it = shard.entries.find(key);
        if (it != shard.entries.end() && is_expired(it->second, now)) {
            shard.entries.erase(it);
            total_keys_.fetch_sub(1, std::memory_order_relaxed);
            expired_keys_removed_.fetch_add(1, std::memory_order_relaxed);
            it = shard.entries.end();
        }
        if (it == shard.entries.end()) {
            shard.entries.emplace(
                key, ValueEntry{suffix, std::nullopt, now, next_access_sequence()});
            total_keys_.fetch_add(1, std::memory_order_relaxed);
            inserted = true;
            length = suffix.size();
        } else {
            it->second.value.append(suffix);
            it->second.last_access_sequence = next_access_sequence();
            length = it->second.value.size();
        }
    }
    if (inserted) {
        enforce_max_keys();
    }
    return length;
}

std::optional<size_t> StorageEngine::strlen(const std::string& key) {
    const auto value = get(key);
    return value.has_value() ? std::optional<size_t>(value->size()) : std::optional<size_t>(0);
}

size_t StorageEngine::cleanup_expired() {
    const auto now = Clock::now();
    size_t removed = 0;
    for (auto& shard : shards_) {
        std::unique_lock lock(shard->mutex);
        removed += cleanup_expired_locked(*shard, now);
    }
    if (removed > 0) {
        total_keys_.fetch_sub(removed, std::memory_order_relaxed);
        expired_keys_removed_.fetch_add(removed, std::memory_order_relaxed);
    }
    return removed;
}

size_t StorageEngine::size() {
    cleanup_expired();
    return total_keys_.load(std::memory_order_relaxed);
}

size_t StorageEngine::memory_estimate_bytes() {
    const auto now = Clock::now();
    size_t total = 0;
    for (const auto& shard : shards_) {
        std::shared_lock lock(shard->mutex);
        for (const auto& [key, entry] : shard->entries) {
            if (!is_expired(entry, now)) {
                total += sizeof(ValueEntry) + key.capacity() + entry.value.capacity();
            }
        }
    }
    return total;
}

size_t StorageEngine::evicted_keys() const {
    return evicted_keys_.load(std::memory_order_relaxed);
}

size_t StorageEngine::expired_keys_removed() const {
    return expired_keys_removed_.load(std::memory_order_relaxed);
}

size_t StorageEngine::shard_count() const {
    return shards_.size();
}

std::vector<StorageEngine::SnapshotEntry> StorageEngine::snapshot_entries() const {
    const auto now = Clock::now();
    std::vector<std::shared_lock<std::shared_mutex>> locks;
    locks.reserve(shards_.size());
    for (const auto& shard : shards_) {
        locks.emplace_back(shard->mutex);
    }

    std::vector<SnapshotEntry> result;
    result.reserve(total_keys_.load(std::memory_order_relaxed));
    for (const auto& shard : shards_) {
        for (const auto& [key, entry] : shard->entries) {
            if (!is_expired(entry, now)) {
                result.push_back({key, entry.value, entry.expires_at});
            }
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.key < rhs.key;
    });
    return result;
}

std::vector<std::string> StorageEngine::take_evicted_keys() {
    std::lock_guard lock(evicted_keys_mutex_);
    std::vector<std::string> keys;
    keys.swap(pending_evicted_keys_);
    return keys;
}

void StorageEngine::set_max_keys(size_t max_keys) {
    max_keys_.store(max_keys, std::memory_order_relaxed);
    enforce_max_keys();
}

void StorageEngine::set_eviction_enabled(bool enabled) {
    eviction_enabled_.store(enabled, std::memory_order_relaxed);
    if (enabled) {
        enforce_max_keys();
    }
}

bool StorageEngine::is_expired(const ValueEntry& entry, TimePoint now) {
    return entry.expires_at.has_value() && *entry.expires_at <= now;
}

size_t StorageEngine::shard_index(const std::string& key) const {
    return std::hash<std::string>{}(key) % shards_.size();
}

uint64_t StorageEngine::next_access_sequence() {
    return access_sequence_.fetch_add(1, std::memory_order_relaxed) + 1;
}

size_t StorageEngine::cleanup_expired_locked(Shard& shard, TimePoint now) {
    size_t removed = 0;
    for (auto it = shard.entries.begin(); it != shard.entries.end();) {
        if (is_expired(it->second, now)) {
            it = shard.entries.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

void StorageEngine::enforce_max_keys() {
    const size_t limit = max_keys_.load(std::memory_order_relaxed);
    if (!eviction_enabled_.load(std::memory_order_relaxed) || limit == 0 ||
        total_keys_.load(std::memory_order_relaxed) <= limit) {
        return;
    }

    std::lock_guard eviction_lock(eviction_mutex_);
    while (total_keys_.load(std::memory_order_relaxed) > limit) {
        std::optional<std::pair<size_t, std::string>> victim;
        uint64_t oldest = std::numeric_limits<uint64_t>::max();

        for (size_t index = 0; index < shards_.size(); ++index) {
            auto& shard = *shards_[index];
            std::shared_lock lock(shard.mutex);
            for (const auto& [key, entry] : shard.entries) {
                if (!victim.has_value() || entry.last_access_sequence < oldest) {
                    victim = std::make_pair(index, key);
                    oldest = entry.last_access_sequence;
                }
            }
        }

        if (!victim.has_value()) {
            return;
        }
        auto& shard = *shards_[victim->first];
        std::unique_lock lock(shard.mutex);
        if (shard.entries.erase(victim->second) > 0) {
            total_keys_.fetch_sub(1, std::memory_order_relaxed);
            evicted_keys_.fetch_add(1, std::memory_order_relaxed);
            std::lock_guard pending_lock(evicted_keys_mutex_);
            pending_evicted_keys_.push_back(victim->second);
        }
    }
}

} // namespace bytecachedb
