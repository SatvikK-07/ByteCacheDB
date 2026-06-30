#!/usr/bin/env python3
"""Run the standard pipelined/non-pipelined ByteCacheDB benchmark matrix."""

from __future__ import annotations

import argparse
import json
import platform
import subprocess
import time
from dataclasses import asdict
from pathlib import Path

from benchmark import run_benchmark


def system_value(name: str) -> str:
    try:
        return subprocess.check_output(
            ["sysctl", "-n", name], text=True, stderr=subprocess.DEVNULL
        ).strip()
    except (FileNotFoundError, subprocess.CalledProcessError):
        return "unknown"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run ByteCacheDB benchmark matrix")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6379)
    parser.add_argument("--requests", type=int, default=100000)
    parser.add_argument("--command", choices=["set", "get", "mixed"], default="mixed")
    parser.add_argument("--pipeline-depth", type=int, default=32)
    parser.add_argument("--clients", type=int, nargs="+", default=[1, 10, 100])
    parser.add_argument("--server-workers", type=int, default=8)
    parser.add_argument("--server-shards", type=int, default=64)
    parser.add_argument("--aof-mode", default="disabled")
    parser.add_argument("--output", default="benchmarks/matrix_results.json")
    return parser.parse_args()


def main() -> int:
    options = parse_args()
    results = []
    for clients in options.clients:
        for pipelined in (False, True):
            run_args = argparse.Namespace(
                host=options.host,
                port=options.port,
                clients=clients,
                requests=options.requests,
                command=options.command,
                mixed=options.command == "mixed",
                pipelined=pipelined,
                pipeline_depth=options.pipeline_depth,
                output="",
            )
            result = run_benchmark(run_args)
            results.append(asdict(result))
            mode = f"pipeline {options.pipeline_depth}" if pipelined else "request-response"
            print(
                f"{clients:>3} clients | {mode:<16} | "
                f"{result.qps:>10,.0f} QPS | p95 {result.p95_latency_ms:.3f} ms"
            )

    document = {
        "generated_at_unix": time.time(),
        "methodology": {
            "transport": "TCP loopback",
            "workload": options.command,
            "requests_per_case": options.requests,
            "clients": options.clients,
            "pipeline_depth": options.pipeline_depth,
            "server_workers": options.server_workers,
            "storage_shards": options.server_shards,
            "aof": options.aof_mode,
            "non_pipelined_latency": "one measured request-response round trip",
            "pipelined_latency": "batch round-trip divided by commands in that batch",
        },
        "machine": {
            "platform": platform.platform(),
            "architecture": platform.machine(),
            "processor": system_value("machdep.cpu.brand_string"),
            "logical_cpus": system_value("hw.ncpu"),
            "memory_bytes": system_value("hw.memsize"),
        },
        "results": results,
    }
    output = Path(options.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    return 0 if all(result["failures"] == 0 for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
