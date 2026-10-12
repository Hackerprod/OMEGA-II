#!/usr/bin/env python3
"""Seal interrupted R14 diagnostic capture files in their existing D13 attempts."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "T0P" / "validator"))
from artifact_store import SessionAttemptStore  # type: ignore

RAW_CAPTURE_FILES = (
    "residency_plan_r14.json", "residency_smoke_schedule_r14.tsv", "events.edp", "emon_raw.txt", "emon_stderr.txt",
    "harness_stdout.txt", "harness_stderr.txt", "clock_map.csv", "block_marks_r14.csv",
    "residency_events_r14.jsonl", "residency_episodes_r14.jsonl",
    "residency_calibration_trials_r14.jsonl", "residency_margins_r14.jsonl",
    "residency_margin_episodes_r14.jsonl", "eviction_manifest_r14.jsonl", "preflight_r14.json",
)


def record(attempt: Path, reason: str) -> tuple[int, int]:
    store = SessionAttemptStore(attempt)
    metadata = json.loads((store.directory / "attempt_metadata.json").read_text(encoding="utf-8"))
    if metadata.get("protocol") != "D13-write-once-v1":
        raise RuntimeError(f"D13 protocol missing: {attempt}")
    raw_names = []
    for name in RAW_CAPTURE_FILES:
        path = store.directory / name
        if not path.is_file():
            continue
        sidecar = store.directory / f"{name}.sha256"
        if sidecar.is_file():
            store.verify_raw(name)
        else:
            store.register_external_raw(name)
        raw_names.append(name)
    if not raw_names:
        raise RuntimeError(f"No R14 raw artifacts found: {attempt}")
    status_name = "partial_capture_status_r14.json"
    status_path = store.directory / status_name
    payload = {"schema": "R14-PARTIAL-CAPTURE-v1", "state": "INCOMPLETE_INTERNAL_DIAGNOSTIC",
               "complete": False, "numeric_metrics_used": False, "reason": reason,
               "raw_file_count": len(raw_names), "raw_files": raw_names,
               "S20_state": "NOT_RUN_INCOMPLETE_CAPTURE"}
    if status_path.exists():
        raise RuntimeError(f"D13 status artifact already exists; refusing replacement: {status_path}")
    encoded = (json.dumps(payload, sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n").encode("utf-8")
    store.write_derived(status_name, encoded, source_raw=raw_names,
                        metadata={"role": "incomplete diagnostic status", "numeric_metrics_used": False})
    return len(raw_names), len([name for name in raw_names if (store.directory / f"{name}.sha256").is_file()])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--attempt", type=Path, required=True)
    parser.add_argument("--reason", required=True)
    ns = parser.parse_args()
    try:
        count, sealed = record(ns.attempt, ns.reason)
        print(f"D13_PARTIAL_R14 PASS attempt={ns.attempt} raw_files={count} sealed_sha256={sealed} status=INCOMPLETE_INTERNAL_DIAGNOSTIC numeric_metrics_used=false S20=NOT_RUN")
        return 0
    except Exception as exc:
        print(f"D13_PARTIAL_R14 REJECT {type(exc).__name__}: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
