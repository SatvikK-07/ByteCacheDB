#!/usr/bin/env python3
"""Measure AOF recovery time for ByteCacheDB."""

from __future__ import annotations

import argparse
import json
import socket
import subprocess
import time
from pathlib import Path

import benchmark


def wait_for_server(host: str, port: int, timeout: float = 10.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.5):
                return
        except OSError:
            time.sleep(0.05)
    raise TimeoutError("server did not become ready")


def start_server(
    binary: str,
    host: str,
    port: int,
    threads: int,
    aof_path: str,
    snapshot_path: str,
) -> subprocess.Popen:
    process = subprocess.Popen(
        [
            binary,
            "--host",
            host,
            "--port",
            str(port),
            "--threads",
            str(threads),
            "--enable-aof",
            "true",
            "--aof-path",
            aof_path,
            "--snapshot-path",
            snapshot_path,
            "--fsync",
            "everysec",
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    wait_for_server(host, port)
    return process


def stop_server(process: subprocess.Popen) -> None:
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def main() -> int:
    parser = argparse.ArgumentParser(description="ByteCacheDB AOF recovery benchmark")
    parser.add_argument("--binary", default="build/bytecachedb")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6380)
    parser.add_argument("--threads", type=int, default=8)
    parser.add_argument("--keys", type=int, default=100000)
    parser.add_argument("--aof-path", default="data/recovery_benchmark.aof")
    parser.add_argument("--output", default="benchmarks/recovery_results.json")
    args = parser.parse_args()

    aof_path = Path(args.aof_path)
    aof_path.parent.mkdir(parents=True, exist_ok=True)
    if aof_path.exists():
        aof_path.unlink()
    snapshot_path = Path(str(aof_path) + ".snapshot")
    if snapshot_path.exists():
        snapshot_path.unlink()

    process = start_server(
        args.binary, args.host, args.port, args.threads, str(aof_path), str(snapshot_path)
    )
    try:
        bench_args = benchmark.parse_args(
            [
                "--host",
                args.host,
                "--port",
                str(args.port),
                "--clients",
                "50",
                "--requests",
                str(args.keys),
                "--command",
                "set",
                "--pipelined",
            ]
        )
        insert_result = benchmark.run_benchmark(bench_args)
    finally:
        stop_server(process)

    started = time.perf_counter()
    process = start_server(
        args.binary, args.host, args.port, args.threads, str(aof_path), str(snapshot_path)
    )
    recovery_seconds = time.perf_counter() - started

    recovered = 0
    try:
        with socket.create_connection((args.host, args.port), timeout=5) as sock:
            sock_file = sock.makefile("rb")
            for i in range(min(args.keys, 1000)):
                value = benchmark.send_command(sock, sock_file, f"GET bench:write:{i}")
                if value == str(i):
                    recovered += 1
    finally:
        stop_server(process)

    result = {
        "keys_inserted": args.keys,
        "sampled_keys_checked": min(args.keys, 1000),
        "sampled_keys_recovered": recovered,
        "recovery_seconds": recovery_seconds,
        "insert_qps": insert_result.qps,
        "aof_path": str(aof_path),
        "timestamp_unix": time.time(),
    }

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if recovered == min(args.keys, 1000) else 1


if __name__ == "__main__":
    raise SystemExit(main())
