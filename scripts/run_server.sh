#!/usr/bin/env bash
set -euo pipefail

cmake --build build --target bytecachedb --parallel
exec ./build/bytecachedb \
  --host "${BYTECACHEDB_HOST:-127.0.0.1}" \
  --port "${BYTECACHEDB_PORT:-6379}" \
  --threads "${BYTECACHEDB_THREADS:-8}" \
  --shards "${BYTECACHEDB_SHARDS:-64}" \
  --enable-aof "${BYTECACHEDB_ENABLE_AOF:-false}" \
  --aof-path "${BYTECACHEDB_AOF_PATH:-data/bytecachedb.aof}" \
  --snapshot-path "${BYTECACHEDB_SNAPSHOT_PATH:-data/bytecachedb.snapshot}" \
  --fsync "${BYTECACHEDB_FSYNC:-everysec}" \
  --max-keys "${BYTECACHEDB_MAX_KEYS:-0}"
