#include "storage_engine.hpp"
#include "test_framework.hpp"

#include <chrono>
#include <thread>

using namespace bytecachedb;

int main() {
    int failures = 0;

    failures += test::run("EXPIRE and TTL", [] {
        StorageEngine storage;
        storage.set("session", "abc");
        ASSERT_TRUE(storage.expire_at("session", StorageEngine::Clock::now() + std::chrono::seconds(2)));
        const auto remaining = storage.ttl("session");
        ASSERT_TRUE(remaining >= 1);
        ASSERT_TRUE(remaining <= 2);
    });

    failures += test::run("expired key disappears", [] {
        StorageEngine storage;
        storage.set("temp", "1");
        ASSERT_TRUE(storage.expire_at("temp",
                                      StorageEngine::Clock::now() +
                                          std::chrono::milliseconds(50)));
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        ASSERT_FALSE(storage.get("temp").has_value());
        ASSERT_EQ(-2LL, storage.ttl("temp"));
    });

    failures += test::run("PERSIST removes expiration", [] {
        StorageEngine storage;
        storage.set("name", "value");
        ASSERT_TRUE(storage.expire_at("name", StorageEngine::Clock::now() + std::chrono::seconds(10)));
        ASSERT_TRUE(storage.persist("name"));
        ASSERT_EQ(-1LL, storage.ttl("name"));
    });

    failures += test::run("cleanup removes expired keys", [] {
        StorageEngine storage;
        storage.set("a", "1");
        storage.set("b", "2");
        storage.expire_at("a", StorageEngine::Clock::now() + std::chrono::milliseconds(20));
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        ASSERT_EQ(static_cast<size_t>(1), storage.cleanup_expired());
        ASSERT_EQ(static_cast<size_t>(1), storage.size());
    });

    return failures == 0 ? 0 : 1;
}
