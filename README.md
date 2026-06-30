# ByteCacheDB

ByteCacheDB is an in-memory key-value database written in C++17. It exposes a simple text protocol over TCP, supports concurrent clients through a worker thread pool, implements TTL expiration, and can recover state from an append-only file.

The project is designed as a resume-grade systems programming project: networking, synchronization, database command execution, persistence, reliability tests, and benchmark tooling live in separate modules instead of one script.

## Architecture

```text
TCP clients
    |
    v
+------------------+        +-------------------+
| Server acceptor  | -----> | ThreadPool queue  |
+------------------+        +-------------------+
                                  |
                                  v
                         +------------------+
                         | ClientHandler    |
                         | line parser      |
                         | command execute  |
                         +------------------+
                                  |
                 +----------------+----------------+
                 v                                 v
        +----------------+                +------------------+
        | StorageEngine  | <------------> | TTL cleanup      |
        | unordered_map  |                | background loop  |
        | shared_mutex   |                +------------------+
        +----------------+
                 |
                 v
        +----------------+
        | Append-only    |
        | persistence    |
        +----------------+
```

## Features

- TCP server with configurable host, port, worker thread count, AOF path, and max-key limit.
- Multiple concurrent clients using a fixed-size worker thread pool.
- Request pipelining: clients can send multiple newline-delimited commands without waiting between responses.
- Thread-safe in-memory storage using `std::unordered_map` and `std::shared_mutex`.
- TTL support with lazy expiration and a background cleanup thread.
- Append-only file persistence with startup replay.
- INFO metrics for uptime, connected clients, command count, key count, expired keys, memory estimate, workers, and AOF status.
- Unit and concurrency tests through CTest.
- Python benchmark clients for throughput, latency, pipelining, client sweeps, and AOF recovery.

## Supported Commands

| Command | Response | Description |
| --- | --- | --- |
| `PING` | `+PONG` | Health check |
| `SET key value` | `+OK` | Set a string value |
| `GET key` | bulk string or `$-1` | Read a value |
| `DEL key` | integer | Delete a key |
| `EXISTS key` | integer | Return 1 if a key exists |
| `EXPIRE key seconds` | integer | Set TTL in seconds |
| `TTL key` | integer | Remaining TTL, `-1` no TTL, `-2` missing |
| `PERSIST key` | integer | Remove a key's TTL |
| `KEYS` | array | List keys |
| `FLUSH` | `+OK` | Clear the store |
| `INFO` | bulk string | Server metrics |
| `MSET key value ...` | `+OK` | Set multiple pairs |
| `MGET key ...` | array | Read multiple keys |
| `INCR key` | integer | Increment integer value |
| `DECR key` | integer | Decrement integer value |
| `APPEND key value` | integer | Append to string and return length |
| `STRLEN key` | integer | Return string length |

## Build

```bash
mkdir -p build
cd build
cmake ..
cmake --build . --parallel
```

Or use:

```bash
scripts/build.sh
```

## Run

```bash
./build/bytecachedb --host 127.0.0.1 --port 6379 --threads 8 --enable-aof true
```

Useful options:

```text
--host 127.0.0.1
--port 6379
--threads 8
--enable-aof true
--aof-path data/bytecachedb.aof
--max-keys 0
--max-line-bytes 1048576
```

## Example With netcat

```bash
nc 127.0.0.1 6379
PING
SET name Satvik
GET name
EXPIRE name 10
TTL name
INFO
```

Example responses:

```text
+PONG
+OK
$6
Satvik
:1
:10
```

## Tests

```bash
scripts/run_tests.sh
```

The test suite covers command parsing, storage behavior, TTL expiration, AOF replay, and concurrent readers/writers.

## Benchmarks

Start the server first:

```bash
./build/bytecachedb --port 6379 --threads 8 --enable-aof false
```

Then run:

```bash
python3 benchmarks/benchmark.py --clients 100 --requests 100000 --command set
python3 benchmarks/benchmark.py --clients 100 --requests 100000 --command get
python3 benchmarks/benchmark.py --clients 500 --requests 200000 --mixed
python3 benchmarks/benchmark.py --clients 100 --requests 100000 --command mixed --pipelined
```

Results are printed and written to `benchmarks/results.json`.

| Scenario | Clients | Requests | QPS | p50 ms | p95 ms | p99 ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Mixed, pipelined depth 32, AOF off | 100 | 100,000 | 609,592 | 0.011 | 0.029 | 2.899 |

Recovery benchmark:

| Persisted keys | Recovery time | Sample verification |
| ---: | ---: | ---: |
| 100,000 | 0.107 seconds | 1,000 / 1,000 keys |

These measurements were collected on June 30, 2026 from a local macOS arm64 environment using an AppleClang 17 Release build. The pipelined benchmark reports per-command latency as batch round-trip time divided by pipeline depth, so compare it with runs from the same harness rather than unrelated systems.

## Design Decisions

- **Thread pool over one thread per connection:** the acceptor stays small and bounded, while worker count is controlled by `--threads`.
- **Line protocol first:** newline-delimited commands are easy to inspect with `nc`, easy to benchmark, and sufficient for a Redis-like learning project.
- **Shared storage abstraction:** command execution does not directly touch the map outside `StorageEngine`.
- **TTL correctness:** expired keys are removed lazily on access and by a background cleanup loop.
- **AOF over snapshots:** append-only replay is simpler, testable, and good enough for crash recovery. Snapshotting is listed as future work.
- **Honest metrics:** INFO reports runtime counters and approximate memory use without claiming distributed behavior.

## Persistence Model

When AOF is enabled, successful mutating commands are appended to `data/bytecachedb.aof` by default. On startup, ByteCacheDB replays that file before accepting clients.

`EXPIRE` is persisted internally as `EXPIREAT key epoch_millis`, so restart does not reset TTL back to the original relative duration.

The AOF is flushed after each write for simple durability. This favors correctness and clarity over maximum throughput.

## Limitations

- Values are whitespace-delimited tokens, not arbitrary binary strings.
- The networking model uses blocking sockets and a thread pool, not `epoll`, `kqueue`, or `io_uring`.
- AOF compaction is not implemented, so long-running write-heavy workloads can grow the log.
- Snapshot persistence is future work.
- LRU eviction is approximate and based on last access timestamps.

## Future Work

- RESP-compatible parser with quoted or binary-safe values.
- AOF compaction and snapshot files.
- Event-driven networking with `epoll` on Linux and `kqueue` on macOS.
- More granular storage sharding to reduce lock contention.
- Dockerfile and CI workflow.

## Resume Bullets

ByteCacheDB | C++, TCP Networking, Concurrency, Database Internals

- Built an in-memory key-value database supporting 17 commands, concurrent TCP clients, TTL eviction, pipelined requests, approximate LRU eviction, and append-only persistence.
- Measured 610K QPS across 100 concurrent clients with p95 latency of 0.029 ms in a 100,000-request mixed pipelined local benchmark using an 8-thread worker pool.
- Replayed an append-only log containing 100,000 persisted keys in 0.107 seconds and verified 1,000/1,000 sampled keys after restart.
