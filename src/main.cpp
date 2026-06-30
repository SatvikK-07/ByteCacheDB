#include "server.hpp"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <pthread.h>
#include <string>
#include <thread>

namespace {

bool parse_bool(const std::string& value, bool& result) {
    if (value == "true" || value == "1" || value == "yes" || value == "on") {
        result = true;
        return true;
    }
    if (value == "false" || value == "0" || value == "no" || value == "off") {
        result = false;
        return true;
    }
    return false;
}

bool parse_fsync_policy(const std::string& value, bytecachedb::FsyncPolicy& result) {
    if (value == "always") {
        result = bytecachedb::FsyncPolicy::Always;
        return true;
    }
    if (value == "everysec") {
        result = bytecachedb::FsyncPolicy::EverySec;
        return true;
    }
    if (value == "never") {
        result = bytecachedb::FsyncPolicy::Never;
        return true;
    }
    return false;
}

void print_usage(const char* program) {
    std::cout << "Usage: " << program
              << " [--host 127.0.0.1] [--port 6379] [--threads 8]"
              << " [--shards 64] [--enable-aof true]"
              << " [--aof-path data/bytecachedb.aof]"
              << " [--snapshot-path data/bytecachedb.snapshot]"
              << " [--fsync always|everysec|never]"
              << " [--max-keys 0] [--max-line-bytes 1048576]\n";
}

bool next_arg(int argc, char** argv, int& i, std::string& value) {
    const std::string current = argv[i];
    const auto equals = current.find('=');
    if (equals != std::string::npos) {
        value = current.substr(equals + 1);
        return true;
    }
    if (i + 1 >= argc) {
        return false;
    }
    value = argv[++i];
    return true;
}

} // namespace

int main(int argc, char** argv) {
    bytecachedb::ServerConfig config;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        std::string value;

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }

        const auto equals = arg.find('=');
        const std::string key = equals == std::string::npos ? arg : arg.substr(0, equals);

        if (!next_arg(argc, argv, i, value)) {
            std::cerr << key << " requires a value\n";
            return 1;
        }
        if (key == "--host") {
            config.host = value;
        } else if (key == "--port") {
            config.port = std::stoi(value);
        } else if (key == "--threads") {
            config.worker_threads = std::stoul(value);
        } else if (key == "--shards") {
            config.storage_shards = std::stoul(value);
        } else if (key == "--enable-aof") {
            if (!parse_bool(value, config.enable_aof)) {
                std::cerr << "--enable-aof expects true or false\n";
                return 1;
            }
        } else if (key == "--aof-path") {
            config.aof_path = value;
        } else if (key == "--snapshot-path") {
            config.snapshot_path = value;
        } else if (key == "--fsync") {
            if (!parse_fsync_policy(value, config.fsync_policy)) {
                std::cerr << "--fsync expects always, everysec, or never\n";
                return 1;
            }
        } else if (key == "--max-keys") {
            config.max_keys = std::stoull(value);
        } else if (key == "--max-line-bytes") {
            config.max_line_bytes = std::stoull(value);
        } else {
            std::cerr << "Unknown option: " << key << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (config.worker_threads == 0 || config.storage_shards == 0) {
        std::cerr << "--threads and --shards must be at least 1\n";
        return 1;
    }
    if (config.port <= 0 || config.port > 65535) {
        std::cerr << "--port must be between 1 and 65535\n";
        return 1;
    }

    sigset_t signal_set;
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGINT);
    sigaddset(&signal_set, SIGTERM);
    if (pthread_sigmask(SIG_BLOCK, &signal_set, nullptr) != 0) {
        std::cerr << "failed to configure signal handling\n";
        return 1;
    }
    std::signal(SIGPIPE, SIG_IGN);

    bytecachedb::Server server(config);
    std::atomic<bool> signal_received{false};
    std::thread signal_thread([&] {
        int received = 0;
        if (sigwait(&signal_set, &received) == 0) {
            signal_received.store(true);
            server.stop();
        }
    });

    std::string error;
    const bool started = server.start(error);
    if (!signal_received.load()) {
        pthread_kill(signal_thread.native_handle(), SIGTERM);
    }
    signal_thread.join();

    if (!started) {
        std::cerr << "ByteCacheDB failed: " << error << "\n";
        return 1;
    }
    return 0;
}
