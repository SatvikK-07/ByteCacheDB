#!/usr/bin/env bash
set -euo pipefail

scripts/build.sh

port="${BYTECACHEDB_DEMO_PORT:-6389}"
./build/bytecachedb \
  --host 127.0.0.1 \
  --port "$port" \
  --threads 8 \
  --shards 64 \
  --enable-aof false \
  --snapshot-path "/tmp/bytecachedb-demo-$$.snapshot" &
server_pid=$!

cleanup() {
  kill -TERM "$server_pid" 2>/dev/null || true
  wait "$server_pid" 2>/dev/null || true
  rm -f "/tmp/bytecachedb-demo-$$.snapshot"
}
trap cleanup EXIT
sleep 0.3

BYTECACHEDB_DEMO_PORT="$port" python3 - <<'PY'
import os
import socket

port = int(os.environ["BYTECACHEDB_DEMO_PORT"])
commands = 'PING\nSET greeting "hello systems world"\nGET greeting\nINCR visits\nINFO\n'
with socket.create_connection(("127.0.0.1", port)) as sock:
    sock.sendall(commands.encode())
    sock.shutdown(socket.SHUT_WR)
    chunks = []
    while chunk := sock.recv(16384):
        chunks.append(chunk)
    print(b"".join(chunks).decode(), end="")
PY

python3 benchmarks/benchmark.py \
  --port "$port" \
  --clients 10 \
  --requests 10000 \
  --command mixed \
  --pipelined \
  --pipeline-depth 16 \
  --output /tmp/bytecachedb-demo-results.json
