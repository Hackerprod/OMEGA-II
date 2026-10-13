#!/usr/bin/env python3
"""R18 smoke capture adapter for the sole S-20 validator and D13 store."""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import math
import re
import sys
import time
from collections import Counter
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
from artifact_store_r18 import ArtifactError, SessionAttemptStore
import residency_plan_r18 as plans
import s20_validator_r18 as s20_validator  # type: ignore

DEFAULT_EVIDENCE = Path(__file__).resolve().parent / "logs_r18" / "obj_r18" / f"telemetry_r18_qa_{time.time_ns()}.txt"
RAW_CAPTURE_FILES = (
    "residency_plan_r18.json", "residency_smoke_schedule_r18.tsv", "events.edp", "emon_raw.txt", "emon_stderr.txt",
    "phase_preflight_r18.json", "phase_preflight_stdout_r18.txt", "phase_preflight_stderr_r18.txt",
    "phase_qa_instruments_expulsion_r18.json", "phase_qa_instruments_expulsion_stdout_r18.txt", "phase_qa_instruments_expulsion_stderr_r18.txt",
    "phase_qualification_r18.json", "phase_qualification_stdout_r18.txt", "phase_qualification_stderr_r18.txt",
    "phase_residency_r18.json", "phase_residency_stdout_r18.txt", "phase_residency_stderr_r18.txt",
    "phase_final_controls_r18.json", "phase_final_controls_stdout_r18.txt", "phase_final_controls_stderr_r18.txt",
    "phase_derivation_verdict_r18.json", "phase_derivation_verdict_stdout_r18.txt", "phase_derivation_verdict_stderr_r18.txt",
    "instrument_qualification_acquisition_r18.json", "instrument_qualification_stdout_r18.txt",
    "instrument_qualification_stderr_r18.txt",
    "acquisition_chain_r18.json",
    "capture_budget_r18.json", "disassembly_qa_r18.json", "harness_disassembly_r18.txt", "pinstr_symbol_map_r18.txt",
    "aux_qualification_raw_r18.jsonl", "aux_qualification_summary_r18.jsonl",
    "clock_map.csv", "block_marks_r18.csv",
    "residency_events_r18.jsonl", "residency_episodes_r18.jsonl",
    "residency_calibration_trials_r18.jsonl", "residency_margins_r18.jsonl",
    "residency_margin_episodes_r18.jsonl", "eviction_manifest_r18.jsonl", "preflight_r18.json",
    "segment_conditioning_r18.jsonl", "capture_timing_r18.json",
    "residency_load_marks_r18.jsonl", "instrumentation_probe_r18.jsonl",
    "session_configuration_r18.json", "effective_config_source.json",
)


def _json_bytes(value: dict[str, Any]) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n").encode("utf-8")


def _compact_window_summary(items: list[dict[str, Any]]) -> dict[str, Any]:
    state_counts = Counter(str(item.get("state", "UNKNOWN")) for item in items)
    reason_counts: Counter[str] = Counter()
    ranges: dict[str, dict[str, float | int]] = {}
    numeric_fields = ("activity_factor_a_i_k", "f_active_hz", "l2_numerator", "l2_denominator",
                      "l2_fraction", "llc_numerator", "llc_denominator", "llc_fraction",
                      "energy_delta", "temperature_c", "temperature")
    values: dict[str, list[float]] = {field: [] for field in numeric_fields}
    for item in items:
        for reason in item.get("reasons", []):
            code = reason.get("code") if isinstance(reason, dict) else reason
            if code:
                reason_counts[str(code)] += 1
        metrics = item.get("metrics") if isinstance(item.get("metrics"), dict) else {}
        for field in numeric_fields:
            value = item.get(field, metrics.get(field))
            if isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value):
                values[field].append(float(value))
    for field, field_values in values.items():
        if field_values:
            ranges[field] = {"count": len(field_values), "min": min(field_values), "max": max(field_values)}
    return {"count": len(items), "state_counts": dict(sorted(state_counts.items())),
            "reason_counts": dict(sorted(reason_counts.items())), "numeric_ranges": ranges}


def _compact_s20_result(result: dict[str, Any]) -> dict[str, Any]:
    """Keep all verdicts and per-window extrema after full in-memory validation."""
    compact = {key: value for key, value in result.items() if key != "results"}
    compact_rows = []
    for row in result.get("results", []):
        reduced = {key: value for key, value in row.items() if key not in {"metrics", "window_statuses"}}
        metrics = row.get("metrics")
        if isinstance(metrics, dict):
            reduced_metrics = {key: value for key, value in metrics.items() if not isinstance(value, list)}
            list_summaries = {key: _compact_window_summary(value) for key, value in metrics.items()
                              if isinstance(value, list)}
            if list_summaries:
                reduced_metrics["list_summaries"] = list_summaries
            reduced["metrics"] = reduced_metrics
        windows = row.get("window_statuses")
        if isinstance(windows, list):
            reduced["window_summary"] = _compact_window_summary(windows)
        compact_rows.append(reduced)
    compact["results"] = compact_rows
    compact["compact_window_summaries"] = True
    return compact


