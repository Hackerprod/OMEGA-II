#!/usr/bin/env python3
"""R16 smoke capture adapter for the sole S-20 validator and D13 store."""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import math
import sys
import time
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
VALIDATOR_DIR = ROOT / "T0P" / "validator"
if str(VALIDATOR_DIR) not in sys.path:
    sys.path.insert(0, str(VALIDATOR_DIR))

from artifact_store import SessionAttemptStore  # type: ignore
import s20_validator  # type: ignore

DEFAULT_EVIDENCE = Path(__file__).resolve().parent / "logs_r16" / "obj_r16" / f"telemetry_r16_qa_{time.time_ns()}.txt"
RAW_CAPTURE_FILES = (
    "residency_plan_r16.json", "residency_smoke_schedule_r16.tsv", "events.edp", "emon_raw.txt", "emon_stderr.txt",
    "official_run_stdout_r16.txt", "official_run_stderr_r16.txt",
    "phase_preflight_r16.json", "phase_preflight_stdout_r16.txt", "phase_preflight_stderr_r16.txt",
    "phase_qa_instruments_expulsion_r16.json", "phase_qa_instruments_expulsion_stdout_r16.txt", "phase_qa_instruments_expulsion_stderr_r16.txt",
    "phase_qualification_r16.json", "phase_qualification_stdout_r16.txt", "phase_qualification_stderr_r16.txt",
    "phase_residency_r16.json", "phase_residency_stdout_r16.txt", "phase_residency_stderr_r16.txt",
    "acquisition_chain_r16.json",
    "capture_budget_r16.json", "disassembly_qa_r16.json", "harness_disassembly_r16.txt", "pinstr_symbol_map_r16.txt",
    "aux_qualification_raw_r16.jsonl", "aux_qualification_summary_r16.jsonl",
    "clock_map.csv", "block_marks_r16.csv",
    "residency_events_r16.jsonl", "residency_episodes_r16.jsonl",
    "residency_calibration_trials_r16.jsonl", "residency_margins_r16.jsonl",
    "residency_margin_episodes_r16.jsonl", "eviction_manifest_r16.jsonl", "preflight_r16.json",
    "segment_conditioning_r16.jsonl", "capture_timing_r16.json",
    "residency_load_marks_r16.jsonl", "effective_config_source.json",
)


def _json_bytes(value: dict[str, Any]) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n").encode("utf-8")


def capture_budget(schedule: bytes) -> dict[str, Any]:
    lines = [line for line in schedule.decode("utf-8").splitlines() if line and not line.startswith("#")]
    if not lines or lines[0] != "sequence\tevent_id\tphase\tblock_id\tatomic_group_id\tsegment_id":
        raise RuntimeError("smoke_schedule_header_invalid")
    rows = [line.split("\t") for line in lines[1:]]
    if any(len(row) != 6 for row in rows):
        raise RuntimeError("smoke_schedule_row_invalid")
    phases = {name: sum(row[2] == name for row in rows) for name in ("calibration", "residency", "aa")}
    if phases != {"calibration": 60, "residency": 180, "aa": 24}:
        raise RuntimeError(f"smoke_schedule_counts_invalid:{phases}")
    # 180 s conditioning once per frozen segment; reserve 45 s for each
    # calibration event. The common R per unit can require several geometric
    # trials and the first real N=3 run exceeded the earlier 15 s allowance.
    # Each retained sample reserves 2.5 s plus both 0.2 s margins; qualification
    # has a 420 s allowance. EMON stops explicitly after the final block.
    calibration_budget_s = phases["calibration"] * 45.0
    planned_s = 180.0 + calibration_budget_s + (phases["residency"] + phases["aa"]) * 2.9 + 420.0
    headroom = 1.20
    limit = math.ceil(planned_s * headroom / 0.050)
    if planned_s > 4200 or limit <= 0:
        raise RuntimeError(f"capture_budget_unbounded:{planned_s}")
    raw_bytes_per_second = 128 * 1024  # 3x the measured EMON rate in recent R16 attempts.
    return {"schedule_phases": phases, "segment_conditioning_s": 180.0,
            "calibration_target_s": 2.5, "calibration_candidate_trials_factor": 2.0,
            "calibration_budget_s": calibration_budget_s, "residency_target_and_margins_s": 2.9,
            "qa_qualification_allowance_s": 420.0, "headroom_factor": headroom,
            "estimated_capture_s": round(planned_s, 3), "emon_interval_s": 0.050,
            "emon_loops": limit, "maximum_capture_s": limit * 0.050,
            "raw_bytes_per_second_budget": raw_bytes_per_second,
            "estimated_max_raw_bytes": int(limit * 0.050 * raw_bytes_per_second + 64 * 1024 * 1024),
            "controlled_stop": "EMON -stop after final block marks and margins"}


