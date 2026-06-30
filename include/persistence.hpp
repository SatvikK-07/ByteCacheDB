#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "command_parser.hpp"
#include "storage_engine.hpp"

namespace bytecachedb {

enum class FsyncPolicy {
    Always,
    EverySec,
    Never
};

class AppendOnlyFile {
public:
    AppendOnlyFile(bool enabled,
                   std::string path,
                   FsyncPolicy fsync_policy = FsyncPolicy::EverySec,
                   std::string snapshot_path = {});
    ~AppendOnlyFile();

    AppendOnlyFile(const AppendOnlyFile&) = delete;
    AppendOnlyFile& operator=(const AppendOnlyFile&) = delete;

    bool enabled() const;
    const std::string& path() const;
    FsyncPolicy fsync_policy() const;
    std::string fsync_policy_name() const;
    bool open(std::string& error);
    bool append(const Command& command, std::string& error);
    bool append_line(const std::string& line, std::string& error);
    bool recover(StorageEngine& storage, std::string& error);
    bool replay(StorageEngine& storage, std::string& error) const;
    bool save_snapshot(const StorageEngine& storage, std::string& error);
    void close();

    static std::string serialize(const Command& command);
    static int64_t to_epoch_millis(StorageEngine::TimePoint time_point);
    static StorageEngine::TimePoint from_epoch_millis(int64_t millis);

private:
    bool load_snapshot(StorageEngine& storage, uint64_t& sequence, std::string& error) const;
    bool replay_after(StorageEngine& storage, uint64_t minimum_sequence, std::string& error) const;
    bool write_record_locked(const std::string& line, std::string& error);
    bool sync_locked(std::string& error);
    void sync_loop();
    static bool apply_replay_command(StorageEngine& storage,
                                     const Command& command,
                                     std::string& error);

    bool enabled_{false};
    std::string path_;
    FsyncPolicy fsync_policy_{FsyncPolicy::EverySec};
    std::string snapshot_path_;
    mutable std::mutex mutex_;
    std::condition_variable sync_condition_;
    std::thread sync_thread_;
    int fd_{-1};
    bool stopping_{false};
    bool dirty_{false};
    mutable std::atomic<uint64_t> sequence_{0};
};

} // namespace bytecachedb
