#!/usr/bin/env python3
"""R14 smoke capture adapter for the sole S-20 validator and D13 store."""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
VALIDATOR_DIR = ROOT / "T0P" / "validator"
if str(VALIDATOR_DIR) not in sys.path:
    sys.path.insert(0, str(VALIDATOR_DIR))

from artifact_store import SessionAttemptStore  # type: ignore
import s20_validator  # type: ignore

DEFAULT_EVIDENCE = Path(__file__).resolve().parent / "logs_r14" / "obj_r14" / "telemetry_r14_qa.txt"
RAW_CAPTURE_FILES = (
    "residency_plan_r14.json", "residency_smoke_schedule_r14.tsv", "events.edp", "emon_raw.txt", "emon_stderr.txt",
    "harness_stdout.txt", "harness_stderr.txt", "clock_map.csv", "block_marks_r14.csv",
    "residency_events_r14.jsonl", "residency_episodes_r14.jsonl",
    "residency_calibration_trials_r14.jsonl", "residency_margins_r14.jsonl",
    "residency_margin_episodes_r14.jsonl", "eviction_manifest_r14.jsonl", "preflight_r14.json",
)


def _json_bytes(value: dict[str, Any]) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n").encode("utf-8")


def prepare(attempt: Path, plan_path: Path, schedule_path: Path, edp_source: Path) -> None:
    plan = json.loads(plan_path.read_text(encoding="utf-8"))
    schedule = schedule_path.read_bytes()
    if b"#plan_sha256=" + plan["sha256"].encode("ascii") + b"\n" not in schedule:
        raise RuntimeError("smoke_schedule_plan_hash_mismatch")
    source_edp = edp_source.read_text(encoding="utf-8-sig")
    if source_edp.count("-l3600") != 1:
        raise RuntimeError("source_EDP_expected_single_l3600")
    edp = source_edp.replace("-l3600", "-l144000")
    store = SessionAttemptStore.create(attempt, session_id="residency-r14-SMOKE",
                                       attempt_id=attempt.name)
    store.write_raw("residency_plan_r14.json", plan_path.read_bytes())
    store.write_raw("residency_smoke_schedule_r14.tsv", schedule)
    store.write_raw("events.edp", edp.encode("utf-8"))
    print(f"D13_R14_ATTEMPT PREPARED path={store.directory} plan_sha256={plan['sha256']} label=SMOKE AUTHORIZED_json=absent")


def _lp_map(preflight: dict[str, Any]) -> list[dict[str, Any]]:
    workers = preflight["P_core_map"]["workers"]
    result = []
    for worker in workers:
        p = int(worker["p_core"])
        lp = int(worker["lp"])
        result.append({"p_core": p, "core_id": p * 4, "core_type": "P",
                       "worker_lp": lp, "smt_siblings": [lp, lp + 1]})
    return result


