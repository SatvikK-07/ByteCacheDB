#!/usr/bin/env bash
set -euo pipefail

python3 benchmarks/benchmark.py \
  --host "${BYTECACHEDB_HOST:-127.0.0.1}" \
  --port "${BYTECACHEDB_PORT:-6379}" \
  --clients "${BYTECACHEDB_BENCH_CLIENTS:-100}" \
  --requests "${BYTECACHEDB_BENCH_REQUESTS:-100000}" \
  --command "${BYTECACHEDB_BENCH_COMMAND:-mixed}" \
  --output benchmarks/results.json