def prepare(attempt: Path, plan_path: Path, schedule_path: Path, edp_source: Path) -> dict[str, Any]:
    plan = json.loads(plan_path.read_text(encoding="utf-8"))
    schedule = schedule_path.read_bytes()
    if b"#plan_sha256=" + plan["sha256"].encode("ascii") + b"\n" not in schedule:
        raise RuntimeError("smoke_schedule_plan_hash_mismatch")
    source_edp = edp_source.read_text(encoding="utf-8-sig")
    if source_edp.count("-l3600") != 1:
        raise RuntimeError("source_EDP_expected_single_l3600")
    budget = capture_budget(schedule)
    if budget["estimated_max_raw_bytes"] >= 1024**3:
        raise RuntimeError(f"capture_attempt_estimate_exceeds_1GiB:{budget['estimated_max_raw_bytes']}")
    edp = source_edp.replace("-l3600", f"-l{budget['emon_loops']}")
    store = SessionAttemptStore.create(attempt, session_id="residency-r16-SMOKE",
                                       attempt_id=attempt.name)
    store.write_raw("residency_plan_r16.json", plan_path.read_bytes())
    store.write_raw("residency_smoke_schedule_r16.tsv", schedule)
    store.write_raw("events.edp", edp.encode("utf-8"))
    budget_bytes = _json_bytes(budget)
    store.write_raw("capture_budget_r16.json", budget_bytes)
    print(f"EMON_CAPTURE_BUDGET_R16 PASS loops={budget['emon_loops']} estimated_s={budget['estimated_capture_s']} max_s={budget['maximum_capture_s']} schedule_events=264");
    print(f"D13_R16_ATTEMPT PREPARED path={store.directory} plan_sha256={plan['sha256']} label=SMOKE AUTHORIZED_json=absent")
    return budget


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
    if "preflight_r16.json" not in raw_names:
        raise RuntimeError("harness_preflight_missing_no_os_blocker_record")
    preflight = json.loads((store.directory / "preflight_r16.json").read_text(encoding="utf-8"))
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
        "qpf_source": "QueryPerformanceFrequency captured by harness_r16",
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
                        metadata={"adapter": "telemetry_r16", "role": "effective S-20 smoke config"})
    try:
        result = s20_validator.validate_capture(store.directory / "emon_raw.txt", store.directory / "clock_map.csv",
                                                store.directory / "block_marks_r16.csv", cfg, mode="temporal")
        state = result.get("state", "INVALID")
        codes = sorted({reason.get("code", "UNKNOWN") for row in result.get("results", [])
                        for reason in row.get("reasons", [])})
        reason = ",".join(codes[:8]) if codes else "none"
    except Exception as exc:
        result = {"schema_version": "S20-RESULT-v1", "state": "INVALID_INPUT",
                  "error_type": type(exc).__name__, "error": str(exc)}
        state, reason = "INVALID_INPUT", f"{type(exc).__name__}:{exc}"
    encoded = _json_bytes(result)
    validation_sources = [name for name in ("emon_raw.txt", "clock_map.csv", "block_marks_r16.csv",
                                             "effective_config_source.json", "events.edp") if name in raw_names]
    digest = store.write_derived("s20_validation_r16.json", encoded,
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
                            "residency_plan_r16.json", "residency_smoke_schedule_r16.tsv", "events.edp",
                            "emon_raw.txt", "preflight_r16.json"}],
                        metadata={"role": "SMOKE pipeline result", "numeric_metrics_used": False})
    print(f"HARNESS_S20_R16 state={state} reason={reason} samples={answer['samples']} intervals={answer['emon_interval_groups']} clock_rows={answer['clock_map_rows']} raw_write_once=PASS validation_sha256={digest}")
    return answer


def self_test(output: Path) -> None:
    validator = Path(__file__).resolve().parents[1] / "collector" / "qualify_r16.py"
    from subprocess import run
    result = run([sys.executable, "-B", str(validator), "--self-test",
                  "--self-test-output", str(output)], cwd=ROOT, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f"shared_S20_D13_fixture_failed:{result.stderr.strip()}")
    print(result.stdout.strip())
    print("TELEMETRY_R16_QA PASS unique_validator=T0P/validator/s20_validator.py D13=write-once")


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
        print(f"TELEMETRY_R16 REJECT {type(exc).__name__}: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
