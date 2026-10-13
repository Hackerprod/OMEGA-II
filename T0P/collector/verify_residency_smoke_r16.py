#!/usr/bin/env python3
"""Stream-verify D9/D8/D13 invariants of a completed R16 SMOKE attempt."""
from __future__ import annotations

import argparse
import csv
import json
import math
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Iterable

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "T0P" / "validator"))
from artifact_store import SessionAttemptStore  # type: ignore

RAW_FILES = (
    "residency_plan_r16.json", "residency_smoke_schedule_r16.tsv", "events.edp", "emon_raw.txt", "emon_stderr.txt",
    "harness_stdout.txt", "harness_stderr.txt", "clock_map.csv", "block_marks_r16.csv",
    "residency_events_r16.jsonl", "residency_episodes_r16.jsonl", "residency_calibration_trials_r16.jsonl",
    "residency_margins_r16.jsonl", "residency_margin_episodes_r16.jsonl", "eviction_manifest_r16.jsonl", "preflight_r16.json",
    "effective_config_source.json",
)


class SmokeVerifyError(RuntimeError):
    pass


def rows(path: Path) -> Iterable[dict[str, Any]]:
    with path.open("r", encoding="utf-8") as handle:
        for number, line in enumerate(handle, 1):
            if not line.strip():
                continue
            try:
                value = json.loads(line)
            except json.JSONDecodeError as exc:
                raise SmokeVerifyError(f"invalid_JSON:{path.name}:line={number}:{exc}") from exc
            if not isinstance(value, dict):
                raise SmokeVerifyError(f"JSON_row_not_object:{path.name}:line={number}")
            yield value


def positive_number(value: Any) -> bool:
    return (isinstance(value, (int, float)) and not isinstance(value, bool)
            and math.isfinite(value) and value > 0)


def verify_timed_rows(paths: list[Path], k_expected: int) -> int:
    checked = 0
    for path in paths:
        for row in rows(path):
            eid = row.get("event_id")
            q0, q1 = row.get("qpc_episode_start"), row.get("qpc_episode_end")
            t0, t1 = row.get("start_tsc"), row.get("end_tsc")
            apps = row.get("applications")
            if (not isinstance(eid, str) or not eid
                    or not isinstance(q0, int) or isinstance(q0, bool)
                    or not isinstance(q1, int) or isinstance(q1, bool) or q1 <= q0
                    or not isinstance(t0, int) or isinstance(t0, bool)
                    or not isinstance(t1, int) or isinstance(t1, bool) or t1 <= t0
                    or not isinstance(apps, list) or len(apps) != k_expected):
                raise SmokeVerifyError(f"episode_timestamp_or_K_invalid:{path.name}:{eid}")
            for app in apps:
                if not isinstance(app, dict):
                    raise SmokeVerifyError(f"application_stamp_not_object:{eid}")
                ts = (app.get("start_tsc"), app.get("end_tsc"), app.get("start_qpc"), app.get("end_qpc"))
                if (any(not isinstance(v, int) or isinstance(v, bool) for v in ts)
                        or ts[1] <= ts[0] or ts[3] <= ts[2] or not positive_number(app.get("t_app_us"))):
                    raise SmokeVerifyError(f"application_timestamp_invalid:{eid}")
            checked += 1
    return checked


