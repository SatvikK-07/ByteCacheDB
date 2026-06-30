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
      storage_(config_.max_keys, config_.storage_shards),
      persistence_(config_.enable_aof,
                   config_.aof_path,
                   config_.fsync_policy,
                   config_.snapshot_path),
      ttl_manager_(storage_, metrics_),
      thread_pool_(std::max<size_t>(1, config_.worker_threads)) {}

Server::~Server() {
    stop();
}

bool Server::start(std::string& error) {
    if (!persistence_.recover(storage_, error)) {
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

    std::cout << "ByteCacheDB listening on " << config_.host << ":" << bound_port_.load()
              << " with " << config_.worker_threads << " command workers and "
              << config_.storage_shards << " storage shards"
              << (config_.enable_aof ? " (AOF " + persistence_.fsync_policy_name() + ")"
                                     : " (AOF disabled)")
              << std::endl;

    while (running_.load()) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        const int client_fd = ::accept(listen_fd_.load(),
                                       reinterpret_cast<sockaddr*>(&client_addr),
                                       &client_len);

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

        std::lock_guard clients_lock(clients_mutex_);
        if (!running_.load()) {
            ::close(client_fd);
            break;
        }
        metrics_.client_connected();
        client_fds_.insert(client_fd);
        connection_threads_.emplace_back([this, client_fd] {
            ClientHandler handler(storage_,
                                  parser_,
                                  persistence_,
                                  metrics_,
                                  thread_pool_,
                                  write_path_mutex_,
                                  config_.worker_threads,
                                  config_.enable_aof,
                                  config_.max_line_bytes);
            handler.handle(client_fd);
            ::close(client_fd);
            {
                std::lock_guard lock(clients_mutex_);
                client_fds_.erase(client_fd);
            }
            metrics_.client_disconnected();
        });
    }

    stop();
    return true;
}

void Server::stop() {
    std::lock_guard stop_lock(stop_mutex_);
    const bool was_running = running_.exchange(false);
    close_listen_socket();
    shutdown_clients();
    ttl_manager_.stop();
    join_connection_threads();
    thread_pool_.shutdown();
    persistence_.close();
    if (was_running) {
        std::cout << "ByteCacheDB stopped" << std::endl;
    }
}

int Server::listen_fd() const {
    return listen_fd_.load();
}

int Server::port() const {
    return bound_port_.load();
}

bool Server::setup_socket(std::string& error) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        error = "socket failed: " + std::string(std::strerror(errno));
        return false;
    }
    listen_fd_.store(fd);

    int reuse = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
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

    if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        error = "bind failed: " + std::string(std::strerror(errno));
        close_listen_socket();
        return false;
    }
    if (::listen(fd, SOMAXCONN) < 0) {
        error = "listen failed: " + std::string(std::strerror(errno));
        close_listen_socket();
        return false;
    }

    sockaddr_in bound_address{};
    socklen_t bound_length = sizeof(bound_address);
    if (::getsockname(fd,
                      reinterpret_cast<sockaddr*>(&bound_address),
                      &bound_length) < 0) {
        error = "getsockname failed: " + std::string(std::strerror(errno));
        close_listen_socket();
        return false;
    }
    bound_port_.store(ntohs(bound_address.sin_port));
    return true;
}

void Server::close_listen_socket() {
    const int fd = listen_fd_.exchange(-1);
    if (fd >= 0) {
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
    }
}

void Server::shutdown_clients() {
    std::lock_guard lock(clients_mutex_);
    for (const int fd : client_fds_) {
        ::shutdown(fd, SHUT_RDWR);
    }
}

void Server::join_connection_threads() {
    std::vector<std::thread> threads;
    {
        std::lock_guard lock(clients_mutex_);
        threads.swap(connection_threads_);
    }
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

} // namespace bytecachedb
