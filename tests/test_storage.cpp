#include "storage_engine.hpp"
#include "test_framework.hpp"

using namespace bytecachedb;

int main() {
    int failures = 0;

    failures += test::run("SET then GET", [] {
        StorageEngine storage;
        storage.set("name", "Satvik");
        ASSERT_EQ(std::string("Satvik"), storage.get("name").value());
    });

    failures += test::run("GET missing key", [] {
        StorageEngine storage;
        ASSERT_FALSE(storage.get("missing").has_value());
    });

    failures += test::run("DEL existing and missing", [] {
        StorageEngine storage;
        storage.set("a", "1");
        ASSERT_EQ(static_cast<size_t>(1), storage.del("a"));
        ASSERT_EQ(static_cast<size_t>(0), storage.del("a"));
    });

    failures += test::run("EXISTS and FLUSH", [] {
        StorageEngine storage;
        storage.set("a", "1");
        storage.set("b", "2");
        ASSERT_TRUE(storage.exists("a"));
        ASSERT_EQ(static_cast<size_t>(2), storage.flush());
        ASSERT_FALSE(storage.exists("a"));
        ASSERT_EQ(static_cast<size_t>(0), storage.size());
    });

    failures += test::run("MSET and MGET", [] {
        StorageEngine storage;
        storage.mset({"a", "1", "b", "2"});
        const auto values = storage.mget({"a", "missing", "b"});
        ASSERT_EQ(std::string("1"), values[0].value());
        ASSERT_FALSE(values[1].has_value());
        ASSERT_EQ(std::string("2"), values[2].value());
    });

    failures += test::run("INCR DECR APPEND STRLEN", [] {
        StorageEngine storage;
        std::string error;
        ASSERT_EQ(1LL, storage.increment("counter", 1, error).value());
        ASSERT_EQ(0LL, storage.increment("counter", -1, error).value());
        ASSERT_EQ(static_cast<size_t>(5), storage.append("word", "hello"));
        ASSERT_EQ(static_cast<size_t>(8), storage.append("word", "dbs"));
        ASSERT_EQ(static_cast<size_t>(8), storage.strlen("word").value());
    });

    failures += test::run("LRU max keys evicts least recently used", [] {
        StorageEngine storage(2, 8);
        storage.set("a", "1");
        storage.set("b", "2");
        ASSERT_TRUE(storage.get("a").has_value());
        storage.set("c", "3");
        ASSERT_TRUE(storage.exists("a"));
        ASSERT_FALSE(storage.exists("b"));
        ASSERT_TRUE(storage.exists("c"));
        ASSERT_EQ(static_cast<size_t>(1), storage.evicted_keys());
        ASSERT_EQ(static_cast<size_t>(8), storage.shard_count());
    });

    failures += test::run("snapshot captures values and expirations across shards", [] {
        StorageEngine storage(0, 16);
        storage.set("first", "one");
        storage.set("second", "two");
        storage.expire_at("second", StorageEngine::Clock::now() + std::chrono::seconds(10));
        const auto entries = storage.snapshot_entries();
        ASSERT_EQ(static_cast<size_t>(2), entries.size());
        ASSERT_EQ(std::string("first"), entries[0].key);
        ASSERT_FALSE(entries[0].expires_at.has_value());
        ASSERT_TRUE(entries[1].expires_at.has_value());
    });

    return failures == 0 ? 0 : 1;
}
