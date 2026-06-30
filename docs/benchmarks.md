# Benchmarks

The benchmark harness uses ordinary TCP clients and the public line protocol. It never
calls the C++ storage engine directly.

## Standard Matrix

```bash
./build/bytecachedb --port 6379 --threads 8 --shards 64 --enable-aof false
python3 benchmarks/benchmark_matrix.py \
  --port 6379 --requests 100000 --command mixed --pipeline-depth 32
```

Machine: Apple M3 MacBook Air, 8 cores, 16 GB RAM, macOS 26.2 arm64. Compiler:
AppleClang 17.0.0 with CMake Release (`-O3 -DNDEBUG`) plus project warning flags.

Workload: 100,000 commands per row, 70% `GET`, 20% `SET`, 5% `DEL`, 5% `EXPIRE`,
8 workers, 64 shards, AOF disabled.

| Clients | Mode | QPS | p50 ms | p95 ms | p99 ms |
| ---: | --- | ---: | ---: | ---: | ---: |
| 1 | request-response | 41,602 | 0.023 | 0.026 | 0.029 |
| 1 | pipeline 32 | 118,647 | 0.008 | 0.009 | 0.009 |
| 10 | request-response | 45,910 | 0.194 | 0.431 | 0.581 |
| 10 | pipeline 32 | 229,156 | 0.037 | 0.075 | 0.133 |
| 100 | request-response | 47,700 | 1.739 | 4.515 | 6.050 |
| 100 | pipeline 32 | 291,564 | 0.169 | 0.460 | 0.690 |

For non-pipelined runs, every sample is one request/response round trip. For pipelined
runs, the harness divides each batch round trip by its command count and assigns that
amortized latency to successful commands. Percentiles sort samples and select
`round(percentile * (N - 1))`.

## Recovery

```bash
python3 benchmarks/recovery_benchmark.py --binary build/bytecachedb --keys 100000
```

The measured 100,000-record AOF replay took 0.108 seconds, and 1,000/1,000 sampled keys
matched. Raw results are in `benchmarks/recovery_results.json`.

Benchmark numbers are local measurements, not service-level guarantees. CPU scheduling,
power mode, Python runtime, fsync policy, and competing processes affect results.
