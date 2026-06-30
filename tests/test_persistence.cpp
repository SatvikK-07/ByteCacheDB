#include "persistence.hpp"
#include "test_framework.hpp"

#include <filesystem>
#include <string>
#include <unistd.h>

using namespace bytecachedb;

namespace {

Command command(CommandType type, std::initializer_list<std::string> args) {
    Command result;
    result.type = type;
    result.args = args;
    return result;
}

std::string test_path(const std::string& name) {
    auto path = std::filesystem::temp_directory_path() /
                ("bytecachedb_" + name + "_" + std::to_string(::getpid()) + ".aof");
    std::filesystem::remove(path);
    return path.string();
}

} // namespace

int main() {
    int failures = 0;

    failures += test::run("AOF writes and replays commands", [] {
        const auto path = test_path("basic");
        std::string error;
        {
            AppendOnlyFile aof(true, path);
            ASSERT_TRUE(aof.open(error));
            ASSERT_TRUE(aof.append(command(CommandType::SET, {"alpha", "1"}), error));
            ASSERT_TRUE(aof.append(command(CommandType::SET, {"beta", "2"}), error));
            ASSERT_TRUE(aof.append(command(CommandType::DEL, {"beta"}), error));
        }

        StorageEngine storage;
        AppendOnlyFile replay(true, path);
        ASSERT_TRUE(replay.replay(storage, error));
        ASSERT_EQ(std::string("1"), storage.get("alpha").value());
        ASSERT_FALSE(storage.get("beta").has_value());
        std::filesystem::remove(path);
    });

    failures += test::run("AOF replays persisted expiration", [] {
        const auto path = test_path("expire");
        std::string error;
        const auto expires_at = StorageEngine::Clock::now() + std::chrono::seconds(20);
        {
            AppendOnlyFile aof(true, path);
            ASSERT_TRUE(aof.open(error));
            ASSERT_TRUE(aof.append(command(CommandType::SET, {"token", "abc"}), error));
            ASSERT_TRUE(aof.append(command(CommandType::EXPIREAT,
                                          {"token",
                                           std::to_string(AppendOnlyFile::to_epoch_millis(
                                               expires_at))}),
                                   error));
        }

        StorageEngine storage;
        AppendOnlyFile replay(true, path);
        ASSERT_TRUE(replay.replay(storage, error));
        ASSERT_TRUE(storage.ttl("token") > 0);
        std::filesystem::remove(path);
    });

    failures += test::run("FLUSH remains durable after replay", [] {
        const auto path = test_path("flush");
        std::string error;
        {
            AppendOnlyFile aof(true, path);
            ASSERT_TRUE(aof.open(error));
            ASSERT_TRUE(aof.append(command(CommandType::SET, {"a", "1"}), error));
            ASSERT_TRUE(aof.append(command(CommandType::FLUSH, {}), error));
        }

        StorageEngine storage;
        AppendOnlyFile replay(true, path);
        ASSERT_TRUE(replay.replay(storage, error));
        ASSERT_EQ(static_cast<size_t>(0), storage.size());
        std::filesystem::remove(path);
    });

    failures += test::run("AOF round trips quoted values", [] {
        const auto path = test_path("quoted");
        std::string error;
        {
            AppendOnlyFile aof(true, path, FsyncPolicy::Always);
            ASSERT_TRUE(aof.open(error));
            ASSERT_TRUE(aof.append(
                command(CommandType::SET, {"message", "spaces, \"quotes\", and \\slashes"}),
                error));
        }
        StorageEngine storage;
        AppendOnlyFile replay(true, path);
        ASSERT_TRUE(replay.replay(storage, error));
        ASSERT_EQ(std::string("spaces, \"quotes\", and \\slashes"),
                  storage.get("message").value());
        std::filesystem::remove(path);
    });

    failures += test::run("snapshot checkpoint skips older AOF records", [] {
        const auto path = test_path("snapshot_aof");
        const auto snapshot = path + ".snapshot";
        std::string error;
        {
            StorageEngine storage;
            AppendOnlyFile aof(true, path, FsyncPolicy::Never, snapshot);
            ASSERT_TRUE(aof.open(error));
            storage.set("counter", "1");
            ASSERT_TRUE(aof.append(command(CommandType::SET, {"counter", "1"}), error));
            storage.increment("counter", 1, error);
            ASSERT_TRUE(aof.append(command(CommandType::INCR, {"counter"}), error));
            ASSERT_TRUE(aof.save_snapshot(storage, error));
            storage.increment("counter", 1, error);
            ASSERT_TRUE(aof.append(command(CommandType::INCR, {"counter"}), error));
        }

        StorageEngine recovered;
        AppendOnlyFile replay(true, path, FsyncPolicy::Never, snapshot);
        ASSERT_TRUE(replay.recover(recovered, error));
        ASSERT_EQ(std::string("3"), recovered.get("counter").value());
        std::filesystem::remove(path);
        std::filesystem::remove(snapshot);
    });

    return failures == 0 ? 0 : 1;
}