def capture_budget(schedule: bytes) -> dict[str, Any]:
    all_lines = schedule.decode("utf-8").splitlines()
    lines = [line for line in all_lines if line and not line.startswith("#")]
    if not lines or lines[0] != "sequence\tevent_id\tphase\tblock_id\tatomic_group_id\tsegment_id":
        raise RuntimeError("smoke_schedule_header_invalid")
    rows = [line.split("\t") for line in lines[1:]]
    if any(len(row) != 6 for row in rows):
        raise RuntimeError("smoke_schedule_row_invalid")
    phases = {name: sum(row[2] == name for row in rows) for name in ("calibration", "residency", "aa")}
    diagnostic = next((line.split("mode=", 1)[1] for line in all_lines
                       if line.startswith("#diagnostic=INSTRUMENT_QUALIFICATION_R18;mode=")), None)
    segments = {row[5] for row in rows if row[2] == "residency" and row[5]}
    if diagnostic:
        smoke_n1 = diagnostic == "SMOKE_N1"
        if diagnostic not in {"SMOKE_N1", "FULL_36_BLOCKS"}:
            raise RuntimeError(f"instrument_qualification_mode_invalid:{diagnostic}")
        expected = {"calibration": 3, "residency": 3, "aa": 0} if smoke_n1 else {
            "calibration": 12, "residency": 36, "aa": 0}
        if phases != expected or len(segments) != (1 if smoke_n1 else 3):
            raise RuntimeError(f"instrument_qualification_schedule_counts_invalid:{phases}:{sorted(segments)}")
        conditioning_s = 180.0 * len(segments)
        calibration_budget_s = phases["calibration"] * 75.4
        residency_s = phases["residency"] * 75.4
        probe_blocks = len(segments) * 2 * 31
        probe_s = probe_blocks * 2.9
        qualification_s = 45.0
        planned_s = conditioning_s + calibration_budget_s + residency_s + probe_s + qualification_s
        calibration_target_s, residency_target_s = 75.0, 75.0
        label = diagnostic
    else:
        if phases != {"calibration": 60, "residency": 180, "aa": 24} or segments != {"R01", "R02", "R03"}:
            raise RuntimeError(f"smoke_schedule_counts_invalid:{phases}:{sorted(segments)}")
        conditioning_s = 180.0 * len(segments)
        probe_blocks = len(segments) * 2 * 31
        calibration_budget_s = phases["calibration"] * 10.0
        residency_s = (phases["residency"] + phases["aa"]) * 2.9
        probe_s = probe_blocks * 2.9
        qualification_s = 600.0
        planned_s = conditioning_s + calibration_budget_s + residency_s + probe_s + qualification_s
        calibration_target_s, residency_target_s = 2.5, 2.5
        label = "N3_ACQUISITION_CHAIN"
    headroom = 1.80
    limit = math.ceil(planned_s * headroom / 0.050)
    if planned_s > 5700 or limit <= 0:
        raise RuntimeError(f"capture_budget_unbounded:{planned_s}")
    raw_bytes_per_second = 96 * 1024  # 2.8x the measured 34.4 kB/s from the completed R18 N3 capture.
    maximum_bytes = int(limit * 0.050 * raw_bytes_per_second + 64 * 1024 * 1024)
    if maximum_bytes >= 1024**3:
        raise RuntimeError(f"capture_attempt_estimate_exceeds_1GiB:{maximum_bytes}")
    return {"schedule_mode": label, "schedule_events": len(rows), "schedule_phases": phases,
            "residency_segments": len(segments),
            "segment_conditioning_s_each": 180.0, "segment_conditioning_s_total": conditioning_s,
            "hot_probe_blocks": probe_blocks, "hot_probe_target_s": 2.5,
            "hot_probe_margins_s_each": 0.2,
            "probe_capture_budget_s": probe_s, "calibration_target_s": calibration_target_s,
            "calibration_budget_s": calibration_budget_s, "residency_target_s": residency_target_s,
            "residency_budget_s": residency_s, "qualification_allowance_s": qualification_s,
            "headroom_factor": headroom,
            "estimated_capture_s": round(planned_s, 3), "emon_interval_s": 0.050,
            "emon_loops": limit, "maximum_capture_s": limit * 0.050,
            "raw_bytes_per_second_budget": raw_bytes_per_second,
            "estimated_max_raw_bytes": maximum_bytes,
            "controlled_stop": "EMON -stop after final block marks and margins"}


def _size_edp_loop(edp_text: str, loops: int) -> str:
    matches = re.findall(r"(?<!\S)-l\s*(\d+)(?=\s|$)", edp_text)
    if len(matches) != 1:
        raise RuntimeError(f"source_EDP_expected_single_loop_limit:{len(matches)}")
    sized, count = re.subn(r"(?<!\S)-l\s*\d+(?=\s|$)", f"-l{loops}", edp_text)
    if count != 1 or len(re.findall(r"(?<!\S)-l\s*(\d+)(?=\s|$)", sized)) != 1:
        raise RuntimeError("source_EDP_loop_limit_rewrite_invalid")
    return sized


