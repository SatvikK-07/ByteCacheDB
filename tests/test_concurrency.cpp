#include "storage_engine.hpp"
#include "test_framework.hpp"

#include <string>
#include <thread>
#include <vector>

using namespace bytecachedb;

int main() {
    int failures = 0;

    failures += test::run("multiple threads setting keys", [] {
        StorageEngine storage;
        constexpr int thread_count = 8;
        constexpr int keys_per_thread = 500;
        std::vector<std::thread> workers;

        for (int t = 0; t < thread_count; ++t) {
            workers.emplace_back([&, t] {
                for (int i = 0; i < keys_per_thread; ++i) {
                    storage.set("k" + std::to_string(t) + ":" + std::to_string(i),
                                std::to_string(i));
                }
            });
        }
        for (auto& worker : workers) {
            worker.join();
        }

        ASSERT_EQ(static_cast<size_t>(thread_count * keys_per_thread), storage.size());
    });

    failures += test::run("mixed reads and writes", [] {
        StorageEngine storage;
        storage.set("hot", "value");
        std::vector<std::thread> workers;

        for (int t = 0; t < 4; ++t) {
            workers.emplace_back([&] {
                for (int i = 0; i < 1000; ++i) {
                    ASSERT_EQ(std::string("value"), storage.get("hot").value());
                }
            });
        }
        for (int t = 0; t < 4; ++t) {
            workers.emplace_back([&, t] {
                for (int i = 0; i < 1000; ++i) {
                    storage.set("writer:" + std::to_string(t) + ":" + std::to_string(i), "x");
                }
            });
        }
        for (auto& worker : workers) {
            worker.join();
        }

        ASSERT_TRUE(storage.exists("hot"));
    });

    failures += test::run("concurrent increments are serialized", [] {
        StorageEngine storage;
        std::vector<std::thread> workers;
        constexpr int thread_count = 8;
        constexpr int increments_per_thread = 1000;

        for (int t = 0; t < thread_count; ++t) {
            workers.emplace_back([&] {
                for (int i = 0; i < increments_per_thread; ++i) {
                    std::string error;
                    storage.increment("counter", 1, error);
                }
            });
        }
        for (auto& worker : workers) {
            worker.join();
        }

        ASSERT_EQ(std::to_string(thread_count * increments_per_thread),
                  storage.get("counter").value());
    });

    return failures == 0 ? 0 : 1;
}
