#include "storage_engine.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <mutex>

namespace bytecachedb {

StorageEngine::StorageEngine(size_t max_keys) : max_keys_(max_keys) {}

void StorageEngine::set(const std::string& key, const std::string& value) {
    const auto now = Clock::now();
    std::unique_lock lock(mutex_);
    entries_[key] = ValueEntry{value, std::nullopt, now, now};
    evict_if_needed_locked();
}

std::vector<std::string> StorageEngine::mset(const std::vector<std::string>& args) {
    const auto now = Clock::now();
    std::vector<std::string> keys;
    keys.reserve(args.size() / 2);

    std::unique_lock lock(mutex_);
    for (size_t i = 0; i < args.size(); i += 2) {
        entries_[args[i]] = ValueEntry{args[i + 1], std::nullopt, now, now};
        keys.push_back(args[i]);
    }
    evict_if_needed_locked();
    return keys;
}

std::optional<std::string> StorageEngine::get(const std::string& key) {
    const auto now = Clock::now();
    std::unique_lock lock(mutex_);
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
        return std::nullopt;
    }
    if (is_expired(it->second, now)) {
        entries_.erase(it);
        return std::nullopt;
    }
    it->second.last_accessed = now;
    return it->second.value;
}

std::vector<std::optional<std::string>> StorageEngine::mget(const std::vector<std::string>& keys) {
    const auto now = Clock::now();
    std::vector<std::optional<std::string>> values;
    values.reserve(keys.size());

    std::unique_lock lock(mutex_);
    for (const auto& key : keys) {
        const auto it = entries_.find(key);
        if (it == entries_.end()) {
            values.emplace_back(std::nullopt);
            continue;
        }
        if (is_expired(it->second, now)) {
            entries_.erase(it);
            values.emplace_back(std::nullopt);
            continue;
        }
        it->second.last_accessed = now;
        values.emplace_back(it->second.value);
    }
    return values;
}

size_t StorageEngine::del(const std::string& key) {
    std::unique_lock lock(mutex_);
    return entries_.erase(key);
}

bool StorageEngine::exists(const std::string& key) {
    const auto now = Clock::now();
    std::unique_lock lock(mutex_);
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
        return false;
    }
    if (is_expired(it->second, now)) {
        entries_.erase(it);
        return false;
    }
    it->second.last_accessed = now;
    return true;
}

bool StorageEngine::expire_at(const std::string& key, TimePoint expires_at) {
    const auto now = Clock::now();
    std::unique_lock lock(mutex_);
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
        return false;
    }
    if (is_expired(it->second, now)) {
        entries_.erase(it);
        return false;
    }
    if (expires_at <= now) {
        entries_.erase(it);
        return true;
    }
    it->second.expires_at = expires_at;
    it->second.last_accessed = now;
    return true;
}

int64_t StorageEngine::ttl(const std::string& key) {
    const auto now = Clock::now();
    std::unique_lock lock(mutex_);
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
        return -2;
    }
    if (is_expired(it->second, now)) {
        entries_.erase(it);
        return -2;
    }
    it->second.last_accessed = now;
    if (!it->second.expires_at.has_value()) {
        return -1;
    }
    const auto remaining_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(*it->second.expires_at - now).count();
    if (remaining_ms <= 0) {
        entries_.erase(it);
        return -2;
    }
    return (remaining_ms + 999) / 1000;
}

bool StorageEngine::persist(const std::string& key) {
    const auto now = Clock::now();
    std::unique_lock lock(mutex_);
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
        return false;
    }
    if (is_expired(it->second, now)) {
        entries_.erase(it);
        return false;
    }
    const bool had_expiration = it->second.expires_at.has_value();
    it->second.expires_at.reset();
    it->second.last_accessed = now;
    return had_expiration;
}

std::vector<std::string> StorageEngine::keys() {
    const auto now = Clock::now();
    std::unique_lock lock(mutex_);
    cleanup_expired_locked(now);
    std::vector<std::string> result;
    result.reserve(entries_.size());
    for (const auto& [key, _] : entries_) {
        result.push_back(key);
    }
    std::sort(result.begin(), result.end());
    return result;
}