def prepare(attempt: Path, plan_path: Path, schedule_path: Path, edp_source: Path) -> dict[str, Any]:
    plan = json.loads(plan_path.read_text(encoding="utf-8"))
    schedule = schedule_path.read_bytes()
    if b"#plan_sha256=" + plan["sha256"].encode("ascii") + b"\n" not in schedule:
        raise RuntimeError("smoke_schedule_plan_hash_mismatch")
    source_edp = edp_source.read_text(encoding="utf-8-sig")
    budget = capture_budget(schedule)
    if budget["estimated_max_raw_bytes"] >= 1024**3:
        raise RuntimeError(f"capture_attempt_estimate_exceeds_1GiB:{budget['estimated_max_raw_bytes']}")
    edp = _size_edp_loop(source_edp, int(budget["emon_loops"]))
    verified_plan = plans.verify_plan(plan_path)
    store = SessionAttemptStore.create(attempt, session_id="residency-r18-SMOKE",
                                       attempt_id=attempt.name)
    offset = dt.datetime.now().astimezone().utcoffset()
    if offset is None:
        raise RuntimeError("local_UTC_offset_unavailable_before_capture")
    session_configuration = {
        "schema_version": "T0P_SESSION_CONFIGURATION_R18_V1",
        "session_id": "residency-r18-SMOKE",
        "recorded_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "local_utc_offset_minutes": int(offset.total_seconds() / 60),
        "plan_sha256": verified_plan["sha256"],
        "schedule_sha256": hashlib.sha256(schedule).hexdigest(),
        "source_edp_sha256": hashlib.sha256(edp_source.read_bytes()).hexdigest(),
        "energy_event": s20_validator.EVENT_611,
        "energy_semantics": "INTERVAL_DELTA",
        "energy_unit_joule": 1 / 16384,
        "energy_unit_source": "recorded S-20 platform profile; EMON 0x606/0x611 channel contract",
        "tcc_target_c": 100,
        "tcc_target_source": "recorded S-20 CPU thermal target configuration",
        "logical_processor_count": 24,
        "logical_processor_count_source": "recorded S-20 EMON topology contract; verified against phase_preflight topology",
        "thermal_history_clear_at_start": False,
        "multiplexing": False,
        "periods_lost": 0,
        "constants_recorded_before_emon": True,
        "constant_source": "captured session protocol configuration at prepare time",
    }
    store.write_raw("residency_plan_r18.json", plan_path.read_bytes())
    store.write_raw("residency_smoke_schedule_r18.tsv", schedule)
    store.write_raw("events.edp", edp.encode("utf-8"))
    budget_bytes = _json_bytes(budget)
    store.write_raw("capture_budget_r18.json", budget_bytes)
    store.write_raw("session_configuration_r18.json", _json_bytes(session_configuration))
    print(f"EMON_CAPTURE_BUDGET_R18 PASS mode={budget['schedule_mode']} loops={budget['emon_loops']} estimated_s={budget['estimated_capture_s']} max_s={budget['maximum_capture_s']} schedule_events={budget['schedule_events']} estimated_max_bytes={budget['estimated_max_raw_bytes']}");
    print(f"D13_R18_ATTEMPT PREPARED path={store.directory} plan_sha256={plan['sha256']} label=SMOKE AUTHORIZED_json=absent")
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


def _s25_probe_expected_keys(segment_replicas: dict[str, int]) -> set[tuple[str, str, int, int]]:
    return {(segment, endpoint, replica, sample)
            for segment, replica in segment_replicas.items()
            for endpoint in ("BEGIN", "END") for sample in range(1, 32)}


