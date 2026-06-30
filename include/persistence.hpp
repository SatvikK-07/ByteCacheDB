#pragma once

#include <fstream>
#include <mutex>
#include <string>

#include "command_parser.hpp"
#include "storage_engine.hpp"

namespace bytecachedb {

class AppendOnlyFile {
public:
    AppendOnlyFile(bool enabled, std::string path);
    ~AppendOnlyFile();

    AppendOnlyFile(const AppendOnlyFile&) = delete;
    AppendOnlyFile& operator=(const AppendOnlyFile&) = delete;

    bool enabled() const;
    const std::string& path() const;
    bool open(std::string& error);
    bool append(const Command& command, std::string& error);
    bool append_line(const std::string& line, std::string& error);
    bool replay(StorageEngine& storage, std::string& error) const;
    void close();

    static std::string serialize(const Command& command);
    static int64_t to_epoch_millis(StorageEngine::TimePoint time_point);
    static StorageEngine::TimePoint from_epoch_millis(int64_t millis);

private:
    static bool apply_replay_command(StorageEngine& storage,
                                     const Command& command,
                                     std::string& error);

    bool enabled_{false};
    std::string path_;
    mutable std::mutex mutex_;
    std::ofstream stream_;
};

} // namespace bytecachedb
