#!/usr/bin/env python3
"""Concurrent benchmark client for ByteCacheDB."""

from __future__ import annotations

import argparse
import json
import random
import socket
import statistics
import threading
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Iterable


@dataclass
class BenchmarkResult:
    host: str
    port: int
    clients: int
    requests: int
    command: str
    pipelined: bool
    pipeline_depth: int
    total_time_seconds: float
    qps: float
    avg_latency_ms: float
    p50_latency_ms: float
    p95_latency_ms: float
    p99_latency_ms: float
    success_rate: float
    successes: int
    failures: int
    timestamp_unix: float


def read_response(sock_file) -> Any:
    line = sock_file.readline()
    if not line:
        raise ConnectionError("server closed connection")
    prefix = line[:1]
    payload = line[1:].rstrip(b"\r\n")

    if prefix in (b"+", b"-", b":"):
        return line.decode("utf-8", "replace").strip()
    if prefix == b"$":
        length = int(payload)
        if length < 0:
            return None
        data = sock_file.readline()
        return data.rstrip(b"\r\n").decode("utf-8", "replace")
    if prefix == b"*":
        count = int(payload)
        return [read_response(sock_file) for _ in range(count)]
    return line.decode("utf-8", "replace").strip()


def send_command(sock: socket.socket, sock_file, command: str) -> Any:
    sock.sendall((command + "\r\n").encode())
    return read_response(sock_file)


def prepopulate(host: str, port: int, keys: int) -> None:
    if keys <= 0:
        return
    with socket.create_connection((host, port), timeout=5) as sock:
        sock_file = sock.makefile("rb")
        for i in range(keys):
            send_command(sock, sock_file, f"SET bench:{i} {i}")


def percentile(values: list[float], pct: float) -> float:
    if not values:
        return 0.0
    values = sorted(values)
    index = min(len(values) - 1, max(0, int(round((pct / 100.0) * (len(values) - 1)))))
    return values[index]


def build_command(kind: str, request_id: int, preload_keys: int, rng: random.Random) -> str:
    if kind == "set":
        return f"SET bench:write:{request_id} {request_id}"
    if kind == "get":
        return f"GET bench:{request_id % max(1, preload_keys)}"
    if kind == "del":
        return f"DEL bench:write:{request_id}"
    if kind == "expire":
        return f"EXPIRE bench:{request_id % max(1, preload_keys)} 60"
    if kind == "mixed":
        roll = rng.random()
        if roll < 0.70:
            return build_command("get", request_id, preload_keys, rng)
        if roll < 0.90:
            return build_command("set", request_id, preload_keys, rng)
        if roll < 0.95:
            return build_command("del", request_id, preload_keys, rng)
        return build_command("expire", request_id, preload_keys, rng)
    raise ValueError(f"unknown command kind: {kind}")


def worker(
    host: str,
    port: int,
    command_kind: str,
    start_id: int,
    request_count: int,
    preload_keys: int,
    pipelined: bool,
    pipeline_depth: int,
    latencies_ms: list[float],
    results_lock: threading.Lock,
    counters: dict[str, int],
) -> None:
    rng = random.Random(start_id)
    local_latencies: list[float] = []
    local_successes = 0
    local_failures = 0

    try:
        with socket.create_connection((host, port), timeout=10) as sock:
            sock.settimeout(10)
            sock_file = sock.makefile("rb")
            sent = 0
            while sent < request_count:
                batch_size = min(pipeline_depth if pipelined else 1, request_count - sent)
                commands = [
                    build_command(command_kind, start_id + sent + offset, preload_keys, rng)
                    for offset in range(batch_size)
                ]
                started = time.perf_counter()
                sock.sendall(("".join(command + "\r\n" for command in commands)).encode())
                batch_successes = 0
                for _ in commands:
                    response = read_response(sock_file)
                    if isinstance(response, str) and response.startswith("-ERR"):
                        local_failures += 1
                    else:
                        local_successes += 1
                        batch_successes += 1
                elapsed_ms = (time.perf_counter() - started) * 1000.0
                if batch_successes:
                    per_command_latency = elapsed_ms / batch_size
                    local_latencies.extend([per_command_latency] * batch_successes)
                sent += batch_size
    except Exception:
        local_failures += request_count

    with results_lock:
        latencies_ms.extend(local_latencies)
        counters["successes"] += local_successes
        counters["failures"] += local_failures


