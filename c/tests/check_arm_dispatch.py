#!/usr/bin/env python3
"""Reference, single-CPU, and fault-injection checks for both build preferences."""
import argparse
import json
import os
import subprocess
import sys

# Rust reference implementation, input byte i = i % 251, unkeyed hashing.
GOLDEN = {
    4096: "015094013f57a5277b59d8475c0501042c0b642e531b0a1c8f58d2163229e969",
    1048576: "74cb441fd087764ca9c3694da742ebe30cbeb3060a17009ca81825c7a8d10343",
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--fault-binary", required=True)
    parser.add_argument("--runner", action="append", default=[])
    parser.add_argument("--require-supported", action="store_true")
    args = parser.parse_args()

    def invoke(cpu=None, corrupt=None):
        env = dict(os.environ)
        binary = args.binary
        if corrupt is not None:
            binary = args.fault_binary
            env["BLAKE3_TEST_CORRUPT_ITERATION"] = str(corrupt)
        return subprocess.run(
            [*args.runner, binary], capture_output=True, text=True,
            timeout=120, env=env,
            preexec_fn=(lambda: os.sched_setaffinity(0, {cpu})) if cpu is not None else None,
        )

    def verify(result):
        assert result.returncode == 0, (result.returncode, result.stderr)
        rows = [json.loads(line) for line in result.stdout.splitlines()]
        assert len(rows) == 30
        mixed = [row for row in rows if row.get("mixed_dispatch_simulated_lengths")]
        assert [row["iteration"] for row in mixed] == list(range(16))
        for row in rows:
            assert row["digest"] == GOLDEN[row["bytes"]], row
        for row in mixed:
            assert row["calls"][1] > 0 and row["calls"][2] > 0, row
            assert (row["calls"][4] > 0) == bool(row["policy"]), row

    first = invoke()
    if first.returncode == 77:
        print(first.stderr.strip())
        return 1 if args.require_supported else 77
    verify(first)
    # The old test intermittently failed coverage when all threads shared a CPU.
    if hasattr(os, "sched_getaffinity"):
        cpu = min(os.sched_getaffinity(0))
        for _ in range(5):
            verify(invoke(cpu=cpu))
    for iteration in range(16):
        result = invoke(corrupt=iteration)
        assert result.returncode == 12, (iteration, result.returncode, result.stderr)
        assert f"digest mismatch at iteration {iteration}" in result.stderr
    print("PASS: every digest and backend checked; single-CPU runs and all fault positions passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
