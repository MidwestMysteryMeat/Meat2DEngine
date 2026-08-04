#!/usr/bin/env python3
"""Check stable benchmark results and conservative throughput floors."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


LINE_PATTERN = re.compile(
    r"^(?P<label>step(?:_parallel)?\(\))\s+"
    r"world=(?P<world>\S+) ticks=(?P<ticks>\d+)\s+"
    r"seconds=(?P<seconds>[0-9.]+) theoretical_Mcells_per_second="
    r"(?P<throughput>[0-9.]+) moves=(?P<moves>\d+) hash=(?P<hash>0x[0-9a-fA-F]+)$"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True)
    return parser.parse_args()


def parse_output(path: Path) -> dict[str, dict[str, str | int | float]]:
    results: dict[str, dict[str, str | int | float]] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        match = LINE_PATTERN.match(line.strip())
        if match is None:
            continue
        values = match.groupdict()
        results["serial" if values["label"] == "step()" else "parallel"] = {
            "world": values["world"],
            "ticks": int(values["ticks"]),
            "seconds": float(values["seconds"]),
            "throughput": float(values["throughput"]),
            "moves": int(values["moves"]),
            "hash": values["hash"].lower(),
        }
    if set(results) != {"serial", "parallel"}:
        raise SystemExit(f"benchmark output did not contain both step modes: {path}")
    return results


def main() -> int:
    args = parse_args()
    baseline = json.loads(args.baseline.read_text(encoding="utf-8"))
    results = parse_output(args.output)
    failures: list[str] = []
    for mode in ("serial", "parallel"):
        expected = baseline[mode]
        actual = results[mode]
        for field in ("world", "ticks"):
            if actual[field] != baseline[field]:
                failures.append(f"{mode} {field}: expected {baseline[field]}, got {actual[field]}")
        if actual["moves"] != expected["moves"]:
            failures.append(f"{mode} moves: expected {expected['moves']}, got {actual['moves']}")
        if actual["hash"] != expected["hash"].lower():
            failures.append(f"{mode} hash: expected {expected['hash']}, got {actual['hash']}")
        minimum = float(expected["minimum_theoretical_mcells_per_second"])
        if actual["throughput"] < minimum:
            failures.append(
                f"{mode} throughput: minimum {minimum:.2f}, got {actual['throughput']:.2f}"
            )
    if failures:
        raise SystemExit("benchmark regression:\n- " + "\n- ".join(failures))
    print("BENCHMARK CHECK PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