def _s20_phase_audit(store: SessionAttemptStore, plan: dict[str, Any],
                     scheduled_ids: list[str], probe_rows: list[dict[str, Any]],
                     result: dict[str, Any], config: dict[str, Any]) -> dict[str, Any]:
    """Trace S-20 FULL decisions to plan events, phase evidence and D13 raw hashes."""
    phase_states: dict[str, dict[str, Any]] = {}
    plan_sha = plan["sha256"]
    session_cfg_sha = store.verify_raw("session_configuration_r18.json")
    edp_sha = store.verify_raw("events.edp")
    if config.get("plan_sha256") != plan_sha:
        raise RuntimeError("D10_full_config_plan_hash_mismatch")
    if config.get("session_configuration_sha256") != session_cfg_sha:
        raise RuntimeError("D10_full_config_session_hash_mismatch")
    if config.get("edp_sha256") != edp_sha:
        raise RuntimeError("D10_full_config_edp_hash_mismatch")
    compat = json.loads((store.directory / "effective_config.json.lineage.json").read_text(encoding="utf-8"))
    effective_bytes = (store.directory / "effective_config.json").read_bytes()
    effective_sha = hashlib.sha256(effective_bytes).hexdigest()
    if compat.get("derived_sha256") != effective_sha:
        raise RuntimeError("D10_full_effective_config_hash_mismatch")
    for name, expected_sha in compat.get("sources", {}).items():
        if store.verify_raw(name) != expected_sha:
            raise RuntimeError(f"D10_full_config_raw_hash_mismatch:{name}")

    for phase in ("preflight", "qa_instruments_expulsion", "qualification", "residency",
                  "final_controls", "derivation_verdict"):
        name = f"phase_{phase}_r18.json"
        path = store.directory / name
        if not path.is_file():
            phase_states[phase] = {"state": "INVALID", "reason": "NOT_ACQUIRED_IN_SMOKE_PHASE_CUT"}
            continue
        raw_sha = store.verify_raw(name)
        evidence = json.loads(path.read_text(encoding="utf-8"))
        if (evidence.get("phase") != phase or evidence.get("plan_sha256") != plan_sha or
                evidence.get("session_id") != config.get("session_id")):
            phase_states[phase] = {"state": "INVALID", "reason": "PHASE_IDENTITY_OR_PLAN_HASH_MISMATCH",
                                   "raw_sha256": raw_sha}
            continue
        phase_states[phase] = {"state": evidence.get("state", "INVALID"),
                               "reason": evidence.get("reason", "none"), "raw_sha256": raw_sha}

    plan_events = {event["id"]: event for event in plan["events"]}
    scheduled = [plan_events[event_id] for event_id in scheduled_ids]
    segment_replicas: dict[str, int] = {}
    for event in scheduled:
        if event.get("phase") != "residency" or not event.get("segment_id"):
            continue
        segment, replica = str(event["segment_id"]), int(event["replica"])
        if segment in segment_replicas and segment_replicas[segment] != replica:
            raise RuntimeError(f"D10_segment_replica_identity_invalid:{segment}")
        segment_replicas[segment] = replica
    expected_segments = sorted(segment_replicas)
    expected_probe_keys = _s25_probe_expected_keys(segment_replicas)
    actual_probe_keys: list[tuple[str, str, int, int]] = []
    malformed_probes = []
    for row in probe_rows:
        try:
            key = (str(row["segment_id"]), str(row["endpoint"]),
                   int(row["replica"]), int(row["sample"]))
            workers = row["worker_evidence"]
            if (row.get("condition_id") != "S25_HOT_PROBE" or row.get("frequency_basis") != "frequency_of_condition" or
                    row.get("load_only") is not True or row.get("weights_refreshed_after_warmup") is not False or
                    not isinstance(workers, list) or len(workers) != 8 or
                    not isinstance(row.get("region_seconds"), (int, float)) or row["region_seconds"] < 2.0 or
                    not isinstance(row.get("instrumentation_overhead_fraction"), (int, float)) or
                    row["instrumentation_overhead_fraction"] > .01):
                malformed_probes.append(key)
            actual_probe_keys.append(key)
        except (KeyError, TypeError, ValueError):
            malformed_probes.append(("", "", 0))
    probe_coverage_ok = (not malformed_probes and len(actual_probe_keys) == len(set(actual_probe_keys)) and
                         set(actual_probe_keys) == expected_probe_keys)
    probe_report = {"state": "PASS" if probe_coverage_ok else "INVALID",
                    "expected_segments": expected_segments, "expected_rows": len(expected_probe_keys),
                    "actual_rows": len(actual_probe_keys), "malformed_rows": len(malformed_probes),
                    "unique_and_complete": probe_coverage_ok,
                    "reason": "none" if probe_coverage_ok else "S25_BEGIN_END_PROBE_COVERAGE_OR_BUDGET_INVALID"}

    s22 = result.get("s22_hot_controls", [])
    expected_s22_rows = len(expected_probe_keys) * 8
    s22_complete = len(s22) == expected_s22_rows
    s22_valid = s22_complete and all(row.get("state") == "PASS" for row in s22)
    s22_report = {"state": "PASS" if s22_valid else "INVALID",
                  "expected_core_rows": expected_s22_rows, "actual_core_rows": len(s22),
                  "all_eight_P_per_probe": s22_complete,
                  "max_H2": max((row.get("H2", 0.0) for row in s22), default=None),
                  "max_H3_over_D2": max((row.get("H3_over_D2", 0.0) for row in s22), default=None),
                  "min_D2": min((row.get("D2", 0) for row in s22), default=None),
                  "reason": "none" if s22_valid else "S22_FULL_CORE_OR_THRESHOLD_INVALID"}

    final_file = store.directory / "phase_final_controls_r18.json"
    if final_file.is_file():
        final_controls = json.loads(final_file.read_text(encoding="utf-8"))
        from official_run_r18 import validate_d8_post_revalidation
        d8_result = validate_d8_post_revalidation(final_controls.get("d8_captures", {}))
        d8_state = "PASS" if (d8_result.get("status") == "PASS" and
                               isinstance(final_controls.get("raw_sources_sha256"), dict)) else "INVALID"
        d8_report = {"state": d8_state, "reason": d8_result.get("reason", "none"),
                     "validator_result": d8_result,
                     "raw_sources_sha256": final_controls.get("raw_sources_sha256", {})}
    else:
        d8_report = {"state": "INVALID", "reason": "D8_REVALIDATION_NOT_ACQUIRED_IN_SMOKE_PHASE_CUT"}
    if d8_report["state"] != "PASS":
        ratio_report = {"state": "INVALID", "reason": "COLD_HOT_RATIO_REQUIRES_HASHED_D8_FINAL_CONTROL_CAPTURE"}
    else:
        ratio_report = {"state": "PASS", "reason": "none",
                        "min_cold_hot_ratio": d8_report["validator_result"].get("min_cold_hot_ratio")}

    return {"state": "INVALID" if result.get("state") != "VALID" or not s22_valid or d8_report["state"] != "PASS" else "VALID",
            "mode": config.get("mode"), "plan_sha256": plan_sha,
            "effective_config_sha256": effective_sha,
            "session_configuration_sha256": session_cfg_sha, "events_edp_sha256": edp_sha,
            "phase_states": phase_states, "frequency_groups_from_plan": True,
            "phase_science_or_derivation_declared_group_lists": False,
            "S20_FULL": {"state": result.get("state"), "samples": result.get("samples"),
                         "reasons": sorted({reason.get("code") for row in result.get("results", [])
                                             for reason in row.get("reasons", [])})},
            "qualification": phase_states["qualification"],
            "residency": phase_states["residency"],
            "final_controls_D8": d8_report, "cold_hot_ratio": ratio_report,
            "S22_hot_full": s22_report, "S25_begin_end_probe": probe_report}


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
        try:
            store.verify_raw(name)
        except ArtifactError:
            store.register_external_raw(name)
        raw_names.append(name)
    if "preflight_r18.json" not in raw_names:
        raise RuntimeError("harness_preflight_missing_no_os_blocker_record")
    preflight = json.loads((store.directory / "preflight_r18.json").read_text(encoding="utf-8"))
    if preflight.get("label") != "SMOKE" or preflight.get("numeric_metrics_used") is not False:
        raise RuntimeError("smoke_preflight_label_or_numeric_use_invalid")
    topology_source_name = None
    topology_map_sha256 = preflight.get("topology_map_sha256")
    if not isinstance(topology_map_sha256, str) or not re.fullmatch(r"[0-9a-f]{64}", topology_map_sha256):
        for candidate in ("phase_preflight_r18.json", "instrument_qualification_acquisition_r18.json",
                          "phase_qa_instruments_expulsion_r18.json"):
            if candidate not in raw_names:
                continue
            candidate_record = json.loads((store.directory / candidate).read_text(encoding="utf-8"))
            candidate_hash = candidate_record.get("topology_map_sha256")
            if isinstance(candidate_hash, str) and re.fullmatch(r"[0-9a-f]{64}", candidate_hash):
                topology_source_name, topology_map_sha256 = candidate, candidate_hash
                break
    else:
        topology_source_name = "preflight_r18.json"
    if topology_source_name is None:
        raise RuntimeError("S20_topology_map_hash_missing_from_hashed_acquisition_evidence")
    topology_source_sha256 = store.verify_raw(topology_source_name)
    session_cfg = json.loads((store.directory / "session_configuration_r18.json").read_text(encoding="utf-8"))
    plan = plans.verify_plan(store.directory / "residency_plan_r18.json")
    if (session_cfg.get("plan_sha256") != plan["sha256"] or
            session_cfg.get("schedule_sha256") != hashlib.sha256((store.directory / "residency_smoke_schedule_r18.tsv").read_bytes()).hexdigest()):
        raise RuntimeError("recorded_session_plan_or_schedule_hash_mismatch")
    edp_path = store.directory / "events.edp"
    edp_hash = store.verify_raw("events.edp")
    schedule_lines = (store.directory / "residency_smoke_schedule_r18.tsv").read_text(encoding="utf-8").splitlines()
    diagnostic_mode = next((line.split("mode=", 1)[1] for line in schedule_lines
                            if line.startswith("#diagnostic=INSTRUMENT_QUALIFICATION_R18;mode=")), None)
    s20_mode = "temporal" if diagnostic_mode == "SMOKE_N1" else "full"
    scheduled_ids = [line.split("\t")[1] for line in schedule_lines
                     if line and not line.startswith("#") and not line.startswith("sequence\t")]
    plan_by_id = {event["id"]: event for event in plan["events"]}
    if len(scheduled_ids) != len(set(scheduled_ids)) or any(eid not in plan_by_id for eid in scheduled_ids):
        raise RuntimeError("frequency_groups_schedule_event_identity_invalid")
    comparison_groups: dict[str, dict[str, Any]] = {}
    condition_replica_counts: dict[str, int] = {}
    for event_id in scheduled_ids:
        event = plan_by_id[event_id]
        comparison_id = (event.get("pair_id") if event["phase"] == "aa" else
                         f"{event.get('block_id', 'single')}|{event.get('atomic_group_id', event_id)}")
        group = comparison_groups.setdefault(comparison_id, {"kind": "repeat", "condition_counts": {}})
        condition = str(event["condition_id"])
        group["condition_counts"][condition] = group["condition_counts"].get(condition, 0) + 1
        if len(group["condition_counts"]) > 1:
            group["kind"] = "contrast"
        key = f"{condition}|r{int(event['replica'])}"
        condition_replica_counts[key] = condition_replica_counts.get(key, 0) + 1
    probe_path = store.directory / "instrumentation_probe_r18.jsonl"
    probe_rows = []
    if probe_path.is_file():
        probe_rows = [json.loads(line) for line in probe_path.read_text(encoding="utf-8").splitlines() if line]
    for row in probe_rows:
        comparison_id = str(row.get("comparison_set", ""))
        if not comparison_id or row.get("frequency_basis") != "frequency_of_condition":
            raise RuntimeError("probe_frequency_basis_or_group_missing")
        group = comparison_groups.setdefault(comparison_id, {"kind": "repeat", "condition_counts": {}})
        condition = str(row["condition_id"])
        group["condition_counts"][condition] = group["condition_counts"].get(condition, 0) + 1
        key = f"{condition}|r{int(row['replica'])}"
        condition_replica_counts[key] = condition_replica_counts.get(key, 0) + 1
    cfg = {
        "schema_version": "S20-R2-v1", "qpf_hz": float(preflight["qpf_hz"]),
        "qpf_source": "QueryPerformanceFrequency captured by harness_r18",
        "f_tsc_qualified_hz": float(preflight["f_tsc_qualified_hz"]),
        "f_tsc_qpc_hz": float(preflight["f_tsc_qualified_hz"]),
        "f_tsc_configured_hz": float(preflight["f_tsc_configured_hz"]),
        "f_tsc_qualification_qpf_hz": float(preflight["qpf_hz"]),
        "emon_local_utc_offset_minutes": int(session_cfg["local_utc_offset_minutes"]), "nominal_window_s": 0.05,
        "session_id": str(session_cfg.get("session_id", "")),
        "lp_map": _lp_map(preflight), "event_groups": [list(s20_validator.DEFAULT_EVENT_GROUP)],
        "comparison_groups": comparison_groups, "condition_replica_counts": condition_replica_counts,
        "multiplexing": bool(session_cfg["multiplexing"]), "periods_lost": int(session_cfg["periods_lost"]),
        "energy_event": str(session_cfg["energy_event"]), "energy_semantics": session_cfg["energy_semantics"],
        "energy_unit_joule": float(session_cfg["energy_unit_joule"]), "tcc_target_c": int(session_cfg["tcc_target_c"]),
        "thermal_history_clear_at_start": bool(session_cfg["thermal_history_clear_at_start"]), "mode": s20_mode,
        "logical_processor_count": int(session_cfg["logical_processor_count"]), "profile": "s20",
        "frequency_basis": "frequency_of_condition",
        "frequency_group_source": "plan r18 + acquired S25 probe rows; IDs bound by plan/schedule hashes",
        "session_configuration_sha256": store.verify_raw("session_configuration_r18.json"),
        "plan_sha256": plan["sha256"],
        "event_map_source": "events.edp captured for this attempt; one effective S-20 event group",
        "marks_clock_domain": "qpc_ticks", "clock_map_path": str(store.directory / "clock_map.csv"),
        "edp_path": str(edp_path), "edp_sha256": edp_hash,
    }
    source_config_path = store.directory / "effective_config_source.json"
    existing_config_source = None
    if source_config_path.is_file():
        store.verify_raw("effective_config_source.json")
        existing_config_source = json.loads(source_config_path.read_text(encoding="utf-8"))
        for path_key in ("clock_map_path", "edp_path"):
            prior_path = existing_config_source.get(path_key)
            current_path = cfg.get(path_key)
            if (isinstance(prior_path, str) and isinstance(current_path, str) and
                    Path(prior_path).name == Path(current_path).name):
                cfg[path_key] = prior_path
    config_source = {**cfg, "topology_map_sha256": topology_map_sha256,
                     "topology_source": topology_source_name,
                     "topology_source_sha256": topology_source_sha256}
    if existing_config_source is not None:
        if existing_config_source != config_source:
            raise RuntimeError("effective_config_source_conflicts_with_hashed_acquisition_evidence")
    else:
        store.write_raw("effective_config_source.json", _json_bytes(config_source))
    raw_names.append("effective_config_source.json")
    effective_sources = [name for name in ("effective_config_source.json", "events.edp", "session_configuration_r18.json",
                        "residency_plan_r18.json", "residency_smoke_schedule_r18.tsv", "preflight_r18.json",
                        "phase_preflight_r18.json", "phase_qa_instruments_expulsion_r18.json",
                        "instrument_qualification_acquisition_r18.json", "instrumentation_probe_r18.jsonl")
                        if store._raw_sidecar(name).is_file()]
    store.write_derived("effective_config.json", _json_bytes(cfg),
                        source_raw=effective_sources,
                        metadata={"adapter": "telemetry_r18", "role": "effective S-20 capture config",
                                  "mode": s20_mode, "topology_map_sha256": topology_map_sha256,
                                  "topology_source": topology_source_name,
                                  "topology_source_sha256": topology_source_sha256})
    try:
        result = s20_validator.validate_capture(store.directory / "emon_raw.txt", store.directory / "clock_map.csv",
                                                store.directory / "block_marks_r18.csv", cfg, mode=s20_mode)
        state = result.get("state", "INVALID")
        codes = sorted({reason.get("code", "UNKNOWN") for row in result.get("results", [])
                        for reason in row.get("reasons", [])})
        reason = ",".join(codes[:8]) if codes else "none"
    except Exception as exc:
        result = {"schema_version": "S20-RESULT-v1", "state": "INVALID_INPUT",
                  "error_type": type(exc).__name__, "error": str(exc)}
        state, reason = "INVALID_INPUT", f"{type(exc).__name__}:{exc}"
    phase_audit = _s20_phase_audit(store, plan, scheduled_ids, probe_rows, result, cfg)
    phase_audit_bytes = _json_bytes(phase_audit)
    phase_audit_sources = [name for name in raw_names if name in {
        "emon_raw.txt", "clock_map.csv", "block_marks_r18.csv", "events.edp",
        "session_configuration_r18.json", "residency_plan_r18.json",
        "residency_smoke_schedule_r18.tsv", "preflight_r18.json",
        "phase_preflight_r18.json", "phase_qa_instruments_expulsion_r18.json",
        "instrument_qualification_acquisition_r18.json",
        "phase_qualification_r18.json", "phase_residency_r18.json",
        "phase_final_controls_r18.json", "instrumentation_probe_r18.jsonl",
        "effective_config_source.json"}]
    phase_audit_sha = store.write_derived("s20_phase_audit_r18.json", phase_audit_bytes,
        source_raw=phase_audit_sources,
        metadata={"validator": "T0P/collector/s20_validator_r18.py", "mode": s20_mode,
                  "plan_sha256": plan["sha256"], "phase_decisions_from_raw": True})
    encoded = _json_bytes(_compact_s20_result(result))
    validation_sources = [name for name in ("emon_raw.txt", "clock_map.csv", "block_marks_r18.csv",
                                             "effective_config_source.json", "events.edp", "session_configuration_r18.json",
                                             "residency_plan_r18.json", "residency_smoke_schedule_r18.tsv",
                                             "preflight_r18.json", "phase_preflight_r18.json",
                                             "phase_qa_instruments_expulsion_r18.json",
                                             "instrument_qualification_acquisition_r18.json",
                                             "instrumentation_probe_r18.jsonl", "effective_config_source.json") if name in raw_names]
    digest = store.write_derived("s20_validation_r18.json", encoded,
                                 source_raw=validation_sources,
                         metadata={"validator": "T0P/collector/s20_validator_r18.py", "mode": s20_mode,
                                           "label": "SMOKE", "numeric_metrics_used": False,
                                           "full_window_validation_before_compaction": True,
                                           "compact_window_summaries": True})
    answer = {"state": state, "reason": reason, "samples": result.get("samples"),
              "emon_interval_groups": result.get("emon_interval_groups"),
              "clock_map_rows": result.get("clock_map_rows"), "sha256": digest,
              "s20_mode": s20_mode, "attempt_directory": str(store.directory),
              "phase_audit_state": phase_audit["state"],
              "phase_audit_sha256": phase_audit_sha,
              "phase_audit": phase_audit}
    if diagnostic_mode in {"SMOKE_N1", "FULL_36_BLOCKS"}:
        import instrument_qualification_r18 as instrument
        event_path = store.directory / "residency_events_r18.jsonl"
        probe_path = store.directory / "instrumentation_probe_r18.jsonl"
        try:
            acquired_events = [json.loads(line) for line in event_path.read_text(encoding="utf-8").splitlines() if line]
            acquired_probes = [json.loads(line) for line in probe_path.read_text(encoding="utf-8").splitlines() if line]
            expected_ids = [event_id for event_id in scheduled_ids
                            if plan_by_id[event_id].get("phase") in {"calibration", "residency"}]
            acquired_ids = [row.get("event_id") for row in acquired_events]
            if acquired_ids != expected_ids:
                raise RuntimeError("S25B_acquired_event_order_or_identity_differs_from_hashed_schedule")
            acquired_events = [{**plan_by_id[row["event_id"]], **row} for row in acquired_events]
            qualification = instrument.validate_acquired_rows(acquired_events, acquired_probes, result,
                                                               smoke_n1=diagnostic_mode == "SMOKE_N1")
        except Exception as exc:
            qualification = {"verdict": "HOLD_INSTRUMENTATION_QUALIFICATION", "state": "HOLD",
                             "reason": f"{type(exc).__name__}:{exc}", "numeric_metrics_used": False}
        qual_bytes = _json_bytes({**qualification, "label": "SMOKE", "numeric_metrics_used": False,
                                  "plan_sha256": plan["sha256"],
                                  "schedule_sha256": hashlib.sha256((store.directory / "residency_smoke_schedule_r18.tsv").read_bytes()).hexdigest(),
                                  "s20_validation_sha256": digest})
        qual_sources = [name for name in ("residency_plan_r18.json", "residency_smoke_schedule_r18.tsv",
                         "residency_events_r18.jsonl", "instrumentation_probe_r18.jsonl", "emon_raw.txt",
                         "clock_map.csv", "block_marks_r18.csv", "effective_config_source.json", "events.edp",
                         "session_configuration_r18.json", "phase_preflight_r18.json", "preflight_r18.json")
                        if store._raw_sidecar(name).is_file()]
        qdigest = store.write_derived("instrument_qualification_verdict_r18.json", qual_bytes,
                                      source_raw=qual_sources,
                                      metadata={"evaluator": "S25B-R18", "diagnostic_mode": diagnostic_mode,
                                                "s20_validation_sha256": digest,
                                                "effective_config_sha256": hashlib.sha256((store.directory / "effective_config.json").read_bytes()).hexdigest(),
                                                "numeric_metrics_used": False})
        answer["instrumentation_qualification"] = qualification
        answer["instrumentation_qualification_sha256"] = qdigest
        print(f"S25B_ACQUIRED_R18 verdict={qualification.get('verdict')} state={qualification.get('state')} S20={state}/{s20_mode} sha256={qdigest}")
    if output.parent.resolve() != store.directory:
        raise RuntimeError("D13_status_output_must_be_inside_attempt")
    status_name = output.name
    store.write_derived(status_name, _json_bytes(answer),
                        source_raw=[name for name in raw_names if name in {
                            "residency_plan_r18.json", "residency_smoke_schedule_r18.tsv", "events.edp",
                            "emon_raw.txt", "preflight_r18.json"}],
                        metadata={"role": "SMOKE pipeline result", "numeric_metrics_used": False})
    print(f"HARNESS_S20_R18 state={state} reason={reason} samples={answer['samples']} intervals={answer['emon_interval_groups']} clock_rows={answer['clock_map_rows']} raw_write_once=PASS validation_sha256={digest}")
    return answer


