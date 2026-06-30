#include "server.hpp"

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

namespace {

bytecachedb::Server* g_server = nullptr;

void handle_signal(int) {
    if (g_server != nullptr) {
        g_server->stop();
    }
}

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

void print_usage(const char* program) {
    std::cout << "Usage: " << program
              << " [--host 127.0.0.1] [--port 6379] [--threads 8]"
              << " [--enable-aof true] [--aof-path data/bytecachedb.aof]"
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
        std::string arg = argv[i];
        std::string value;

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }

        const auto equals = arg.find('=');
        const std::string key = equals == std::string::npos ? arg : arg.substr(0, equals);

        if (key == "--host") {
            if (!next_arg(argc, argv, i, value)) {
                std::cerr << "--host requires a value\n";
                return 1;
            }
            config.host = value;
        } else if (key == "--port") {
            if (!next_arg(argc, argv, i, value)) {
                std::cerr << "--port requires a value\n";
                return 1;
            }
            config.port = std::stoi(value);
        } else if (key == "--threads") {
            if (!next_arg(argc, argv, i, value)) {
                std::cerr << "--threads requires a value\n";
                return 1;
            }
            config.worker_threads = std::stoul(value);
        } else if (key == "--enable-aof") {
            if (!next_arg(argc, argv, i, value)) {
                std::cerr << "--enable-aof requires a value\n";
                return 1;
            }
            if (!parse_bool(value, config.enable_aof)) {
                std::cerr << "--enable-aof expects true or false\n";
                return 1;
            }
        } else if (key == "--aof-path") {
            if (!next_arg(argc, argv, i, value)) {
                std::cerr << "--aof-path requires a value\n";
                return 1;
            }
            config.aof_path = value;
        } else if (key == "--max-keys") {
            if (!next_arg(argc, argv, i, value)) {
                std::cerr << "--max-keys requires a value\n";
                return 1;
            }
            config.max_keys = std::stoull(value);
        } else if (key == "--max-line-bytes") {
            if (!next_arg(argc, argv, i, value)) {
                std::cerr << "--max-line-bytes requires a value\n";
                return 1;
            }
            config.max_line_bytes = std::stoull(value);
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (config.worker_threads == 0) {
        std::cerr << "--threads must be at least 1\n";
        return 1;
    }
    if (config.port <= 0 || config.port > 65535) {
        std::cerr << "--port must be between 1 and 65535\n";
        return 1;
    }

    bytecachedb::Server server(config);
    g_server = &server;
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    std::string error;
    if (!server.start(error)) {
        std::cerr << "ByteCacheDB failed: " << error << "\n";
        return 1;
    }

    return 0;
}