size_t StorageEngine::flush() {
    std::unique_lock lock(mutex_);
    const size_t count = entries_.size();
    entries_.clear();
    return count;
}

std::optional<long long> StorageEngine::increment(const std::string& key,
                                                  long long delta,
                                                  std::string& error) {
    const auto now = Clock::now();
    std::unique_lock lock(mutex_);
    auto it = entries_.find(key);
    if (it != entries_.end() && is_expired(it->second, now)) {
        entries_.erase(it);
        it = entries_.end();
    }

    long long current = 0;
    if (it != entries_.end()) {
        const auto& value = it->second.value;
        auto begin = value.data();
        auto end = value.data() + value.size();
        const auto [ptr, ec] = std::from_chars(begin, end, current);
        if (ec != std::errc() || ptr != end) {
            error = "value is not an integer";
            return std::nullopt;
        }
    }

    if ((delta > 0 && current > std::numeric_limits<long long>::max() - delta) ||
        (delta < 0 && current < std::numeric_limits<long long>::min() - delta)) {
        error = "integer overflow";
        return std::nullopt;
    }

    const long long next = current + delta;
    if (it == entries_.end()) {
        entries_[key] = ValueEntry{std::to_string(next), std::nullopt, now, now};
        evict_if_needed_locked();
    } else {
        it->second.value = std::to_string(next);
        it->second.last_accessed = now;
    }
    return next;
}

size_t StorageEngine::append(const std::string& key, const std::string& suffix) {
    const auto now = Clock::now();
    std::unique_lock lock(mutex_);
    auto it = entries_.find(key);
    if (it != entries_.end() && is_expired(it->second, now)) {
        entries_.erase(it);
        it = entries_.end();
    }

    if (it == entries_.end()) {
        entries_[key] = ValueEntry{suffix, std::nullopt, now, now};
        evict_if_needed_locked();
        return suffix.size();
    }

    it->second.value.append(suffix);
    it->second.last_accessed = now;
    return it->second.value.size();
}

std::optional<size_t> StorageEngine::strlen(const std::string& key) {
    const auto value = get(key);
    if (!value.has_value()) {
        return 0;
    }
    return value->size();
}

size_t StorageEngine::cleanup_expired() {
    const auto now = Clock::now();
    size_t removed = 0;
    std::unique_lock lock(mutex_);
    cleanup_expired_locked(now, &removed);
    return removed;
}

size_t StorageEngine::size() {
    const auto now = Clock::now();
    std::unique_lock lock(mutex_);
    cleanup_expired_locked(now);
    return entries_.size();
}

size_t StorageEngine::memory_estimate_bytes() {
    const auto now = Clock::now();
    std::shared_lock lock(mutex_);
    size_t total = 0;
    for (const auto& [key, entry] : entries_) {
        if (!is_expired(entry, now)) {
            total += sizeof(ValueEntry) + key.capacity() + entry.value.capacity();
        }
    }
    return total;
}

void StorageEngine::set_max_keys(size_t max_keys) {
    std::unique_lock lock(mutex_);
    max_keys_ = max_keys;
    evict_if_needed_locked();
}

bool StorageEngine::is_expired(const ValueEntry& entry, TimePoint now) {
    return entry.expires_at.has_value() && *entry.expires_at <= now;
}

void StorageEngine::cleanup_expired_locked(TimePoint now, size_t* removed) {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (is_expired(it->second, now)) {
            it = entries_.erase(it);
            if (removed != nullptr) {
                ++(*removed);
            }
        } else {
            ++it;
        }
    }
}

void StorageEngine::evict_if_needed_locked() {
    if (max_keys_ == 0) {
        return;
    }

    while (entries_.size() > max_keys_) {
        const auto victim = std::min_element(
            entries_.begin(),
            entries_.end(),
            [](const auto& lhs, const auto& rhs) {
                return lhs.second.last_accessed < rhs.second.last_accessed;
            });
        if (victim == entries_.end()) {
            return;
        }
        entries_.erase(victim);
    }
}

} // namespace bytecachedb
