#!/usr/bin/env python3
"""Run ByteCacheDB with several client counts and save aggregate results."""

from __future__ import annotations

import argparse
import json
from dataclasses import asdict
from pathlib import Path

import benchmark


def main() -> int:
    parser = argparse.ArgumentParser(description="ByteCacheDB concurrent client sweep")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6379)
    parser.add_argument("--clients", default="1,10,100,500")
    parser.add_argument("--requests", type=int, default=100000)
    parser.add_argument("--command", choices=["set", "get", "mixed"], default="mixed")
    parser.add_argument("--output", default="benchmarks/concurrent_results.json")
    args = parser.parse_args()

    client_counts = [int(item.strip()) for item in args.clients.split(",") if item.strip()]
    results = []
    for clients in client_counts:
        bench_args = benchmark.parse_args(
            [
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
        )
        result = benchmark.run_benchmark(bench_args)
        benchmark.print_result(result)
        results.append(asdict(result))

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
