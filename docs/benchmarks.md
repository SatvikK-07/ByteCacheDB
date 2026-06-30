# Benchmarks

The benchmark scripts are written in Python and use real TCP connections. They do not bypass the server or call C++ code directly.

## Scripts

| Script | Purpose |
| --- | --- |
| `benchmark.py` | Main throughput and latency benchmark |
| `concurrent_clients.py` | Sweep multiple client counts |
| `latency_benchmark.py` | Compare latency across client counts |
| `recovery_benchmark.py` | Insert keys, restart server, measure AOF recovery |

## Example Commands

```bash
./build/bytecachedb --port 6379 --threads 8 --enable-aof false
python3 benchmarks/benchmark.py --clients 100 --requests 100000 --command set
python3 benchmarks/benchmark.py --clients 100 --requests 100000 --command get
python3 benchmarks/benchmark.py --clients 100 --requests 100000 --command mixed --pipelined
```

Recovery:

```bash
python3 benchmarks/recovery_benchmark.py --binary build/bytecachedb --keys 100000
```

## Metrics Collected

- total requests
- total elapsed time
- QPS
- average latency
- p50 latency
- p95 latency
- p99 latency
- success rate
- recovery time for AOF replay

## Results

Measured on June 30, 2026 in a local macOS arm64 environment with AppleClang 17.0 and a Release build.

| Date | Machine | Compiler | Scenario | Clients | Requests | QPS | p95 ms |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: |
| 2026-06-30 | macOS arm64 | AppleClang 17.0 | Mixed, pipeline 32, AOF off | 100 | 100,000 | 609,592 | 0.029 |

Recovery result:

| Persisted keys | Startup recovery | Keys sampled | Keys recovered |
| ---: | ---: | ---: | ---: |
| 100,000 | 0.107 seconds | 1,000 | 1,000 |

The pipelined client records per-command latency as batch round-trip time divided by batch size. This is useful for comparing ByteCacheDB configurations with the same harness, but it should not be compared directly with non-pipelined latency reports from other tools.

## Interpretation Guide

- Compare AOF disabled and enabled separately. AOF flushes every write, so write-heavy throughput should be lower when enabled.
- Compare pipelined and non-pipelined workloads. Pipelining should improve throughput by reducing request/response round trips.
- Mixed workloads expose lock contention more honestly than pure GET or pure SET.
- Recovery time depends on AOF size and disk speed.
