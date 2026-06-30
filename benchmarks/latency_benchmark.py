#!/usr/bin/env python3
"""Latency-focused ByteCacheDB benchmark."""

from __future__ import annotations

import argparse
import json
from dataclasses import asdict
from pathlib import Path

import benchmark


def main() -> int:
    parser = argparse.ArgumentParser(description="ByteCacheDB latency benchmark")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6379)
    parser.add_argument("--clients", default="1,10,50,100")
    parser.add_argument("--requests", type=int, default=50000)
    parser.add_argument("--command", choices=["set", "get", "mixed"], default="get")
    parser.add_argument("--pipelined", action="store_true")
    parser.add_argument("--output", default="benchmarks/latency_results.json")
    args = parser.parse_args()

    results = []
    for clients in [int(item.strip()) for item in args.clients.split(",") if item.strip()]:
        argv = [
            "--host",
            args.host,
            "--port",
            str(args.port),
            "--clients",
            str(clients),
            "--requests",
            str(args.requests),
            "--command",
            args.command,
        ]
        if args.pipelined:
            argv.append("--pipelined")
        bench_args = benchmark.parse_args(argv)
        result = benchmark.run_benchmark(bench_args)
        benchmark.print_result(result)
        results.append(asdict(result))

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
