# Concurrency Model

## Socket And Worker Ownership

- The acceptor only accepts and registers sockets.
- One reader thread per connection performs blocking reads and ordered writes.
- A bounded `ThreadPool` executes commands; idle clients do not consume these workers.
- Shutdown interrupts active sockets before joining reader threads, so idle connections
  cannot prevent termination.

## Shard Locking

The default 64 storage shards independently protect their maps. Single-key commands touch
one lock, allowing unrelated keys to progress concurrently. Multi-shard operations acquire
locks in ascending shard index to avoid lock-order cycles.

Reads such as `GET` take a unique shard lock because they update approximate-LRU access
time and may lazily delete an expired key. Snapshot and memory-estimation scans use shared
locks.

## Ordered AOF Writes

An AOF file mutex alone prevents byte interleaving but does not preserve mutation order.
ByteCacheDB therefore uses a higher-level write-path mutex:

1. Acquire the write-path mutex.
2. Execute the storage mutation.
3. Assign an AOF sequence and append the record.
4. Apply the configured fsync policy.
5. Release the mutex and return the response.

The gate is only required when AOF is enabled. With AOF disabled, independent shard writes
can execute concurrently.

## Atomicity Boundaries

- Single-key methods are atomic at their shard.
- `MSET` is atomic across its affected shards.
- `MGET` reads keys sequentially and is not a transactionally consistent snapshot.
- `KEYS` and snapshots obtain a cross-shard view.
- There are no multi-command transactions or compare-and-swap operations.