def verify(attempt: Path) -> dict[str, Any]:
    store = SessionAttemptStore(attempt)
    meta = json.loads((store.directory / "attempt_metadata.json").read_text(encoding="utf-8"))
    if meta.get("protocol") != "D13-write-once-v1":
        raise SmokeVerifyError("D13_protocol_missing")
    for name in RAW_FILES:
        if not (store.directory / name).is_file():
            raise SmokeVerifyError(f"raw_missing:{name}")
        store.verify_raw(name)
    if not (store.directory / "s20_validation_r16.json").is_file():
        raise SmokeVerifyError("S20_derived_result_missing")

    with (store.directory / "residency_smoke_schedule_r16.tsv").open("r", encoding="utf-8", newline="") as handle:
        content = handle.read().splitlines()
    header = next((line for line in content if line and not line.startswith("#")), None)
    if header is None:
        raise SmokeVerifyError("schedule_header_missing")
    schedule = list(csv.DictReader([line for line in content if line and not line.startswith("#")], delimiter="\t"))
    expected_ids = {row["event_id"] for row in schedule}
    if len(schedule) != 18 or len(expected_ids) != 18:
        raise SmokeVerifyError("schedule_event_count_or_ID_uniqueness_invalid")

    event_rows = list(rows(store.directory / "residency_events_r16.jsonl"))
    event_ids = [row.get("event_id") for row in event_rows]
    if len(event_rows) != 18 or len(set(event_ids)) != 18 or set(event_ids) != expected_ids:
        raise SmokeVerifyError("event_summary_union_mismatch")
    phases = Counter(row.get("phase") for row in event_rows)
    if phases != Counter({"calibration": 3, "residency": 9, "aa": 6}):
        raise SmokeVerifyError(f"smoke_phase_counts_invalid:{dict(phases)}")
    if any(row.get("label") != "SMOKE" or row.get("numeric_metrics_used") is not False for row in event_rows):
        raise SmokeVerifyError("smoke_numeric_use_label_invalid")
    if {(row.get("O"), row.get("m")) for row in event_rows} != {(1024, 1)}:
        raise SmokeVerifyError("smoke_MAC_geometry_invalid")

    calibration = [row for row in event_rows if row.get("phase") == "calibration"]
    if len({row.get("R") for row in calibration}) != 1 or len({row.get("unit_id") for row in calibration}) != 1:
        raise SmokeVerifyError("R_calibration_not_common_across_A_B_C")
    r_values = {row.get("R") for row in calibration}
    if len(r_values) != 1 or not isinstance(next(iter(r_values)), int):
        raise SmokeVerifyError("R_calibration_invalid")
    common_r = next(iter(r_values))
    if any(row.get("R") != common_r for row in event_rows if row.get("phase") in {"residency", "aa"}):
        raise SmokeVerifyError("R_sample_differs_from_common_calibration")

    a_rows = {(row.get("W_target_kib"), row.get("O"), row.get("m"), row.get("K"), row.get("replica")): row for row in event_rows
              if row.get("phase") == "residency" and row.get("variant") == "A"}
    c_rows = {(row.get("W_target_kib"), row.get("O"), row.get("m"), row.get("K"), row.get("replica")): row for row in event_rows
              if row.get("phase") == "residency" and row.get("variant") == "C"}
    if set(a_rows) != set(c_rows) or len(a_rows) != 3:
        raise SmokeVerifyError("A_C_sample_pair_coverage_invalid")
    ac_workers = 0
    for key in a_rows:
        a, c = a_rows[key], c_rows[key]
        if a.get("region_instance_id") != c.get("region_instance_id"):
            raise SmokeVerifyError(f"A_C_region_instance_mismatch:{key}")
        ar = {x.get("worker"): x for x in a.get("weight_regions", [])}
        cr = {x.get("worker"): x for x in c.get("weight_regions", [])}
        if set(ar) != set(range(8)) or set(cr) != set(range(8)):
            raise SmokeVerifyError(f"A_C_worker_region_coverage_invalid:{key}")
        for worker in range(8):
            for region in (ar[worker], cr[worker]):
                if (not isinstance(region.get("allocation_id"), str) or not region["allocation_id"]
                        or not isinstance(region.get("address"), int) or isinstance(region.get("address"), bool)
                        or region["address"] <= 0 or not isinstance(region.get("sha256"), str)
                        or len(region["sha256"]) != 64 or not positive_number(region.get("bytes"))):
                    raise SmokeVerifyError(f"A_C_region_identity_fields_invalid:{key}:w{worker}")
            if any(ar[worker].get(field) != cr[worker].get(field)
                   for field in ("allocation_id", "address", "sha256", "bytes")):
                raise SmokeVerifyError(f"A_C_live_address_or_hash_mismatch:{key}:w{worker}")
            ac_workers += 1

    for row in event_rows:
        if row.get("phase") == "calibration":
            continue
        if (row.get("timestamps_complete") is not True or row.get("margin_continuous") is not True
                or row.get("margin_condition_id") != row.get("condition_id")
                or not positive_number(row.get("pre_margin_seconds"))
                or not positive_number(row.get("post_margin_seconds"))
                or row["pre_margin_seconds"] < .200 or row["post_margin_seconds"] < .200
                or row.get("qpc_pad_start", 0) >= row.get("qpc_start", 0)
                or row.get("qpc_pad_end", 0) <= row.get("qpc_end", 0)):
            raise SmokeVerifyError(f"margin_or_timestamp_block_invalid:{row.get('event_id')}")

    timed_count = verify_timed_rows([
        store.directory / "residency_episodes_r16.jsonl",
        store.directory / "residency_calibration_trials_r16.jsonl",
        store.directory / "residency_margin_episodes_r16.jsonl",
    ], k_expected=16)
    manifest_count = 0
    weight_calls = activation_calls = total_calls = 0
    for row in rows(store.directory / "eviction_manifest_r16.jsonl"):
        if row.get("primitive") != "omega_evict::evict_range_record" or row.get("call_counts_match") is not True:
            raise SmokeVerifyError(f"D8_manifest_primitive_or_counter_invalid:{row.get('event_id')}")
        manifest_count += 1
        weight_calls += int(row.get("weight_range_calls", -1))
        activation_calls += int(row.get("activation_range_calls", -1))
        total_calls += int(row.get("range_calls_total", -1))
    # Successful calibration episodes are copied from the trial stream into
    # the final episode stream; all other episode records map one-to-one.
    expected_manifest_min = timed_count - 3 * common_r
    if manifest_count < expected_manifest_min or total_calls != weight_calls + activation_calls:
        raise SmokeVerifyError("D8_manifest_episode_coverage_or_sum_invalid")

    marks = list(csv.DictReader((store.directory / "block_marks_r16.csv").open("r", encoding="utf-8-sig", newline="")))
    mark_counts = Counter(mark.get("sample_id") for mark in marks)
    measured_ids = {row["event_id"] for row in event_rows if row.get("phase") in {"residency", "aa"}}
    if set(mark_counts) != measured_ids or any(mark_counts[eid] != 8 for eid in measured_ids):
        raise SmokeVerifyError("S20_block_mark_coverage_invalid")
    preflight = json.loads((store.directory / "preflight_r16.json").read_text(encoding="utf-8"))
    if preflight.get("plan_sha256") != meta.get("plan_sha256") and preflight.get("plan_sha256") != json.loads((store.directory / "residency_plan_r16.json").read_text(encoding="utf-8")).get("sha256"):
        raise SmokeVerifyError("preflight_plan_hash_mismatch")
    harness_stdout = (store.directory / "harness_stdout.txt").read_text(encoding="utf-8")
    if "MAC_CHECK real_work=512*O*m value=524288 PASS" not in harness_stdout:
        raise SmokeVerifyError("MAC_512_O_mismatch")
    for marker in ("SMOKE_R16_CAPTURE PASS", "D9_AC_SHARED same_live_buffer=true", "D9_EPISODES all_timestamps=true",
                   "D8_EVICT primitive=omega_evict::evict_range_record only"):
        if marker not in harness_stdout:
            raise SmokeVerifyError(f"harness_completion_marker_missing:{marker}")
    s20 = json.loads((store.directory / "s20_validation_r16.json").read_text(encoding="utf-8"))
    return {"schedule_events": len(schedule), "event_summaries": len(event_rows), "common_R": common_r,
            "timed_episode_records": timed_count, "D8_manifest_episodes": manifest_count,
            "D8_weight_calls": weight_calls, "D8_activation_calls": activation_calls,
            "D8_total_calls": total_calls, "A_C_worker_region_pairs": ac_workers,
            "marks": len(marks), "S20_state": s20.get("state"), "numeric_metrics_used": False,
            "D13_raws_verified": len(RAW_FILES)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--attempt", type=Path, required=True)
    ns = parser.parse_args()
    try:
        result = verify(ns.attempt)
        print("RESIDENCY_SMOKE_R16_VERIFY PASS " + " ".join(f"{key}={value}" for key, value in result.items()))
        return 0
    except Exception as exc:
        print(f"RESIDENCY_SMOKE_R16_VERIFY REJECT {type(exc).__name__}: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
