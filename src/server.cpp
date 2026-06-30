#include "server.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <utility>
#include <unistd.h>

#include <arpa/inet.h>

namespace bytecachedb {

Server::Server(ServerConfig config)
    : config_(std::move(config)),
      storage_(config_.max_keys),
      persistence_(config_.enable_aof, config_.aof_path),
      ttl_manager_(storage_, metrics_),
      thread_pool_(std::max<size_t>(1, config_.worker_threads)) {}

Server::~Server() {
    stop();
}

bool Server::start(std::string& error) {
    if (!persistence_.replay(storage_, error)) {
        return false;
    }
    if (!persistence_.open(error)) {
        return false;
    }
    if (!setup_socket(error)) {
        return false;
    }

    running_.store(true);
    ttl_manager_.start();

    std::cout << "ByteCacheDB listening on " << config_.host << ":" << config_.port
              << " with " << config_.worker_threads << " worker threads"
              << (config_.enable_aof ? " (AOF enabled)" : " (AOF disabled)") << std::endl;

    while (running_.load()) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        const int client_fd =
            ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);

        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (!running_.load() || errno == EBADF || errno == EINVAL) {
                break;
            }
            error = "accept failed: " + std::string(std::strerror(errno));
            stop();
            return false;
        }

        metrics_.client_connected();
        const bool queued = thread_pool_.enqueue([this, client_fd] {
            ClientHandler handler(storage_,
                                  parser_,
                                  persistence_,
                                  metrics_,
                                  config_.worker_threads,
                                  config_.enable_aof,
                                  config_.max_line_bytes);
            handler.handle(client_fd);
            ::close(client_fd);
            metrics_.client_disconnected();
        });

        if (!queued) {
            ::close(client_fd);
            metrics_.client_disconnected();
        }
    }

    stop();
    return true;
}

void Server::stop() {
    const bool was_running = running_.exchange(false);
    close_listen_socket();
    ttl_manager_.stop();
    thread_pool_.shutdown();
    persistence_.close();
    if (was_running) {
        std::cout << "ByteCacheDB stopped" << std::endl;
    }
}

int Server::listen_fd() const {
    return listen_fd_;
}

bool Server::setup_socket(std::string& error) {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        error = "socket failed: " + std::string(std::strerror(errno));
        return false;
    }

    int reuse = 1;
    if (::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        error = "setsockopt failed: " + std::string(std::strerror(errno));
        close_listen_socket();
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(config_.port));
    if (::inet_pton(AF_INET, config_.host.c_str(), &address.sin_addr) != 1) {
        error = "invalid IPv4 host: " + config_.host;
        close_listen_socket();
        return false;
    }

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        error = "bind failed: " + std::string(std::strerror(errno));
        close_listen_socket();
        return false;
    }

    if (::listen(listen_fd_, SOMAXCONN) < 0) {
        error = "listen failed: " + std::string(std::strerror(errno));
        close_listen_socket();
        return false;
    }

    return true;
}

void Server::close_listen_socket() {
    const int fd = listen_fd_;
    if (fd >= 0) {
        listen_fd_ = -1;
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
    }
}

} // namespace bytecachedb