def run_benchmark(args: argparse.Namespace) -> BenchmarkResult:
    preload_keys = 0
    if args.command in {"get", "mixed"}:
        preload_keys = min(max(args.clients * 20, 1000), max(args.requests, 1))
        prepopulate(args.host, args.port, preload_keys)

    latencies_ms: list[float] = []
    counters = {"successes": 0, "failures": 0}
    lock = threading.Lock()
    threads: list[threading.Thread] = []

    base = args.requests // args.clients
    remainder = args.requests % args.clients
    started = time.perf_counter()

    request_start = 0
    for client_id in range(args.clients):
        count = base + (1 if client_id < remainder else 0)
        thread = threading.Thread(
            target=worker,
            args=(
                args.host,
                args.port,
                args.command,
                request_start,
                count,
                preload_keys,
                args.pipelined,
                args.pipeline_depth,
                latencies_ms,
                lock,
                counters,
            ),
            daemon=True,
        )
        threads.append(thread)
        request_start += count

    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()

    elapsed = time.perf_counter() - started
    successes = counters["successes"]
    failures = counters["failures"]
    total = successes + failures
    qps = successes / elapsed if elapsed > 0 else 0.0
    avg_latency = statistics.fmean(latencies_ms) if latencies_ms else 0.0

    return BenchmarkResult(
        host=args.host,
        port=args.port,
        clients=args.clients,
        requests=args.requests,
        command=args.command,
        pipelined=args.pipelined,
        pipeline_depth=args.pipeline_depth if args.pipelined else 1,
        total_time_seconds=elapsed,
        qps=qps,
        avg_latency_ms=avg_latency,
        p50_latency_ms=percentile(latencies_ms, 50),
        p95_latency_ms=percentile(latencies_ms, 95),
        p99_latency_ms=percentile(latencies_ms, 99),
        success_rate=(successes / total * 100.0) if total else 0.0,
        successes=successes,
        failures=failures,
        timestamp_unix=time.time(),
    )


def write_result(result: BenchmarkResult, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(asdict(result), indent=2) + "\n", encoding="utf-8")


def print_result(result: BenchmarkResult) -> None:
    data = asdict(result)
    for key in (
        "clients",
        "requests",
        "command",
        "pipelined",
        "qps",
        "avg_latency_ms",
        "p50_latency_ms",
        "p95_latency_ms",
        "p99_latency_ms",
        "success_rate",
        "successes",
        "failures",
        "total_time_seconds",
    ):
        value = data[key]
        if isinstance(value, float):
            print(f"{key}: {value:.3f}")
        else:
            print(f"{key}: {value}")


def parse_args(argv: Iterable[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Benchmark ByteCacheDB over TCP")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6379)
    parser.add_argument("--clients", type=int, default=100)
    parser.add_argument("--requests", type=int, default=100000)
    parser.add_argument("--command", choices=["set", "get", "mixed"], default="set")
    parser.add_argument("--mixed", action="store_true", help="shortcut for --command mixed")
    parser.add_argument("--pipelined", action="store_true")
    parser.add_argument("--pipeline-depth", type=int, default=32)
    parser.add_argument("--output", default="benchmarks/results.json")
    args = parser.parse_args(argv)
    if args.mixed:
        args.command = "mixed"
    if args.clients <= 0:
        parser.error("--clients must be positive")
    if args.requests <= 0:
        parser.error("--requests must be positive")
    if args.pipeline_depth <= 0:
        parser.error("--pipeline-depth must be positive")
    return args


def main() -> int:
    args = parse_args()
    result = run_benchmark(args)
    print_result(result)
    write_result(result, Path(args.output))
    return 0 if result.failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
