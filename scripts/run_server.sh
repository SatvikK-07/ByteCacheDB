#!/usr/bin/env bash
set -euo pipefail

cmake --build build --target bytecachedb --parallel
exec ./build/bytecachedb \
  --host "${BYTECACHEDB_HOST:-127.0.0.1}" \
  --port "${BYTECACHEDB_PORT:-6379}" \
  --threads "${BYTECACHEDB_THREADS:-8}" \
  --enable-aof "${BYTECACHEDB_ENABLE_AOF:-false}" \
  --aof-path "${BYTECACHEDB_AOF_PATH:-data/bytecachedb.aof}"
