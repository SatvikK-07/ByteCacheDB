#!/usr/bin/env bash
set -euo pipefail

python3 benchmarks/benchmark_matrix.py \
  --host "${BYTECACHEDB_HOST:-127.0.0.1}" \
  --port "${BYTECACHEDB_PORT:-6379}" \
  --requests "${BYTECACHEDB_BENCH_REQUESTS:-100000}" \
  --command "${BYTECACHEDB_BENCH_COMMAND:-mixed}" \
  --pipeline-depth "${BYTECACHEDB_PIPELINE_DEPTH:-32}" \
  --server-workers "${BYTECACHEDB_THREADS:-8}" \
  --server-shards "${BYTECACHEDB_SHARDS:-64}" \
  --aof-mode "${BYTECACHEDB_ENABLE_AOF:-false}" \
  --output benchmarks/matrix_results.json
