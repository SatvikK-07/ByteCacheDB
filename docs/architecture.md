# Architecture

ByteCacheDB is organized around small components with clear ownership.

## Server Accept Loop

`Server` owns the listening socket, storage engine, metrics, persistence layer, TTL manager, and thread pool. The main thread accepts TCP connections and submits accepted client sockets to `ThreadPool`.

The accept loop is intentionally small:

1. Create and bind the listening socket.
2. Replay AOF if enabled.
3. Start the TTL cleanup thread.
4. Accept client sockets.
5. Enqueue each socket for worker processing.

## Client Lifecycle

`ClientHandler` owns the request loop for one connected client. It reads from a blocking socket, buffers data, splits complete lines, parses commands, executes them, and writes responses back in order.

The handler enforces a maximum line length to avoid unbounded memory growth from malformed clients.

## Command Parsing

`CommandParser` converts a raw line into:

```cpp
struct Command {
    CommandType type;
    std::vector<std::string> args;
    std::string raw;
};
```

It uppercases command names, tolerates extra spaces, rejects empty input, validates argument counts, and returns parse errors instead of throwing.

## Storage Engine

`StorageEngine` owns the in-memory map:

```cpp
std::unordered_map<std::string, ValueEntry>
```

Each entry contains:

- value string
- optional expiration timestamp
- creation timestamp
- last accessed timestamp

Expired keys are removed lazily when accessed and by the background TTL manager.

## TTL Management

`TtlManager` wakes once per second and calls `StorageEngine::cleanup_expired()`. It updates metrics with the number of removed keys.

This is simple and predictable. For very large keyspaces, a future improvement would sample keys or maintain an expiration index.

## AOF Persistence

`AppendOnlyFile` appends successful mutating commands when AOF is enabled. Startup replay rebuilds the in-memory state.

`EXPIRE` is logged as `EXPIREAT key epoch_millis` to avoid extending TTL after restart.

## Metrics

`Metrics` stores atomic counters for:

- connected clients
- total commands processed
- expired keys removed
- server uptime

`INFO` combines those counters with live storage metrics.
