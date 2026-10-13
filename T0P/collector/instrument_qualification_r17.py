"""Frozen S-25-B qualification schedule and raw-evidence gate for T0P r17."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

import residency_plan_r17 as plans


class QualificationError(ValueError):
    pass


VARIANTS = ("A", "B", "C")
CONDITIONS = ((1, 1), (1, 16), (16, 1), (16, 16))
SCHEDULE_HEADER = "sequence\tevent_id\tphase\tblock_id\tatomic_group_id\tsegment_id"


def expected_events(plan: dict[str, Any], *, smoke_n1: bool) -> list[dict[str, Any]]:
    events = [row for row in plan["events"] if row.get("phase") in {"calibration", "residency"}
              and row.get("W_target_kib") == 288 and row.get("O") == 1024]
    if smoke_n1:
        events = [row for row in events if
                  (row["phase"] == "calibration" and row.get("m") == 1 and row.get("K") == 1) or
                  (row["phase"] == "residency" and row.get("m") == 1 and row.get("K") == 1
                   and row.get("replica") == 1)]
    else:
        events = [row for row in events if
                  (row["phase"] == "calibration" and row.get("m") in (1, 16) and row.get("K") in (1, 16)) or
                  (row["phase"] == "residency" and row.get("m") in (1, 16) and row.get("K") in (1, 16)
                   and row.get("replica") in (1, 2, 3))]
    return sorted(events, key=lambda row: row["sequence"])


def verify_event_contract(events: list[dict[str, Any]], *, smoke_n1: bool) -> None:
    calibrations = [row for row in events if row["phase"] == "calibration"]
    residency = [row for row in events if row["phase"] == "residency"]
    expected_calibrations = 3 if smoke_n1 else 12
    expected_residency = 3 if smoke_n1 else 36
    if len(calibrations) != expected_calibrations or len(residency) != expected_residency:
        raise QualificationError("S25B_event_multiplicity_invalid")
    calibration_units: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in calibrations:
        calibration_units[row["unit_id"]].append(row)
    wanted_units = {"W288|O1024|m1|K1"} if smoke_n1 else {
        f"W288|O1024|m{m}|K{k}" for m, k in CONDITIONS
    }
    if set(calibration_units) != wanted_units:
        raise QualificationError("S25B_calibration_unit_coverage_invalid")
    for unit, rows in calibration_units.items():
        if [row["variant"] for row in rows] != list(VARIANTS):
            raise QualificationError(f"S25B_calibration_A_B_C_not_contiguous:{unit}")
    groups: dict[tuple[int, int, int], list[dict[str, Any]]] = defaultdict(list)
    for row in residency:
        if row["W_target_kib"] != 288 or row["O"] != 1024 or row["variant"] not in VARIANTS:
            raise QualificationError("S25B_residency_geometry_invalid")
        groups[(row["replica"], row["m"], row["K"])].append(row)
    wanted = {(1, 1, 1)} if smoke_n1 else {
        (replica, m, k) for replica in (1, 2, 3) for m, k in CONDITIONS
    }
    if set(groups) != wanted:
        raise QualificationError("S25B_condition_replica_coverage_invalid")
    for key, rows in groups.items():
        if [row["variant"] for row in rows] != list(VARIANTS):
            raise QualificationError(f"S25B_A_B_C_triplet_or_order_invalid:{key}")
        if len({row["block_id"] for row in rows}) != 1 or len({row["atomic_group_id"] for row in rows}) != 1:
            raise QualificationError(f"S25B_A_B_C_triplet_split:{key}")


def build_schedule(plan: dict[str, Any], *, smoke_n1: bool) -> bytes:
    events = expected_events(plan, smoke_n1=smoke_n1)
    verify_event_contract(events, smoke_n1=smoke_n1)
    label = "SMOKE_N1" if smoke_n1 else "FULL_36_BLOCKS"
    lines = [f"#plan_sha256={plan['sha256']}",
             f"#diagnostic=INSTRUMENT_QUALIFICATION_R17;mode={label}",
             "#condition_order=source_plan_order;replica_major;within_block_seeded_order_frozen",
             "#calibration=R(W288,m,K)_common_A_B_C;target_region_s=75;minimum_region_s=60",
             "#conditioning_seconds=180;no_fill=true;retry_until_pass=false", SCHEDULE_HEADER]
    for row in events:
        lines.append("\t".join(str(value) for value in (
            row["sequence"], row["id"], row["phase"], row["block_id"],
            row["atomic_group_id"], row.get("segment_id", ""))))
    return ("\n".join(lines) + "\n").encode("utf-8")


def verify_schedule(plan_path: Path, schedule_path: Path) -> tuple[dict[str, Any], list[dict[str, Any]], bool]:
    plan = plans.verify_plan(plan_path)
    content = schedule_path.read_bytes()
    lines = content.decode("utf-8").splitlines()
    smoke_n1 = "#diagnostic=INSTRUMENT_QUALIFICATION_R17;mode=SMOKE_N1" in lines
    full = "#diagnostic=INSTRUMENT_QUALIFICATION_R17;mode=FULL_36_BLOCKS" in lines
    if smoke_n1 == full or f"#plan_sha256={plan['sha256']}" not in lines or SCHEDULE_HEADER not in lines:
        raise QualificationError("S25B_schedule_header_or_plan_hash_invalid")
    start = lines.index(SCHEDULE_HEADER) + 1
    actual_ids: list[str] = []
    for line in lines[start:]:
        fields = line.split("\t")
        if len(fields) != 6 or int(fields[0]) <= 0:
            raise QualificationError("S25B_schedule_row_invalid")
        actual_ids.append(fields[1])
    expected = expected_events(plan, smoke_n1=smoke_n1)
    if actual_ids != [row["id"] for row in expected]:
        raise QualificationError("S25B_schedule_not_exact_plan_subset_or_order")
    verify_event_contract(expected, smoke_n1=smoke_n1)
    return plan, expected, smoke_n1


def _finite(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def validate_acquired_rows(events: list[dict[str, Any]], probe_rows: list[dict[str, Any]],
                           s20: dict[str, Any], *, smoke_n1: bool) -> dict[str, Any]:
    """Gate acquired event durations, common R, per-window frequency and probe cache limits."""
    try:
        verify_event_contract(events, smoke_n1=smoke_n1)
        event_r: dict[tuple[int, int], set[int]] = defaultdict(set)
        event_ids: set[str] = set()
        residency_ids: set[str] = set()
        for row in events:
            phase = row.get("phase")
            if phase not in {"calibration", "residency"}:
                continue
            eid = row.get("event_id")
            if not isinstance(eid, str) or not eid:
                raise QualificationError("S25B_acquired_event_id_missing")
            event_ids.add(eid)
            r = row.get("R")
            if type(r) is not int or r <= 0:
                raise QualificationError(f"S25B_R_invalid:{eid}")
            event_r[(int(row.get("m", 0)), int(row.get("K", 0)))].add(r)
            if phase == "calibration":
                if row.get("R_policy") != "R(W,m,K)_common_A_B_C_fixed_across_S25B_replicas" or row.get("R_unit_id") != row.get("unit_id"):
                    raise QualificationError(f"S25B_calibration_R_provenance_invalid:{eid}")
            else:
                residency_ids.add(eid)
                duration = row.get("condition_region_seconds")
                if not _finite(duration) or duration < 60.0:
                    raise QualificationError(f"S25B_region_below_60_seconds:{eid}")
                if row.get("R_unit_id") != f"W288|O1024|m{row.get('m')}|K{row.get('K')}":
                    raise QualificationError(f"S25B_residency_R_unit_invalid:{eid}")
        if any(len(values) != 1 for values in event_r.values()):
            raise QualificationError("S25B_R_not_common_by_m_K")
        expected_s20_mode = "temporal" if smoke_n1 else "full"
        if s20.get("state") != "VALID" or s20.get("mode") != expected_s20_mode:
            raise QualificationError(f"S25B_S20_{expected_s20_mode}_capture_not_valid")
        results = s20.get("results")
        if not isinstance(results, list):
            raise QualificationError("S25B_S20_results_missing")
        by_sample_channel_core: dict[tuple[str, str, int], list[dict[str, Any]]] = defaultdict(list)
        for row in results:
            sample = row.get("sample", {})
            sid = sample.get("sample_id")
            channel = row.get("channel")
            core = row.get("core")
            if isinstance(sid, str) and isinstance(channel, str) and type(core) is int:
                by_sample_channel_core[(sid, channel, core)].append(row)
        for eid in residency_ids:
            for core in range(8):
                rows = by_sample_channel_core.get((eid, "THREAD_REF_TSC", core), [])
                if len(rows) != 1 or rows[0].get("state") != "VALID":
                    raise QualificationError(f"S25B_event_thread_ref_invalid:{eid}:core{core}")
                windows = rows[0].get("metrics", {}).get("window_metrics", [])
                factors = [item.get("activity_factor_a_i_k") for item in windows]
                if not factors or any(not _finite(value) or not .98 <= value <= 1.02 for value in factors):
                    raise QualificationError(f"S25B_event_frequency_excursion_gt_2_percent:{eid}:core{core}")
        if not probe_rows:
            raise QualificationError("S25B_instrumentation_probe_raw_missing")
        valid_windows = 0
        for probe in probe_rows:
            sid = probe.get("event_id")
            if not isinstance(sid, str) or not probe.get("load_only") or probe.get("weights_refreshed_after_warmup") is not False:
                raise QualificationError("S25B_probe_identity_or_load_only_invalid")
            for core in range(8):
                thread = by_sample_channel_core.get((sid, "THREAD_REF_TSC", core), [])
                l2 = by_sample_channel_core.get((sid, "L2_DEMAND_DATA_READ", core), [])
                llc = by_sample_channel_core.get((sid, "LLC_DEMAND_DATA_READ", core), [])
                if len(thread) != 1 or len(l2) != 1 or len(llc) != 1 or any(
                        row.get("state") != "VALID" for row in (thread[0], l2[0], llc[0])):
                    raise QualificationError(f"S25B_probe_channel_or_core_missing:{sid}:core{core}")
                windows = thread[0].get("metrics", {}).get("window_metrics", [])
                if not windows:
                    raise QualificationError(f"S25B_probe_frequency_windows_missing:{sid}:core{core}")
                factors = [item.get("activity_factor_a_i_k") for item in windows]
                if any(not _finite(value) or not .98 <= value <= 1.02 for value in factors):
                    raise QualificationError(f"S25B_probe_frequency_excursion_gt_2_percent:{sid}:core{core}")
                l2m, llcm = l2[0].get("metrics", {}), llc[0].get("metrics", {})
                d2 = l2m.get("l2_denominator")
                m2, m3 = l2m.get("l2_numerator"), llcm.get("llc_numerator")
                if any(type(value) is not int for value in (d2, m2, m3)) or d2 < 10_000:
                    raise QualificationError(f"S25B_probe_D2_below_10000_or_invalid:{sid}:core{core}")
                if m2 / d2 > .10 or m3 / d2 > .10:
                    raise QualificationError(f"S25B_probe_H2_H3_gate_failed:{sid}:core{core}")
                valid_windows += len(windows)
        return {"verdict": "INSTRUMENTATION_QUALIFICATION_PASS",
                "state": "SMOKE_N1_ONLY" if smoke_n1 else "QUALIFIED",
                "residency_events": len(residency_ids), "probe_endpoint_blocks": len(probe_rows),
                "valid_frequency_windows": valid_windows, "R_common_by_m_K": True,
                "S20_mode": expected_s20_mode, "numeric_metrics_used": False}
    except (QualificationError, KeyError, TypeError, ValueError, ZeroDivisionError) as exc:
        return {"verdict": "HOLD_INSTRUMENTATION_QUALIFICATION", "state": "HOLD",
                "reason": str(exc), "numeric_metrics_used": False}


def _synthetic_s20(events: list[dict[str, Any]], probes: list[dict[str, Any]], *, mode: str = "full") -> dict[str, Any]:
    result_rows: list[dict[str, Any]] = []
    for event in events:
        for core in range(8):
            result_rows.append({"sample": {"sample_id": event["event_id"]}, "channel": "THREAD_REF_TSC",
                                "core": core, "state": "VALID", "metrics": {"window_metrics": [
                                    {"activity_factor_a_i_k": 1.0}]}})
    for probe in probes:
        for core in range(8):
            result_rows.extend([
                {"sample": {"sample_id": probe["event_id"]}, "channel": "THREAD_REF_TSC", "core": core,
                 "state": "VALID", "metrics": {"window_metrics": [{"activity_factor_a_i_k": 1.0}]}},
                {"sample": {"sample_id": probe["event_id"]}, "channel": "L2_DEMAND_DATA_READ", "core": core,
                 "state": "VALID", "metrics": {"l2_numerator": 100, "l2_denominator": 20_000}},
                {"sample": {"sample_id": probe["event_id"]}, "channel": "LLC_DEMAND_DATA_READ", "core": core,
                 "state": "VALID", "metrics": {"llc_numerator": 20, "llc_denominator": 20_000}},
            ])
    return {"state": "VALID", "mode": mode, "results": result_rows}


def self_test(output: Path) -> None:
    plan = plans.build_payload()
    smoke_events = expected_events(plan, smoke_n1=True)
    if len(smoke_events) != 6 or len(expected_events(plan, smoke_n1=False)) != 48:
        raise QualificationError("S25B_schedule_fixture_counts_invalid")
    verify_event_contract(smoke_events, smoke_n1=True)
    acquired = []
    for row in smoke_events:
        item = dict(row, event_id=row["id"], R=128, R_unit_id=row["unit_id"])
        if row["phase"] == "calibration":
            item.update(R_policy="R(W,m,K)_common_A_B_C_fixed_across_S25B_replicas")
        else:
            item.update(R_unit_id=f"W288|O1024|m{row['m']}|K{row['K']}", condition_region_seconds=75.0)
        acquired.append(item)
    probes = [{"event_id": f"probe-{sample}", "load_only": True,
               "weights_refreshed_after_warmup": False} for sample in range(62)]
    good = validate_acquired_rows(acquired, probes, _synthetic_s20(acquired, probes, mode="temporal"), smoke_n1=True)
    if good.get("verdict") != "INSTRUMENTATION_QUALIFICATION_PASS":
        raise QualificationError(f"S25B_positive_fixture_failed:{good}")
    cases = ["positive_N1=PASS"]
    mutations = []
    altered = [dict(row) for row in acquired]
    altered[-1]["R"] = 256
    mutations.append(("R_mismatch", altered, "S25B_R_not_common_by_m_K"))
    altered = [dict(row) for row in acquired]
    next(row for row in altered if row["phase"] == "residency")["condition_region_seconds"] = 59.9
    mutations.append(("region_59_9s", altered, "S25B_region_below_60_seconds"))
    altered_s20 = _synthetic_s20(acquired, probes, mode="temporal")
    first_residency = next(row["event_id"] for row in acquired if row["phase"] == "residency")
    next(row for row in altered_s20["results"] if row["channel"] == "THREAD_REF_TSC" and
         row["sample"]["sample_id"] == first_residency)["metrics"]["window_metrics"][0]["activity_factor_a_i_k"] = 1.031
    mutations.append(("frequency_excursion_3_1pct", acquired, "S25B_event_frequency_excursion_gt_2_percent", altered_s20))
    altered_s20 = _synthetic_s20(acquired, probes, mode="temporal")
    next(row for row in altered_s20["results"] if row["channel"] == "L2_DEMAND_DATA_READ")["metrics"]["l2_denominator"] = 9_999
    mutations.append(("D2_9999", acquired, "S25B_probe_D2_below_10000_or_invalid", altered_s20))
    for case in mutations:
        name, rows, reason, *custom = case
        s20 = custom[0] if custom else _synthetic_s20(rows, probes, mode="temporal")
        result = validate_acquired_rows(rows, probes, s20, smoke_n1=True)
        if result.get("verdict") != "HOLD_INSTRUMENTATION_QUALIFICATION" or reason not in result.get("reason", ""):
            raise QualificationError(f"S25B_mutation_not_rejected:{name}:{result}")
        cases.append(f"{name}=HOLD")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("S25B_INSTRUMENT_QUALIFICATION_R17_QA PASS\n" + "\n".join(cases) + "\n",
                      encoding="utf-8", newline="\n")
    print(f"S25B_INSTRUMENT_QUALIFICATION_R17_QA PASS synthetic_cases={len(cases)} full_blocks=36 smoke_N1_blocks=1 evidence={output}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest="command", required=True)
    schedule = subs.add_parser("schedule")
    schedule.add_argument("--plan", type=Path, required=True)
    schedule.add_argument("--output", type=Path, required=True)
    schedule.add_argument("--smoke-n1", action="store_true")
    verify = subs.add_parser("verify")
    verify.add_argument("--plan", type=Path, required=True)
    verify.add_argument("--schedule", type=Path, required=True)
    qa = subs.add_parser("self-test")
    qa.add_argument("--output", type=Path, required=True)
    ns = parser.parse_args(argv)
    try:
        if ns.command == "schedule":
            plan = plans.verify_plan(ns.plan)
            payload = build_schedule(plan, smoke_n1=ns.smoke_n1)
            ns.output.parent.mkdir(parents=True, exist_ok=True)
            if ns.output.exists():
                raise QualificationError("S25B_schedule_write_once_refused")
            ns.output.write_bytes(payload)
            print(f"S25B_SCHEDULE_R17 PASS mode={'SMOKE_N1' if ns.smoke_n1 else 'FULL_36_BLOCKS'} events={len(payload.decode().splitlines())-6} sha256={hashlib.sha256(payload).hexdigest()} path={ns.output}")
        elif ns.command == "verify":
            plan, events, smoke_n1 = verify_schedule(ns.plan, ns.schedule)
            print(f"S25B_SCHEDULE_R17_VERIFY PASS mode={'SMOKE_N1' if smoke_n1 else 'FULL_36_BLOCKS'} events={len(events)} plan_sha256={plan['sha256']}")
        else:
            self_test(ns.output)
        return 0
    except (OSError, plans.PlanError, QualificationError, json.JSONDecodeError, KeyError, TypeError, ValueError) as exc:
        print(f"S25B_INSTRUMENT_QUALIFICATION_R17 REJECT {type(exc).__name__}: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
