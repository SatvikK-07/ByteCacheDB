#include "persistence.hpp"
#include "server.hpp"
#include "test_framework.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <filesystem>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace bytecachedb;

namespace {

class RunningServer {
public:
    explicit RunningServer(ServerConfig config) : server_(std::move(config)) {
        thread_ = std::thread([this] { result_ = server_.start(error_); });
        for (int i = 0; i < 200 && server_.port() == 0; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (server_.port() == 0) {
            server_.stop();
            thread_.join();
            throw std::runtime_error("server did not bind: " + error_);
        }
    }

    ~RunningServer() {
        server_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    int port() const {
        return server_.port();
    }

private:
    Server server_;
    std::thread thread_;
    bool result_{false};
    std::string error_;
};

int connect_to(int port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throw std::runtime_error("socket failed");
    }
    timeval timeout{2, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        ::close(fd);
        throw std::runtime_error("connect failed");
    }
    return fd;
}

void send_all(int fd, const std::string& data) {
    size_t offset = 0;
    while (offset < data.size()) {
        const auto sent = ::send(fd, data.data() + offset, data.size() - offset, 0);
        if (sent <= 0) {
            throw std::runtime_error("send failed");
        }
        offset += static_cast<size_t>(sent);
    }
}

std::string receive_exact(int fd, size_t size) {
    std::string response;
    response.resize(size);
    size_t offset = 0;
    while (offset < size) {
        const auto received = ::recv(fd, response.data() + offset, size - offset, 0);
        if (received <= 0) {
            throw std::runtime_error("receive failed");
        }
        offset += static_cast<size_t>(received);
    }
    return response;
}

void expect(int fd, const std::string& command, const std::string& response) {
    send_all(fd, command);
    ASSERT_EQ(response, receive_exact(fd, response.size()));
}

std::string receive_bulk(int fd) {
    std::string header;
    char c = 0;
    while (header.size() < 128) {
        if (::recv(fd, &c, 1, 0) != 1) {
            throw std::runtime_error("bulk header receive failed");
        }
        header.push_back(c);
        if (header.size() >= 2 && header.substr(header.size() - 2) == "\r\n") {
            break;
        }
    }
    const size_t length = std::stoul(header.substr(1, header.size() - 3));
    const auto payload = receive_exact(fd, length + 2);
    return payload.substr(0, length);
}

std::string temporary_path(const std::string& suffix) {
    const auto path = std::filesystem::temp_directory_path() /
                      ("bytecachedb_tcp_" + std::to_string(::getpid()) + suffix);
    std::filesystem::remove(path);
    return path.string();
}

ServerConfig test_config() {
    ServerConfig config;
    config.port = 0;
    config.worker_threads = 4;
    config.storage_shards = 16;
    config.snapshot_path = temporary_path(".snapshot");
    return config;
}

} // namespace

int main() {
    int failures = 0;

    failures += test::run("TCP commands, pipelining, expiry, and errors", [] {
        const auto config = test_config();
        const auto snapshot_path = config.snapshot_path;
        {
            RunningServer server(config);
            const int fd = connect_to(server.port());

            expect(fd, "SET alpha one\n", "+OK\r\n");
            expect(fd, "GET alpha\n", "$3\r\none\r\n");
            expect(fd, "DEL alpha\n", ":1\r\n");
            expect(fd, "GET alpha\n", "$-1\r\n");
            expect(fd, "SET phrase \"hello systems world\"\n", "+OK\r\n");
            expect(fd, "GET phrase\n", "$19\r\nhello systems world\r\n");
            expect(fd, "SAVE\n", "+OK\r\n");

            expect(fd, "SET temporary value\n", "+OK\r\n");
            expect(fd, "EXPIRE temporary 1\n", ":1\r\n");
            std::this_thread::sleep_for(std::chrono::milliseconds(1100));
            expect(fd, "GET temporary\n", "$-1\r\n");

            expect(fd,
                   "PING\nSET piped yes\nGET piped\n",
                   "+PONG\r\n+OK\r\n$3\r\nyes\r\n");
            expect(fd, "NOT_A_COMMAND\n", "-ERR unknown command\r\n");
            ::close(fd);
        }

        StorageEngine recovered;
        std::string error;
        AppendOnlyFile snapshot_only(false, "", FsyncPolicy::Never, snapshot_path);
        ASSERT_TRUE(snapshot_only.recover(recovered, error));
        ASSERT_EQ(std::string("hello systems world"), recovered.get("phrase").value());
        std::filesystem::remove(snapshot_path);
    });

    failures += test::run("concurrent TCP writes replay in mutation order", [] {
        auto config = test_config();
        config.enable_aof = true;
        config.fsync_policy = FsyncPolicy::Never;
        config.aof_path = temporary_path(".aof");
        const auto aof_path = config.aof_path;
        const auto snapshot_path = config.snapshot_path;
        std::string live_value;

        {
            RunningServer server(config);
            std::vector<std::thread> clients;
            for (int client = 0; client < 8; ++client) {
                clients.emplace_back([&, client] {
                    const int fd = connect_to(server.port());
                    for (int command = 0; command < 100; ++command) {
                        const auto value =
                            std::to_string(client) + "-" + std::to_string(command);
                        expect(fd, "SET ordered " + value + "\n", "+OK\r\n");
                    }
                    ::close(fd);
                });
            }
            for (auto& client : clients) {
                client.join();
            }

            const int fd = connect_to(server.port());
            send_all(fd, "GET ordered\n");
            live_value = receive_bulk(fd);
            ::close(fd);
        }

        StorageEngine recovered;
        std::string error;
        AppendOnlyFile replay(true, aof_path, FsyncPolicy::Never, snapshot_path);
        ASSERT_TRUE(replay.recover(recovered, error));
        ASSERT_EQ(live_value, recovered.get("ordered").value());
        std::filesystem::remove(aof_path);
        std::filesystem::remove(snapshot_path);
    });

    failures += test::run("AOF records read-influenced LRU evictions", [] {
        auto config = test_config();
        config.enable_aof = true;
        config.fsync_policy = FsyncPolicy::Never;
        config.max_keys = 2;
        config.aof_path = temporary_path(".eviction.aof");
        const auto aof_path = config.aof_path;
        const auto snapshot_path = config.snapshot_path;

        {
            RunningServer server(config);
            const int fd = connect_to(server.port());
            expect(fd, "SET a one\n", "+OK\r\n");
            expect(fd, "SET b two\n", "+OK\r\n");
            expect(fd, "GET a\n", "$3\r\none\r\n");
            expect(fd, "SET c three\n", "+OK\r\n");
            expect(fd, "GET b\n", "$-1\r\n");
            ::close(fd);
        }

        StorageEngine recovered(2, 16);
        std::string error;
        AppendOnlyFile replay(true, aof_path, FsyncPolicy::Never, snapshot_path);
        ASSERT_TRUE(replay.recover(recovered, error));
        ASSERT_EQ(std::string("one"), recovered.get("a").value());
        ASSERT_FALSE(recovered.get("b").has_value());
        ASSERT_EQ(std::string("three"), recovered.get("c").value());
        std::filesystem::remove(aof_path);
        std::filesystem::remove(snapshot_path);
    });

    return failures == 0 ? 0 : 1;
}
