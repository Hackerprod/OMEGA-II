#!/usr/bin/env python3
"""Harness adapter for the unique S-20 validator and immutable D13 attempts."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
VALIDATOR_DIR = ROOT / "T0P" / "validator"
if str(VALIDATOR_DIR) not in sys.path:
    sys.path.insert(0, str(VALIDATOR_DIR))

from artifact_store import SessionAttemptStore  # type: ignore  # D13 implementation in validator/
import s20_validator  # type: ignore  # S-20 implementation in validator/


def _fit_clock_map(path: Path, config: dict[str, Any]):
    """Delegate clock-map parsing and QPF uncertainty to S-20."""
    return s20_validator.parse_clock_map(path, config)


def _read_emon(path: Path, clock_map: dict[str, Any], config: dict[str, Any]):
    """Delegate EMON event parsing and interval checks to S-20."""
    return s20_validator.parse_emon_raw(path, config, clock_map)


def _derive_block(emon_raw: Path, clock_map: Path, block_marks: Path,
                  effective_config: Path, mode: str = "temporal"):
    """Return the S-20 result; this adapter does not recalculate its channels."""
    return s20_validator.validate_capture(emon_raw, clock_map, block_marks,
                                          effective_config, mode=mode)


def _check(metrics: dict[str, Any], _reported_hz: float | None = None):
    """Gate only on the unique S-20 result and its per-channel/per-window states."""
    if metrics.get("schema_version") != "S20-RESULT-v1":
        return False, "S20_RESULT_SCHEMA_INVALID"
    if metrics.get("state") != "VALID":
        codes = sorted({reason.get("code", "UNKNOWN") for sample in metrics.get("results", [])
                        for reason in sample.get("reasons", [])})
        return False, "S20_" + ("_".join(codes) if codes else metrics.get("state", "INVALID"))
    if not metrics.get("results") or any(sample.get("state") != "VALID" for sample in metrics["results"]):
        return False, "S20_CHANNEL_OR_WINDOW_INVALID"
    return True, "S20-R2-v1_VALID"


def validate_attempt(*, emon_raw: Path, clock_map: Path, block_marks: Path,
                     effective_config: Path, attempt_directory: Path,
                     session_id: str, attempt_id: str, mode: str = "temporal"):
    """Snapshot raw inputs into a new D13 attempt, validate, and write a linked result."""
    from adapter_example import validate_attempt as validate_d13_attempt
    outcome = validate_d13_attempt(emon_raw=emon_raw, clock_map=clock_map,
                                   block_marks=block_marks, effective_config=effective_config,
                                   attempt_directory=attempt_directory, session_id=session_id,
                                   attempt_id=attempt_id, mode=mode)
    valid, reason = _check(outcome["result"])
    return {**outcome, "adapter_state": "VALID" if valid else "HOLD_INSTRUMENTATION",
            "adapter_reason": reason}


def archive_harness_outputs(*, raw_jsonl: Path, summary_jsonl: Path,
                            attempt_directory: Path, session_id: str, attempt_id: str):
    """Archive fresh harness files through SessionAttemptStore; never reopen/truncate."""
    store = SessionAttemptStore.create(attempt_directory, session_id=session_id, attempt_id=attempt_id)
    raw_sha = store.ingest_raw(raw_jsonl, "harness_raw.jsonl")
    summary_sha = store.ingest_raw(summary_jsonl, "harness_summary.jsonl")
    metadata = {"raw_sha256": raw_sha, "summary_sha256": summary_sha,
                "source_raw": ["harness_raw.jsonl", "harness_summary.jsonl"],
                "label": "SMOKE", "numeric_metrics_used": False}
    encoded = (json.dumps(metadata, sort_keys=True, separators=(",", ":"), allow_nan=False)+"\n").encode()
    derived_sha = store.write_derived("harness_archive.json", encoded,
                                      source_raw=["harness_raw.jsonl", "harness_summary.jsonl"],
                                      metadata={"protocol": "D13-write-once-v1", "role": "harness smoke archive"})
    return {"attempt_directory": str(store.directory), "raw_sha256": raw_sha,
            "summary_sha256": summary_sha, "archive_sha256": derived_sha}


def archive_emon_raw(*, emon_raw: Path, attempt_directory: Path,
                     session_id: str, attempt_id: str):
    """Snapshot an official EMON reader output into a fresh D13 attempt."""
    store = SessionAttemptStore.create(attempt_directory, session_id=session_id, attempt_id=attempt_id)
    raw_sha = store.ingest_raw(emon_raw, "emon_raw.txt")
    metadata = {"label": "SMOKE", "reader": "Intel EMON", "numeric_metrics_used": False,
                "raw_sha256": raw_sha, "source_raw": ["emon_raw.txt"]}
    encoded = (json.dumps(metadata, sort_keys=True, separators=(",", ":"), allow_nan=False)+"\n").encode()
    summary_sha = store.write_derived("emon_capture_summary.json", encoded,
                                      source_raw=["emon_raw.txt"],
                                      metadata={"protocol": "D13-write-once-v1", "role": "SMOKE EMON reader output"})
    return {"attempt_directory": str(store.directory), "raw_sha256": raw_sha,
            "summary_sha256": summary_sha}


def validate_existing_attempt(attempt_directory: Path, mode: str = "temporal"):
    """Verify the stored D13 sources and call S-20 again; result creation is exclusive."""
    store = SessionAttemptStore(attempt_directory)
    metadata = json.loads((store.directory / "attempt_metadata.json").read_text(encoding="utf-8"))
    if metadata.get("protocol") != "D13-write-once-v1":
        raise RuntimeError("D13 protocol metadata missing")
    for name in ("emon_raw.txt", "clock_map.csv", "block_marks.csv", "effective_config_source.json", "events.edp"):
        store.verify_raw(name)
    result = _derive_block(store.directory/"emon_raw.txt", store.directory/"clock_map.csv",
                           store.directory/"block_marks.csv", store.directory/"effective_config.json", mode)
    valid, reason = _check(result)
    encoded = (json.dumps(result, sort_keys=True, separators=(",", ":"), allow_nan=False)+"\n").encode()
    source_names = ["emon_raw.txt", "clock_map.csv", "block_marks.csv", "effective_config_source.json", "events.edp"]
    digest = store.write_derived("s20_validation_r12.json", encoded, source_raw=source_names,
                                 metadata={"validator": "S20-R2-v1", "mode": mode})
    return {"state": "VALID" if valid else "HOLD_INSTRUMENTATION", "reason": reason,
            "result_sha256": digest, "samples": result.get("samples"),
            "channels": len(result.get("results", []))}


def self_test(output: Path):
    qualifier = Path(__file__).resolve().parents[1] / "collector" / "qualify_r12.py"
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [sys.executable, "-B", str(qualifier), "--self-test", "--self-test-output", str(output)]
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError("S20/D13 integration self-test failed: " + result.stderr.strip())
    print(result.stdout.strip())
    print(f"HARNESS_S20_INTEGRATION self_test=PASS D13=write-once output={output}")


def main(argv=None):
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    test = sub.add_parser("self-test-s20")
    test.add_argument("--output", type=Path,
                      default=Path(__file__).resolve().parent/"logs_r12"/"obj_r12"/"s20_integration_qa_r12.txt")
    archive = sub.add_parser("archive-harness")
    archive.add_argument("--raw", type=Path, required=True)
    archive.add_argument("--summary", type=Path, required=True)
    archive.add_argument("--attempt-directory", type=Path, required=True)
    archive.add_argument("--session-id", required=True)
    archive.add_argument("--attempt-id", required=True)
    emon = sub.add_parser("archive-emon")
    emon.add_argument("--raw", type=Path, required=True)
    emon.add_argument("--attempt-directory", type=Path, required=True)
    emon.add_argument("--session-id", required=True)
    emon.add_argument("--attempt-id", required=True)
    validate = sub.add_parser("validate")
    validate.add_argument("--emon-raw", type=Path, required=True)
    validate.add_argument("--clock-map", type=Path, required=True)
    validate.add_argument("--block-marks", type=Path, required=True)
    validate.add_argument("--effective-config", type=Path, required=True)
    validate.add_argument("--attempt-directory", type=Path, required=True)
    validate.add_argument("--session-id", required=True)
    validate.add_argument("--attempt-id", required=True)
    validate.add_argument("--mode", choices=("temporal", "full"), default="temporal")
    existing = sub.add_parser("validate-existing")
    existing.add_argument("--attempt-directory", type=Path, required=True)
    existing.add_argument("--mode", choices=("temporal", "full"), default="temporal")
    ns = parser.parse_args(argv)
    try:
        if ns.command == "self-test-s20":
            self_test(ns.output)
        elif ns.command == "archive-harness":
            result = archive_harness_outputs(raw_jsonl=ns.raw, summary_jsonl=ns.summary,
                                             attempt_directory=ns.attempt_directory,
                                             session_id=ns.session_id, attempt_id=ns.attempt_id)
            print("HARNESS_D13_ARCHIVE PASS " + json.dumps(result, sort_keys=True))
        elif ns.command == "archive-emon":
            result = archive_emon_raw(emon_raw=ns.raw, attempt_directory=ns.attempt_directory,
                                      session_id=ns.session_id, attempt_id=ns.attempt_id)
            print("HARNESS_EMON_D13_ARCHIVE PASS " + json.dumps(result, sort_keys=True))
        elif ns.command == "validate":
            result = validate_attempt(emon_raw=ns.emon_raw, clock_map=ns.clock_map,
                block_marks=ns.block_marks, effective_config=ns.effective_config,
                attempt_directory=ns.attempt_directory, session_id=ns.session_id,
                attempt_id=ns.attempt_id, mode=ns.mode)
            print("HARNESS_S20 state="+result["adapter_state"]+" reason="+result["adapter_reason"]+
                  " samples="+str(result["result"].get("samples")))
            return 0 if result["adapter_state"] == "VALID" else 1
        else:
            result = validate_existing_attempt(ns.attempt_directory, ns.mode)
            print("HARNESS_S20_EXISTING " + json.dumps(result, sort_keys=True))
            return 0 if result["state"] == "VALID" else 1
        return 0
    except Exception as exc:
        print(f"HARNESS_S20_REJECT {type(exc).__name__}: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