def self_test(output: Path) -> None:
    validator = Path(__file__).resolve().parents[1] / "collector" / "qualify_r18.py"
    from subprocess import run
    result = run([sys.executable, "-B", str(validator), "--self-test",
                  "--self-test-output", str(output)], cwd=ROOT, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f"shared_S20_D13_fixture_failed:{result.stderr.strip()}")
    print(result.stdout.strip())
    compact_fixture = _compact_s20_result({"state": "VALID", "results": [{
        "state": "VALID", "metrics": {"window_metrics": [
            {"state": "VALID", "activity_factor_a_i_k": .99},
            {"state": "VALID", "activity_factor_a_i_k": 1.01}]},
        "window_statuses": [{"state": "VALID", "metrics": {"activity_factor_a_i_k": .99}},
                            {"state": "VALID", "metrics": {"activity_factor_a_i_k": 1.01}}]}]})
    compact_row = compact_fixture["results"][0]
    compact_windows = compact_row["metrics"]["list_summaries"]["window_metrics"]
    if ("window_metrics" in compact_row["metrics"] or compact_windows.get("count") != 2 or
            compact_windows.get("numeric_ranges", {}).get("activity_factor_a_i_k", {}).get("min") != .99 or
            compact_row.get("window_summary", {}).get("count") != 2):
        raise RuntimeError("S20_compact_window_summary_fixture_invalid")
    print("TELEMETRY_S20_COMPACT_RESULT_R18 PASS full_validation_before_summary_window_counts_and_extrema=true")
    import instrument_qualification_r18 as instrument
    plan = plans.build_payload()
    smoke_rows = plans.smoke_schedule(plan)
    normal_lines = [f"#plan_sha256={plan['sha256']}", "#diagnostic=SMOKE_ONLY",
                    "sequence\tevent_id\tphase\tblock_id\tatomic_group_id\tsegment_id"]
    normal_lines.extend("\t".join(str(row.get(key, "")) for key in
                        ("sequence", "id", "phase", "block_id", "atomic_group_id", "segment_id"))
                        for row in smoke_rows)
    normal_budget = capture_budget(("\n".join(normal_lines)+"\n").encode("utf-8"))
    if normal_budget["schedule_mode"] != "N3_ACQUISITION_CHAIN" or normal_budget["schedule_events"] != 264 or normal_budget["estimated_max_raw_bytes"] >= 1024**3:
        raise RuntimeError(f"N3_capture_budget_fixture_invalid:{normal_budget}")
    print("TELEMETRY_N3_CAPTURE_BUDGET_R18 PASS events=264 max_attempt_bytes_below_1GiB")
    segment_replicas: dict[str, int] = {}
    for event in smoke_rows:
        if event["phase"] == "residency":
            segment, replica = str(event["segment_id"]), int(event["replica"])
            if segment in segment_replicas and segment_replicas[segment] != replica:
                raise RuntimeError(f"D10_probe_segment_replica_fixture_invalid:{segment}")
            segment_replicas[segment] = replica
    probe_keys = _s25_probe_expected_keys(segment_replicas)
    if (segment_replicas != {"R01": 1, "R02": 2, "R03": 3} or len(probe_keys) != 186 or
            len({(segment, endpoint) for segment, endpoint, _replica, _sample in probe_keys}) != 6):
        raise RuntimeError(f"D10_S25_probe_plan_mapping_fixture_invalid:{segment_replicas}:{len(probe_keys)}")
    print("TELEMETRY_D10_S25_PROBE_MAPPING_R18 PASS segment_replica=plan samples_per_endpoint=31 probe_rows=186 S22_core_rows=1488")
    for smoke_n1, label, expected_events in ((True, "SMOKE_N1", 6), (False, "FULL_36_BLOCKS", 48)):
        budget = capture_budget(instrument.build_schedule(plan, smoke_n1=smoke_n1))
        if budget["schedule_mode"] != label or budget["schedule_events"] != expected_events or budget["estimated_max_raw_bytes"] >= 1024**3:
            raise RuntimeError(f"S25B_capture_budget_fixture_invalid:{label}:{budget}")
    print("TELEMETRY_S25B_CAPTURE_BUDGET_R18 PASS N1_events=6 full_events=48 max_attempt_bytes_below_1GiB")
    sized_edp = _size_edp_loop("-q -c -v -experimental -t0.050 -l3600\n-C (\nX\n)\n", 20573)
    if "-l20573" not in sized_edp or "-l3600" in sized_edp:
        raise RuntimeError("EMON_inline_loop_limit_rewrite_fixture_failed")
    print("TELEMETRY_EDP_LOOP_BUDGET_R18 PASS inline_options_line -l3600 -> -l20573")
    print("TELEMETRY_R18_QA PASS governing_validator=T0P/collector/s20_validator_r18.py mode=full D13=write-once")


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
        print(f"TELEMETRY_R18 REJECT {type(exc).__name__}: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