def capture_postprocess(attempt: Path, output: Path) -> dict[str, Any]:
    store = SessionAttemptStore(attempt)
    meta = json.loads((store.directory / "attempt_metadata.json").read_text(encoding="utf-8"))
    if meta.get("protocol") != "D13-write-once-v1":
        raise RuntimeError("D13_protocol_missing")
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
    if "preflight_r14.json" not in raw_names:
        raise RuntimeError("harness_preflight_missing_no_os_blocker_record")
    preflight = json.loads((store.directory / "preflight_r14.json").read_text(encoding="utf-8"))
    if preflight.get("label") != "SMOKE" or preflight.get("numeric_metrics_used") is not False:
        raise RuntimeError("smoke_preflight_label_or_numeric_use_invalid")
    edp_path = store.directory / "events.edp"
    edp_hash = store.verify_raw("events.edp")
    utc_offset = dt.datetime.now().astimezone().utcoffset()
    if utc_offset is None:
        raise RuntimeError("local_UTC_offset_unavailable")
    offset_minutes = int(utc_offset.total_seconds() / 60)
    cfg = {
        "schema_version": "S20-R2-v1", "qpf_hz": float(preflight["qpf_hz"]),
        "qpf_source": "QueryPerformanceFrequency captured by harness_r14",
        "f_tsc_qualified_hz": float(preflight["f_tsc_qualified_hz"]),
        "f_tsc_qpc_hz": float(preflight["f_tsc_qualified_hz"]),
        "f_tsc_configured_hz": float(preflight["f_tsc_configured_hz"]),
        "f_tsc_qualification_qpf_hz": float(preflight["qpf_hz"]),
        "emon_local_utc_offset_minutes": offset_minutes, "nominal_window_s": 0.05,
        "lp_map": _lp_map(preflight), "event_groups": [list(s20_validator.DEFAULT_EVENT_GROUP)],
        "multiplexing": False, "periods_lost": 0,
        "energy_event": s20_validator.EVENT_611, "energy_semantics": "INTERVAL_DELTA",
        "energy_unit_joule": 1 / 16384, "tcc_target_c": 100,
        "thermal_history_clear_at_start": False, "mode": "temporal",
        "logical_processor_count": 24, "profile": "s20",
        "event_map_source": "T0P/telemetry/reader/emon_joint_qualification.edp; one effective S-20 event group",
        "marks_clock_domain": "qpc_ticks", "clock_map_path": str(store.directory / "clock_map.csv"),
        "edp_path": str(edp_path), "edp_sha256": edp_hash,
    }
    store.write_raw("effective_config_source.json", _json_bytes(cfg))
    store.write_derived("effective_config.json", _json_bytes(cfg),
                        source_raw=["effective_config_source.json", "events.edp"],
                        metadata={"adapter": "telemetry_r14", "role": "effective S-20 smoke config"})
    try:
        result = s20_validator.validate_capture(store.directory / "emon_raw.txt", store.directory / "clock_map.csv",
                                                store.directory / "block_marks_r14.csv", cfg, mode="temporal")
        state = result.get("state", "INVALID")
        codes = sorted({reason.get("code", "UNKNOWN") for row in result.get("results", [])
                        for reason in row.get("reasons", [])})
        reason = ",".join(codes[:8]) if codes else "none"
    except Exception as exc:
        result = {"schema_version": "S20-RESULT-v1", "state": "INVALID_INPUT",
                  "error_type": type(exc).__name__, "error": str(exc)}
        state, reason = "INVALID_INPUT", f"{type(exc).__name__}:{exc}"
    encoded = _json_bytes(result)
    validation_sources = [name for name in ("emon_raw.txt", "clock_map.csv", "block_marks_r14.csv",
                                             "effective_config_source.json", "events.edp") if name in raw_names]
    digest = store.write_derived("s20_validation_r14.json", encoded,
                                 source_raw=validation_sources,
                                 metadata={"validator": "T0P/validator/s20_validator.py", "mode": "temporal",
                                           "label": "SMOKE", "numeric_metrics_used": False})
    answer = {"state": state, "reason": reason, "samples": result.get("samples"),
              "emon_interval_groups": result.get("emon_interval_groups"),
              "clock_map_rows": result.get("clock_map_rows"), "sha256": digest,
              "attempt_directory": str(store.directory)}
    if output.parent.resolve() != store.directory:
        raise RuntimeError("D13_status_output_must_be_inside_attempt")
    status_name = output.name
    store.write_derived(status_name, _json_bytes(answer),
                        source_raw=[name for name in raw_names if name in {
                            "residency_plan_r14.json", "residency_smoke_schedule_r14.tsv", "events.edp",
                            "emon_raw.txt", "preflight_r14.json"}],
                        metadata={"role": "SMOKE pipeline result", "numeric_metrics_used": False})
    print(f"HARNESS_S20_R14 state={state} reason={reason} samples={answer['samples']} intervals={answer['emon_interval_groups']} clock_rows={answer['clock_map_rows']} raw_write_once=PASS validation_sha256={digest}")
    return answer


def self_test(output: Path) -> None:
    validator = Path(__file__).resolve().parents[1] / "collector" / "qualify_r14.py"
    from subprocess import run
    result = run([sys.executable, "-B", str(validator), "--self-test",
                  "--self-test-output", str(output)], cwd=ROOT, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f"shared_S20_D13_fixture_failed:{result.stderr.strip()}")
    print(result.stdout.strip())
    print("TELEMETRY_R14_QA PASS unique_validator=T0P/validator/s20_validator.py D13=write-once")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    prep = sub.add_parser("prepare")
    prep.add_argument("--attempt", type=Path, required=True)
    prep.add_argument("--plan", type=Path, required=True)
    prep.add_argument("--schedule", type=Path, required=True)
    prep.add_argument("--edp-source", type=Path, required=True)
    post = sub.add_parser("postprocess")
    post.add_argument("--attempt", type=Path, required=True)
    post.add_argument("--output", type=Path, required=True)
    test = sub.add_parser("self-test")
    test.add_argument("--output", type=Path, default=DEFAULT_EVIDENCE)
    ns = parser.parse_args(argv)
    try:
        if ns.command == "prepare":
            prepare(ns.attempt, ns.plan, ns.schedule, ns.edp_source)
        elif ns.command == "postprocess":
            capture_postprocess(ns.attempt, ns.output)
        else:
            self_test(ns.output)
        return 0
    except Exception as exc:
        print(f"TELEMETRY_R14 REJECT {type(exc).__name__}: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
