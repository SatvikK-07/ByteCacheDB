# Concurrency

ByteCacheDB uses a bounded worker thread pool. The server accept loop does not process commands itself; it accepts sockets and hands each connection to the queue.

## Why a Thread Pool

A thread pool keeps concurrency explicit and configurable through `--threads`. It avoids unbounded thread creation under many clients while still allowing multiple clients to make progress at the same time.

## Task Queue

`ThreadPool` uses:

- `std::mutex`
- `std::condition_variable`
- `std::queue<std::function<void()>>`
- `std::thread`

Workers sleep when the queue is empty and wake when new client sockets are enqueued.

## Storage Synchronization

`StorageEngine` protects the map with `std::shared_mutex`.

Current operations often take a unique lock because reads may update `last_accessed` and may lazily remove expired keys. The shared mutex still leaves room for future read-only fast paths and keeps synchronization explicit at the storage boundary.

## Race Conditions Avoided

- A key cannot be erased while another storage method is reading it because map access is lock-protected.
- TTL cleanup and client command execution use the same storage lock.
- AOF writes are protected by an internal mutex so commands from different clients do not interleave in the log.
- Metrics use atomics for counters updated by multiple threads.

## Known Bottlenecks

- One global storage lock limits write-heavy scaling.
- Blocking sockets tie one worker to one connected client.
- AOF flushes every mutating command, which improves durability but can reduce throughput.

## Future Improvements

- Shard the keyspace by hash to reduce lock contention.
- Use `epoll` or `kqueue` for many mostly idle connections.
- Add AOF batching or configurable fsync policy.
- Maintain a separate expiration heap or timing wheel for faster TTL cleanup.
