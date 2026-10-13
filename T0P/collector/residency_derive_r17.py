#!/usr/bin/env python3
"""Strict S-19 residency derivation: exact IDs, S-20 controls, D9 episode trace, D13 write-once."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import statistics
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Callable

import residency_plan_r17 as planlib
from artifact_store_r17 import ArtifactError, SessionAttemptStore
from qualify_r17 import telemetry_valid
import s20_validator_r17 as s20_validator

M_VALUES = (1, 4, 8, 16)
VARIANTS = ("A", "B", "C")
W_GRID = (256, 288, 512, 768, 1024, 1280, 1536, 1792,
          2048, 2560, 3072, 4096, 6144, 8192, 12288, 16384)
LP_BY_WORKER = tuple(range(0, 16, 2))
DEFAULT_EVIDENCE = Path(__file__).resolve().parent / "logs_r17" / "obj_r17" / "residency_deriver_qa_r17.txt"


class DeriveError(ValueError):
    pass


def _strict_pairs(pairs):
    out = {}
    for key, value in pairs:
        if key in out:
            raise DeriveError(f"duplicate_json_key:{key}")
        out[key] = value
    return out


def _strict_loads(line: str):
    return json.loads(line, object_pairs_hook=_strict_pairs,
                      parse_constant=lambda v: (_ for _ in ()).throw(DeriveError(f"nonfinite:{v}")))


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    rows = []
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        for number, line in enumerate(stream, 1):
            if not line.strip():
                raise DeriveError(f"blank_jsonl_line:{path.name}:{number}")
            item = _strict_loads(line)
            if not isinstance(item, dict):
                raise DeriveError(f"jsonl_object_required:{path.name}:{number}")
            rows.append(item)
    if not rows:
        raise DeriveError(f"empty_jsonl:{path.name}")
    return rows


def _positive(value: Any) -> bool:
    return (isinstance(value, (int, float)) and not isinstance(value, bool)
            and math.isfinite(value) and value > 0)


def _relative_delta(a: float, b: float) -> float:
    return abs(a - b) / ((a + b) / 2.0)


def _nearest_rank(values: list[float], q: float) -> float:
    if not values:
        return math.nan
    ordered = sorted(values)
    return ordered[max(0, math.ceil(q * len(ordered)) - 1)]


def _expected_plan_fields(event: dict[str, Any]) -> dict[str, Any]:
    return {key: event[key] for key in event if key not in {"sequence", "parallel_event_group", "workers_inside_event", "order_key"}}


def _validate_event_identity(row: dict[str, Any], planned: dict[str, Any]) -> None:
    for key in ("phase", "condition_id", "unit_id", "pair_id", "observation", "variant",
                "replica", "W_target_kib", "O", "m", "K"):
        if key in planned and row.get(key) != planned[key]:
            raise DeriveError(f"event_identity_mismatch:{planned['id']}:{key}")
    if row.get("event_id") != planned["id"]:
        raise DeriveError("event_id_identity_mismatch")


def _index_episode_rows(episode_rows: list[dict[str, Any]], event_rows: dict[str, dict[str, Any]],
                        synthetic: bool) -> dict[str, dict[str, Any]]:
    grouped: dict[str, dict[int, dict[str, Any]]] = defaultdict(dict)
    for row in episode_rows:
        eid = row.get("event_id")
        if not isinstance(eid, str) or eid not in event_rows:
            raise DeriveError(f"episode_event_id_not_executed:{eid}")
        index = row.get("episode_index")
        if isinstance(index, bool) or not isinstance(index, int) or index < 1:
            raise DeriveError(f"episode_index_invalid:{eid}")
        if index in grouped[eid]:
            raise DeriveError(f"episode_timestamp_duplicate:{eid}:episode={index}")
        grouped[eid][index] = row

    aggregates: dict[str, dict[str, Any]] = {}
    for eid, summary in event_rows.items():
        expected_r = summary.get("R")
        if isinstance(expected_r, bool) or not isinstance(expected_r, int) or expected_r < 1:
            raise DeriveError(f"R_invalid:{eid}")
        episodes = grouped.get(eid, {})
        if set(episodes) != set(range(1, expected_r + 1)):
            raise DeriveError(f"episode_timestamps_incomplete:{eid}:expected={expected_r}:actual={len(episodes)}")
        hz = summary.get("f_tsc_hz", 0)
        qpf = summary.get("qpf_hz", 0)
        if not synthetic and (not _positive(hz) or not _positive(qpf)):
            raise DeriveError(f"episode_clock_missing:{eid}")
        expected_k = int(summary.get("K", 0))
        if expected_k < 1:
            raise DeriveError(f"episode_K_missing:{eid}")
        total_us = 0.0
        start_qpc = None
        end_qpc = None
        for episode_index in range(1, expected_r + 1):
            episode = episodes[episode_index]
            apps = episode.get("applications")
            if not isinstance(apps, list) or len(apps) != expected_k:
                raise DeriveError(f"application_episode_count_invalid:{eid}:episode={episode_index}")
            episode_qpc_start, episode_qpc_end = episode.get("qpc_episode_start"), episode.get("qpc_episode_end")
            if not synthetic and (isinstance(episode_qpc_start, bool) or not isinstance(episode_qpc_start, int)
                                  or isinstance(episode_qpc_end, bool) or not isinstance(episode_qpc_end, int)
                                  or episode_qpc_end <= episode_qpc_start):
                raise DeriveError(f"episode_boundary_timestamps_invalid:{eid}:episode={episode_index}")
            episode_us = 0.0
            for app_index, app in enumerate(apps):
                if not isinstance(app, dict):
                    raise DeriveError(f"application_timestamp_schema_invalid:{eid}:episode={episode_index}:app={app_index}")
                reported = app.get("t_app_us")
                workers = app.get("workers")
                if isinstance(workers, list):
                    if len(workers) != 8:
                        raise DeriveError(f"episode_worker_coverage_invalid:{eid}:episode={episode_index}:app={app_index}")
                    by_worker = {worker.get("worker"): worker for worker in workers if isinstance(worker, dict)}
                    if set(by_worker) != set(range(8)) or len(by_worker) != 8:
                        raise DeriveError(f"episode_worker_ids_invalid:{eid}:episode={episode_index}:app={app_index}")
                    tsc_starts, tsc_ends = [], []
                    for worker, stamp in by_worker.items():
                        expected_lp = LP_BY_WORKER[worker]
                        if stamp.get("lp_start") != expected_lp or stamp.get("lp_end") != expected_lp:
                            raise DeriveError(f"episode_LP_migration:{eid}:r{summary.get('replica')}:w{worker}")
                        ts0, ts1 = stamp.get("start_tsc"), stamp.get("end_tsc")
                        q0, q1 = stamp.get("start_qpc"), stamp.get("end_qpc")
                        if any(isinstance(v, bool) or not isinstance(v, int) for v in (ts0, ts1, q0, q1)) or ts1 <= ts0 or q1 <= q0:
                            raise DeriveError(f"episode_timestamp_invalid:{eid}:episode={episode_index}:app={app_index}:w{worker}")
                        if not synthetic:
                            tsc_s = (ts1 - ts0) / float(hz)
                            qpc_s = (q1 - q0) / float(qpf)
                            if abs(tsc_s - qpc_s) > 0.001:
                                raise DeriveError(f"episode_TSC_QPC_disagreement:{eid}:episode={episode_index}:app={app_index}:w{worker}")
                        tsc_starts.append(ts0)
                        tsc_ends.append(ts1)
                    if not synthetic:
                        app_us = (max(tsc_ends) - min(tsc_starts)) * 1e6 / float(hz)
                        if not _positive(reported) or abs(float(reported) - app_us) > max(0.001, app_us * 0.001):
                            raise DeriveError(f"episode_team_time_not_reproducible:{eid}:episode={episode_index}:app={app_index}")
                    else:
                        app_us = float(reported or 1.0)
                        if not _positive(app_us):
                            raise DeriveError(f"synthetic_episode_time_invalid:{eid}")
                elif not synthetic:
                    ts0, ts1 = app.get("start_tsc"), app.get("end_tsc")
                    q0, q1 = app.get("start_qpc"), app.get("end_qpc")
                    if any(isinstance(v, bool) or not isinstance(v, int) for v in (ts0, ts1, q0, q1)) or ts1 <= ts0 or q1 <= q0:
                        raise DeriveError(f"application_timestamp_invalid:{eid}:episode={episode_index}:app={app_index}")
                    tsc_s, qpc_s = (ts1 - ts0) / float(hz), (q1 - q0) / float(qpf)
                    if abs(tsc_s - qpc_s) > 0.001:
                        raise DeriveError(f"application_TSC_QPC_disagreement:{eid}:episode={episode_index}:app={app_index}")
                    app_us = (ts1 - ts0) * 1e6 / float(hz)
                    if not _positive(reported) or abs(float(reported) - app_us) > max(0.001, app_us * 0.001):
                        raise DeriveError(f"application_time_not_reproducible:{eid}:episode={episode_index}:app={app_index}")
                else:
                    app_us = float(reported or 1.0)
                    if not _positive(app_us):
                        raise DeriveError(f"synthetic_episode_time_invalid:{eid}")
                episode_us += app_us
                if app_index == 0:
                    lo = min(stamp.get("start_qpc", 0) for stamp in workers) if isinstance(workers, list) else app.get("start_qpc", 0)
                    if start_qpc is None or lo < start_qpc:
                        start_qpc = lo
                if app_index == expected_k - 1:
                    hi = max(stamp.get("end_qpc", 0) for stamp in workers) if isinstance(workers, list) else app.get("end_qpc", 0)
                    if end_qpc is None or hi > end_qpc:
                        end_qpc = hi
            total_us += episode_us
        aggregates[eid] = {"R": expected_r, "sum_t_app_us": total_us,
                           "mean_t_app_us": total_us / expected_r,
                           "episode_count": expected_r,
                           "episode_index_set": sorted(episodes),
                           "qpc_first": start_qpc, "qpc_last": end_qpc}
    extra = set(grouped) - set(event_rows)
    if extra:
        raise DeriveError(f"orphan_episode_records:{len(extra)}")
    return aggregates


def _check_margin(row: dict[str, Any], event: dict[str, Any]) -> None:
    if event["phase"] == "calibration":
        return
    pre = row.get("pre_margin_seconds")
    post = row.get("post_margin_seconds")
    if not _positive(pre) or not _positive(post) or pre < 0.2 or post < 0.2:
        raise DeriveError(f"same_condition_margin_below_200ms:{event['id']}")
    if row.get("margin_condition_id") != row.get("condition_id") or row.get("margin_continuous") is not True:
        raise DeriveError(f"same_condition_margin_provenance_invalid:{event['id']}")
    for key in ("qpc_pad_start", "qpc_start", "qpc_end", "qpc_pad_end"):
        value = row.get(key)
        if isinstance(value, bool) or not isinstance(value, int):
            raise DeriveError(f"block_timestamp_missing:{event['id']}:{key}")
    if not (row["qpc_pad_start"] < row["qpc_start"] < row["qpc_end"] < row["qpc_pad_end"]):
        raise DeriveError(f"block_timestamp_order_invalid:{event['id']}")


def _check_weight_regions(row: dict[str, Any], event: dict[str, Any]) -> tuple[dict[int, list[dict[str, Any]]], str]:
    regions = row.get("weight_regions")
    if not isinstance(regions, list):
        raise DeriveError(f"weight_regions_missing:{event['id']}")
    by_worker: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for item in regions:
        if not isinstance(item, dict):
            raise DeriveError(f"weight_region_schema_invalid:{event['id']}")
        worker, copy_index = item.get("worker"), item.get("copy_index")
        address, size, digest, allocation_id = (item.get(k) for k in ("address", "bytes", "sha256", "allocation_id"))
        if (isinstance(worker, bool) or not isinstance(worker, int) or worker not in range(8)
                or isinstance(copy_index, bool) or not isinstance(copy_index, int) or copy_index < 0
                or isinstance(address, bool) or not isinstance(address, int) or address <= 0
                or isinstance(size, bool) or not isinstance(size, int) or size != 288 * event["O"]
                or not isinstance(digest, str) or len(digest) != 64
                or not isinstance(allocation_id, str) or not allocation_id):
            raise DeriveError(f"weight_region_invalid:{event['id']}")
        by_worker[worker].append(item)
    expected_count = event["K"] if event["variant"] == "B" else 1
    if set(by_worker) != set(range(8)) or any(len(items) != expected_count for items in by_worker.values()):
        raise DeriveError(f"weight_region_coverage_invalid:{event['id']}")
    all_addresses = []
    for worker, items in by_worker.items():
        items.sort(key=lambda x: x["copy_index"])
        if [x["copy_index"] for x in items] != list(range(expected_count)):
            raise DeriveError(f"weight_region_copy_indices_invalid:{event['id']}:w{worker}")
        if len({x["address"] for x in items}) != len(items):
            raise DeriveError(f"weight_regions_alias_within_worker:{event['id']}:w{worker}")
        if len({x["sha256"] for x in items}) != 1:
            raise DeriveError(f"weight_copy_content_hash_mismatch:{event['id']}:w{worker}")
        all_addresses.extend(x["address"] for x in items)
    if len(set(all_addresses)) != len(all_addresses):
        raise DeriveError(f"weight_regions_alias_across_workers:{event['id']}")
    if row.get("large_pages") is not True or row.get("affinity") != "PASS" or row.get("numerical") != "S12_PASS":
        raise DeriveError(f"residency_cell_physical_control_invalid:{event['id']}")
    macs = 512 * event["O"] * event["m"]
    if row.get("macs_per_call") != macs:
        raise DeriveError(f"MAC_512_O_m_mismatch:{event['id']}")
    combined_identity = hashlib.sha256("|".join(
        f"{w}:{by_worker[w][0]['allocation_id']}:{by_worker[w][0]['address']}:{by_worker[w][0]['sha256']}"
        for w in range(8)).encode("ascii")).hexdigest()
    return by_worker, combined_identity


def derive(plan: dict[str, Any], event_rows: list[dict[str, Any]], episode_rows: list[dict[str, Any]], *,
           telemetry_check: Callable[[dict[str, Any]], tuple[bool, str]], synthetic: bool = False) -> dict[str, Any]:
    if event_rows and all(row.get("label") in {"SMOKE", "SMOKE_ONLY"} for row in event_rows):
        return {"state": "SMOKE_ONLY", "label": "SMOKE", "numeric_metrics_used": False,
                "reason": "smoke_is_not_adjudicable", "rows": len(event_rows),
                "executed_event_ids": [row.get("event_id") for row in event_rows]}
    if not synthetic and any(row.get("label") != "OFFICIAL" for row in event_rows):
        return {"state": "NO_ADJUDICABLE_CONTROLS", "campaign": "HOLD",
                "reason": "mixed_or_nonofficial_labels", "numeric_metrics_used": False}

    # Qualification rows share the frozen plan, but D11 consumes only the
    # residence portion.  The gate has already validated qualification.
    planned_events = [event for event in plan["events"]
                      if event.get("phase") in {"calibration", "residency", "aa"}]
    planned_by_id = {event["id"]: event for event in planned_events}
    ids = [row.get("event_id") for row in event_rows]
    duplicates = sorted(eid for eid, count in Counter(ids).items() if count > 1)
    if duplicates:
        return {"state": "NO_ADJUDICABLE_CONTROLS", "campaign": "HOLD",
                "reason": f"plan_event_id_duplicate:{duplicates[0]}", "numeric_metrics_used": False}
    if set(ids) != set(planned_by_id):
        return {"state": "NO_ADJUDICABLE_CONTROLS", "campaign": "HOLD",
                "reason": f"plan_event_id_union_mismatch:missing={len(set(planned_by_id)-set(ids))}:extra={len(set(ids)-set(planned_by_id))}",
                "numeric_metrics_used": False}
    event_by_id = {row["event_id"]: row for row in event_rows}
    try:
        for eid, row in event_by_id.items():
            event = planned_by_id[eid]
            _validate_event_identity(row, event)
            if not synthetic:
                valid, reason = telemetry_check(row)
                if not valid:
                    raise DeriveError(f"S20_CONTROL_INVALID:{eid}:{reason}")
            elif row.get("s20_fixture_valid") is not True:
                raise DeriveError(f"SYNTHETIC_CONTROL_INVALID:{eid}")
            _check_margin(row, event)
        episodes = _index_episode_rows(episode_rows, event_by_id, synthetic)
        calibrations: dict[str, dict[str, int]] = defaultdict(dict)
        calibration_elapsed: dict[str, dict[str, float]] = defaultdict(dict)
        for event in planned_events:
            if event["phase"] != "calibration":
                continue
            row = event_by_id[event["id"]]
            unit_id, variant = event["unit_id"], event["variant"]
            r = row.get("R")
            elapsed = episodes[event["id"]]["sum_t_app_us"] / 1e6
            if isinstance(r, bool) or not isinstance(r, int) or r < 1:
                raise DeriveError(f"calibration_R_invalid:{event['id']}")
            if not synthetic and elapsed < 2.5:
                raise DeriveError(f"calibration_target_not_reached:{event['id']}:seconds={elapsed}")
            calibrations[unit_id][variant] = r
            calibration_elapsed[unit_id][variant] = elapsed
        r_by_unit: dict[str, int] = {}
        for unit_id, variants in calibrations.items():
            if set(variants) != {"A", "B", "C"} or len(set(variants.values())) != 1:
                raise DeriveError(f"R_NOT_COMMON_A_B_C:{unit_id}")
            r_by_unit[unit_id] = variants["A"]
        if len(r_by_unit) != 140:
            raise DeriveError(f"calibration_unit_coverage_invalid:{len(r_by_unit)}")

        time_by_id: dict[str, float] = {}
        identity_by_id: dict[str, dict[int, list[dict[str, Any]]]] = {}
        identity_hash_by_id: dict[str, str] = {}
        for event in planned_events:
            if event["phase"] == "calibration":
                continue
            row = event_by_id[event["id"]]
            unit_id = f"W{event['W_target_kib']}|O{event['O']}|m{event['m']}|K{event['K']}"
            expected_r = r_by_unit.get(unit_id)
            if row.get("R") != expected_r:
                raise DeriveError(f"R_NOT_COMMON_BY_W_M_K:{event['id']}:expected={expected_r}:actual={row.get('R')}")
            if row.get("timestamps_complete") is not True:
                raise DeriveError(f"episode_timestamps_not_marked:{event['id']}")
            agg = episodes[event["id"]]
            time_by_id[event["id"]] = agg["mean_t_app_us"]
            regions, region_hash = _check_weight_regions(row, event)
            identity_by_id[event["id"]] = regions
            identity_hash_by_id[event["id"]] = region_hash

        # A and C refer to one still-live region, keyed by cell and replica.
        residency_by_coord = {
            (event["W_target_kib"], event["O"], event["m"], event["K"], event["variant"], event["replica"]): event
            for event in planned_events if event["phase"] == "residency"
        }
        for event in planned_events:
            if event["phase"] != "residency" or event["variant"] != "A":
                continue
            a_id = event["id"]
            c_event = residency_by_coord.get((event["W_target_kib"], event["O"], event["m"],
                                               event["K"], "C", event["replica"]))
            if c_event is None:
                raise DeriveError(f"A_C_match_missing_or_duplicate:{a_id}")
            if identity_hash_by_id[a_id] != identity_hash_by_id[c_event["id"]]:
                raise DeriveError(f"A_C_live_region_identity_mismatch:{a_id}:{c_event['id']}")
            for worker in range(8):
                ar, cr = identity_by_id[a_id][worker][0], identity_by_id[c_event["id"]][worker][0]
                if any(ar[key] != cr[key] for key in ("allocation_id", "address", "sha256")):
                    raise DeriveError(f"A_C_region_pointer_or_hash_mismatch:{a_id}:w{worker}")

        # A/A is exactly 31 paired observations for each architectural m.
        aa_values: dict[int, list[float]] = defaultdict(list)
        aa_region_ids: dict[int, list[str]] = defaultdict(list)
        aa_by_pair: dict[str, dict[str, dict[str, Any]]] = defaultdict(dict)
        for event in planned_events:
            if event["phase"] == "aa":
                aa_by_pair[event["pair_id"]][event["observation"]] = event_by_id[event["id"]]
                aa_region_ids[event["m"]].append(identity_hash_by_id[event["id"]])
        if len(aa_by_pair) != 124:
            raise DeriveError(f"AA31_pair_count_invalid:{len(aa_by_pair)}")
        for pair_id, observations in aa_by_pair.items():
            if set(observations) != {"A1", "A2"}:
                raise DeriveError(f"AA_pair_incomplete:{pair_id}")
            pair_events = [e for e in planned_events if e.get("pair_id") == pair_id]
            if len(pair_events) != 2:
                raise DeriveError(f"AA_pair_event_count_invalid:{pair_id}")
            eid1, eid2 = pair_events[0]["id"], pair_events[1]["id"]
            if identity_hash_by_id[eid1] != identity_hash_by_id[eid2]:
                raise DeriveError(f"AA_pair_region_changed:{pair_id}")
            if observations["A1"].get("region_instance_id") != observations["A2"].get("region_instance_id"):
                raise DeriveError(f"AA_pair_allocation_id_changed:{pair_id}")
            a, b = time_by_id[eid1], time_by_id[eid2]
            aa_values[pair_events[0]["m"]].append(_relative_delta(a, b))
        for m in M_VALUES:
            if len(aa_values[m]) != 31:
                raise DeriveError(f"AA31_replica_coverage_invalid:m{m}")

        # Reproduce normalized episode times; each marginal uses separately
        # calibrated R(W,m,K), shared by A/B/C for that K.
        by_coord: dict[tuple[int, int, int, str, int], float] = {}
        for event in planned_events:
            if event["phase"] == "residency":
                by_coord[(event["W_target_kib"], event["m"], event["K"], event["variant"], event["replica"])] = time_by_id[event["id"]]

        def marginals(w: int, m: int, variant: str) -> list[float]:
            values = []
            for replica in range(1, 32):
                t1 = by_coord[(w, m, 1, variant, replica)]
                t16 = by_coord[(w, m, 16, variant, replica)]
                marginal = (t16 - t1) / 15.0
                if not _positive(marginal):
                    raise DeriveError(f"nonpositive_or_nonfinite_marginal:W{w}:m{m}:{variant}:r{replica}")
                values.append(marginal)
            return values

        arch_c = {variant: statistics.median(marginals(288, 1, variant)) for variant in VARIANTS}
        if any(not _positive(value) for value in arch_c.values()):
            raise DeriveError("nonpositive_architectural_marginal")
        rho = arch_c["A"] / arch_c["B"]
        delta_by_m = {}
        for m in M_VALUES:
            c_b = statistics.median(marginals(288, m, "B"))
            c_c = statistics.median(marginals(288, m, "C"))
            if not _positive(c_b) or not _positive(c_c):
                raise DeriveError(f"nonpositive_B_C_marginal:m{m}")
            delta_by_m[m] = _relative_delta(c_b, c_c)
        aa_by_m = {m: _nearest_rank(aa_values[m], .95) for m in M_VALUES}
        if any(delta_by_m[m] > .15 for m in M_VALUES):
            raise DeriveError("DELTA_BC_CONTROL_GT_0_15")
        if any(aa_by_m[m] > .05 for m in M_VALUES):
            raise DeriveError("AA31_NOISE_CONTROL_GT_0_05")

        p_by_m = {}
        for m in M_VALUES:
            per_replica = []
            for replica in range(1, 32):
                time_k16 = by_coord[(288, m, 16, "A", replica)]
                per_replica.append((512 * 1024 * m * 16) / time_k16)
            p_by_m[m] = statistics.median(per_replica)
        speedups = {m: p_by_m[m] / p_by_m[1] for m in M_VALUES}
        max_s = max(speedups[8], speedups[16])

        # D11 L2 attribution consumes the 31 governing K=16,A replicas at each
        # W. Cache scope and provenance are retained per LP and source event.
        point_rows = []
        for w in W_GRID:
            o = 1024 if w == 288 else planlib.output_for_w(w)
            c_a = statistics.median(marginals(w, 1, "A"))
            g = c_a / (512 * o)
            cache_events = [event_by_id[event["id"]] for event in planned_events
                            if (event["phase"] == "residency" and event["variant"] == "A" and
                                event["m"] == 1 and event["W_target_kib"] == w and
                                event["K"] == 16)]
            if len(cache_events) != 31:
                raise DeriveError(f"L2_governing_replica_count_invalid:W{w}:{len(cache_events)}")
            per_lp: dict[int, dict[str, list[float]]] = {
                lp: {"l2": [], "llc": []} for lp in LP_BY_WORKER}
            seen_replicas: set[int] = set()
            for cache_event in cache_events:
                replica = cache_event.get("replica")
                if type(replica) is not int or replica in seen_replicas:
                    raise DeriveError(f"L2_replica_provenance_invalid:W{w}:{replica}")
                seen_replicas.add(replica)
                cache = cache_event.get("cache_evidence")
                by_lp = {item.get("lp"): item for item in cache if isinstance(item, dict)} if isinstance(cache, list) else {}
                if set(by_lp) != set(LP_BY_WORKER):
                    raise DeriveError(f"L2_LP_coverage_invalid:W{w}:r{replica}")
                for lp, item in by_lp.items():
                    if (item.get("scope") != "LP_WORKER" or item.get("valid") is not True or
                            item.get("replica") != replica or item.get("source_event_id") != cache_event.get("event_id") or
                            not re.fullmatch(r"[0-9a-f]{64}", str(item.get("source_raw_sha256", "")))):
                        raise DeriveError(f"L2_LLC_event_provenance_invalid:W{w}:r{replica}:LP{lp}")
                    l2 = item.get("l2_miss_fraction")
                    llc = item.get("llc_miss_fraction")
                    if not _positive_or_zero(l2) or not _positive_or_zero(llc):
                        raise DeriveError(f"L2_LLC_fraction_nonfinite_or_negative:W{w}:r{replica}:LP{lp}")
                    per_lp[lp]["l2"].append(float(l2))
                    per_lp[lp]["llc"].append(float(llc))
            if seen_replicas != set(range(1, 32)):
                raise DeriveError(f"L2_replica_coverage_invalid:W{w}")
            by_lp = {lp: {"scope": "LP_WORKER", "l2_miss_fraction": statistics.median(values["l2"]),
                          "llc_miss_fraction": statistics.median(values["llc"]),
                          "governing_replicas": 31, "replica_ids": list(range(1, 32)),
                          "source_event_ids": [event["event_id"] for event in cache_events]}
                    for lp, values in per_lp.items()}
            point_rows.append({"W": w, "O": o, "g": g, "l2": by_lp,
                               "llc_valid": all(_positive_or_zero(v.get("llc_miss_fraction")) for v in by_lp.values()),
                               "governing_replicas": 31})
        plateau = {lp: statistics.median(point_rows[i]["l2"][lp]["l2_miss_fraction"] for i in range(2))
                   for lp in LP_BY_WORKER}
        knee_pair = None
        l2_pair = None
        for i in range(2, len(point_rows) - 1):
            base = statistics.median(point_rows[j]["g"] for j in range(2))
            temporal_here = point_rows[i]["g"] >= base * 1.15
            temporal_next = point_rows[i + 1]["g"] >= base * 1.15
            if temporal_here and temporal_next:
                knee_pair = (point_rows[i]["W"], point_rows[i + 1]["W"])
                l2_here = all(point_rows[i]["l2"][lp]["l2_miss_fraction"] - plateau[lp] >= .20
                              and point_rows[i]["l2"][lp]["llc_miss_fraction"] <= .10 for lp in LP_BY_WORKER)
                l2_next = all(point_rows[i + 1]["l2"][lp]["l2_miss_fraction"] - plateau[lp] >= .20
                              and point_rows[i + 1]["l2"][lp]["llc_miss_fraction"] <= .10 for lp in LP_BY_WORKER)
                if l2_here and l2_next:
                    l2_pair = knee_pair
                break

        if rho > .50 or max_s < 1.50:
            state = "RESIDENCIA_FAIL"
        elif knee_pair is None or l2_pair != knee_pair:
            state = "L2_NOT_ESTABLISHED"
        else:
            state = "RESIDENCIA_PASS"
        ids = [event["id"] for event in planned_events]
        planlib.verify_union({"events": planned_events}, ids, ids)
        return {
            "schema": "T0P_S19_RESIDENCY_RESULT_R17_V1", "label": "OFFICIAL",
            "state": state, "campaign": "ADJUDICABLE", "numeric_metrics_used": True,
            "plan_sha256": plan["sha256"], "plan_event_count": len(ids),
            "condition_counts": {"total": 420, "architecture": 60, "sweep": 360},
            "calibration_units": len(r_by_unit), "calibrated_R_by_unit": r_by_unit,
            "AA31_pairs_by_m": {str(m): len(aa_values[m]) for m in M_VALUES},
            "rho": rho, "rho_max": .50, "rho_strong": .25,
            "delta_BC_by_m": {str(m): delta_by_m[m] for m in M_VALUES}, "delta_BC_max": .15,
            "S_by_m": {str(m): speedups[m] for m in M_VALUES}, "max_S8_S16": max_s,
            "max_S_min": 1.50, "AA_noise_P95_by_m": {str(m): aa_by_m[m]},
            "AA_noise_max": .05,
            "L2_RESIDENCY": "ATTRIBUTED" if l2_pair else "NOT_ESTABLISHED",
            "L2_evidence_provenance": "median of 31 governing residency replicas, phase=residency, variant=A, m=1, K=16, LP_WORKER",
            "temporal_knee_W_pair_kib": list(knee_pair) if knee_pair else None,
            "L2_attributed_W_pair_kib": list(l2_pair) if l2_pair else None,
            "C_L2_knee_kib": None if not l2_pair else l2_pair[0],
            "B_L2_operational_kib_per_worker": None,
            "W_curve": [{"W_target_kib": p["W"], "O": p["O"], "g": p["g"]} for p in point_rows],
            "derived_event_ids": ids, "raws_replaced": False,
        }
    except (DeriveError, KeyError, TypeError, ValueError, OverflowError) as exc:
        return {"schema": "T0P_S19_RESIDENCY_RESULT_R17_V1", "label": "OFFICIAL",
                "state": "NO_ADJUDICABLE_CONTROLS", "campaign": "HOLD",
                "reason": str(exc), "numeric_metrics_used": False,
                "plan_sha256": plan.get("sha256"), "raws_replaced": False}


def _positive_or_zero(value: Any) -> bool:
    return (isinstance(value, (int, float)) and not isinstance(value, bool)
            and math.isfinite(value) and value >= 0)


def _synthetic_fixture(plan: dict[str, Any], *, branch: str = "pass") -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    events = plan["events"]
    wgrid = set(W_GRID)
    event_rows: list[dict[str, Any]] = []
    episodes: list[dict[str, Any]] = []
    pair_address = {worker: 0x10000000 + worker * 0x1000000 for worker in range(8)}
    region_hash = "a" * 64
    for ev in events:
        row = {k: v for k, v in ev.items() if k not in {"order_key", "sequence", "parallel_event_group", "workers_inside_event"}}
        row["event_id"] = ev["id"]
        row.update({"label": "SYNTHETIC_FIXTURE", "R": 1, "f_tsc_hz": 2_112_000_000.0,
                    "qpf_hz": 1_000_000.0, "s20_fixture_valid": True,
                    "timestamps_complete": True, "pre_margin_seconds": .201,
                    "post_margin_seconds": .201, "margin_condition_id": ev.get("condition_id"),
                    "margin_continuous": True, "qpc_pad_start": 1_000_000,
                    "qpc_start": 1_200_000, "qpc_end": 1_400_000, "qpc_pad_end": 1_600_000,
                    "large_pages": True, "affinity": "PASS", "numerical": "S12_PASS",
                    "macs_per_call": 512 * ev["O"] * ev["m"]})
        phase, variant = ev["phase"], ev.get("variant", "A")
        if phase == "calibration":
            row["calibration_elapsed_seconds"] = 3.0
            duration_us = 3_000_000.0
        else:
            duration_scale = ev["O"] / 1024.0
            if ev.get("W_target_kib", 288) >= 768:
                duration_scale *= 1.20
            per_call = duration_scale * (10.0 if variant == "A" else 25.0)
            if phase == "aa":
                per_call = duration_scale * 10.0
            if ev.get("K") == 1:
                duration_us = per_call
            else:
                duration_us = per_call * 16
            if phase == "aa":
                row["region_instance_id"] = "shared-architecture-A"
            row["weight_regions"] = []
            copies = ev["K"] if variant == "B" else 1
            for worker in range(8):
                for copy_index in range(copies):
                    row["weight_regions"].append({"worker": worker, "copy_index": copy_index,
                        "address": pair_address[worker] + copy_index * 0x100000,
                        "bytes": 288 * ev["O"], "sha256": region_hash,
                        "allocation_id": f"shared-w{worker}" if variant != "B" else f"B-w{worker}-k{copy_index}"})
            if phase == "residency" and ev["m"] == 1 and ev["K"] == 16:
                hot = ev["W_target_kib"] >= 768
                row["cache_evidence"] = [{"lp": lp, "scope": "LP_WORKER", "valid": True,
                    "replica": ev["replica"], "source_event_id": ev["id"],
                    "source_raw_sha256": "b" * 64, "l2_miss_fraction": .25 if hot else .01,
                    "llc_miss_fraction": .05} for lp in LP_BY_WORKER]
            elif phase == "residency" and ev["m"] == 1:
                # Derivation consumes cache evidence at K=16 only.
                row["cache_evidence"] = [{"lp": lp, "scope": "LP_WORKER", "valid": True,
                    "replica": ev["replica"], "source_event_id": ev["id"],
                    "source_raw_sha256": "b" * 64, "l2_miss_fraction": .01,
                    "llc_miss_fraction": .05} for lp in LP_BY_WORKER]
            if (branch == "nonfinite_fraction" and ev["W_target_kib"] == 512 and ev["m"] == 1 and
                    ev["K"] == 16 and ev["variant"] == "A" and ev["replica"] == 7):
                row["cache_evidence"][0]["l2_miss_fraction"] = float("nan")
            if (branch == "missing_l2_provenance" and ev["W_target_kib"] == 512 and ev["m"] == 1 and
                    ev["K"] == 16 and ev["variant"] == "A" and ev["replica"] == 7):
                row["cache_evidence"][0].pop("source_event_id")
        if (branch == "nonpositive_marginal" and phase == "residency" and
                ev.get("W_target_kib") == 288 and ev.get("m") == 1 and
                ev.get("variant") == "A" and ev.get("replica") == 1):
            duration_us = 1000.0 if ev.get("K") == 1 else 120.0 if ev.get("K") == 16 else duration_us
        if branch == "no_knee" and phase == "residency" and ev.get("W_target_kib", 0) >= 768:
            if ev.get("m") == 1 and ev.get("variant") == "A":
                # Remove the 20% temporal and event transition from points >=768.
                duration_us /= 1.20
                for item in row.get("cache_evidence", []):
                    item["l2_miss_fraction"] = .01
        if branch == "rho_fail" and phase == "residency" and ev.get("W_target_kib") == 288 and ev.get("m") == 1 and ev.get("variant") == "A":
            duration_us = duration_us * 1.6
        if branch == "delta_hold" and phase == "residency" and ev.get("W_target_kib") == 288 and ev.get("m") == 4 and ev.get("variant") == "C":
            duration_us = duration_us * 2.0
        if branch == "aa_hold" and phase == "aa" and ev.get("observation") == "A2":
            duration_us *= 1.10
        if branch == "s20_hold" and phase == "residency" and ev.get("W_target_kib") == 288 and ev.get("m") == 1 and ev.get("K") == 1 and ev.get("variant") == "A":
            row["s20_fixture_valid"] = False
        event_rows.append(row)
        apps = []
        app_us = duration_us / max(1, ev.get("K", 1))
        ticks = max(1, round(app_us * 2_112_000_000 / 1e6))
        for app_index in range(ev.get("K", 1)):
            base = 10_000_000_000 + ev["sequence"] * 1_000_000 + app_index * 10_000
            apps.append({"t_app_us": app_us, "workers": [
                {"worker": worker, "lp_start": LP_BY_WORKER[worker], "lp_end": LP_BY_WORKER[worker],
                 "start_tsc": base, "end_tsc": base + ticks,
                 "start_qpc": 100_000_000 + base // 2112,
                 "end_qpc": 100_000_000 + (base + ticks) // 2112}
                for worker in range(8)]})
        episodes.append({"event_id": ev["id"], "episode_index": 1, "applications": apps})
    return event_rows, episodes


def self_test(output: Path) -> None:
    plan = planlib.build_payload()
    plan = {**plan, "events": [event for event in plan["events"]
                               if event.get("phase") in {"calibration", "residency", "aa"}]}
    lines = []
    s20_rows = []
    synthetic_lp_map = []
    for worker, lp in enumerate(LP_BY_WORKER):
        synthetic_lp_map.append({"worker_lp": lp, "p_core": worker})
        sample = {"sample_id": "event-W288-r1"}
        s20_rows.extend([
            {"sample": sample, "core": worker, "channel": "L2_DEMAND_DATA_READ", "state": "VALID",
             "metrics": {"l2_fraction_state": "ESTIMABLE", "l2_numerator": 25, "l2_denominator": 100,
                         "l2_fraction": .25}},
            {"sample": sample, "core": worker, "channel": "LLC_DEMAND_DATA_READ", "state": "VALID",
             "metrics": {"llc_fraction_state": "ESTIMABLE", "llc_numerator": 5, "llc_denominator": 100,
                         "llc_fraction": .05}},
        ])
    cache_fixture = _cache_evidence_from_result("event-W288-r1", 1, {"results": s20_rows},
        synthetic_lp_map, "a" * 64, "c" * 64)
    if (len(cache_fixture) != 8 or {item["lp"] for item in cache_fixture} != set(LP_BY_WORKER) or
            any(item["source_event_id"] != "event-W288-r1" or item["source_raw_sha256"] != "a" * 64 or
                item["source_s20_validation_sha256"] != "c" * 64 or item["l2_numerator"] != 25
                for item in cache_fixture)):
        raise DeriveError("D11_raw_S20_cache_evidence_fixture_invalid")
    try:
        _cache_evidence_from_result("missing", 1, {"results": s20_rows}, synthetic_lp_map,
                                    "a" * 64, "c" * 64)
    except DeriveError as exc:
        if not str(exc).startswith("D11_S20_CACHE_SAMPLE_MISSING"):
            raise
    else:
        raise DeriveError("D11_missing_raw_S20_sample_not_rejected")
    lines.append("D11_S20_raw_counter_binding=PASS missing_sample=REJECTED LPs=8 provenance=raw+validation_hash")
    for branch, expected in (("pass", "RESIDENCIA_PASS"), ("no_knee", "L2_NOT_ESTABLISHED"),
                             ("rho_fail", "RESIDENCIA_FAIL"), ("delta_hold", "NO_ADJUDICABLE_CONTROLS"),
                             ("aa_hold", "NO_ADJUDICABLE_CONTROLS"), ("s20_hold", "NO_ADJUDICABLE_CONTROLS")):
        rows, episode_rows = _synthetic_fixture(plan, branch=branch)
        result = derive(plan, rows, episode_rows,
                        telemetry_check=lambda row: (False, "unused synthetic path"), synthetic=True)
        if result.get("state") != expected:
            raise DeriveError(f"fixture_branch_wrong:{branch}:expected={expected}:actual={result.get('state')}:{result.get('reason')}")
        lines.append(f"branch={branch} state={expected} PASS")
    for branch, expected_reason in (("nonpositive_marginal", "nonpositive_or_nonfinite_marginal"),
                                    ("nonfinite_fraction", "L2_LLC_fraction_nonfinite_or_negative"),
                                    ("missing_l2_provenance", "L2_LLC_event_provenance_invalid")):
        rows, episode_rows = _synthetic_fixture(plan, branch=branch)
        result = derive(plan, rows, episode_rows,
                        telemetry_check=lambda row: (False, "unused synthetic path"), synthetic=True)
        if result.get("state") != "NO_ADJUDICABLE_CONTROLS" or not result.get("reason", "").startswith(expected_reason):
            raise DeriveError(f"D11_fixture_branch_wrong:{branch}:{result}")
        lines.append(f"D11_branch={branch} HOLD PASS")
    rows, episode_rows = _synthetic_fixture(plan)
    duplicate_rows = rows + [dict(rows[0])]
    duplicate_rows[-1]["event_id"] = duplicate_rows[0]["event_id"]
    duplicate = derive(plan, duplicate_rows, episode_rows,
                       telemetry_check=lambda row: (True, "VALID"), synthetic=True)
    if duplicate.get("state") != "NO_ADJUDICABLE_CONTROLS" or "duplicate" not in duplicate.get("reason", ""):
        raise DeriveError("duplicate_ID_branch_not_held")
    lines.append("branch=duplicate_event_id state=NO_ADJUDICABLE_CONTROLS HOLD PASS")
    missing_rows = rows[1:]
    missing_episodes = episode_rows[1:]
    missing = derive(plan, missing_rows, missing_episodes,
                     telemetry_check=lambda row: (True, "VALID"), synthetic=True)
    if missing.get("state") != "NO_ADJUDICABLE_CONTROLS" or "missing=" not in missing.get("reason", ""):
        raise DeriveError("missing_ID_branch_not_held")
    lines.append("branch=missing_event_id state=NO_ADJUDICABLE_CONTROLS HOLD PASS")
    smoke = derive(plan, [{"label": "SMOKE", "event_id": "smoke"}], [],
                   telemetry_check=lambda row: (False, "must_not_be_called"))
    if smoke.get("state") != "SMOKE_ONLY" or smoke.get("numeric_metrics_used") is not False:
        raise DeriveError("smoke_numeric_use_branch_failed")
    lines.append("branch=SMOKE_ONLY numeric_metrics_used=false PASS")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("RESIDENCY_DERIVER_R17_QA PASS\n" + "\n".join(lines) + "\n"
                      + "R=R(W,m,K) common A/B/C by K; T=sum(episode times)/R; c=(T16-T1)/15 PASS\n"
                      + "AA31=31 pairs per architectural m; delta_BC_by_m; exact plan/executed/derived union PASS\n"
                      + "L2 attribution requires temporal knee and valid L2/LLC evidence at same two consecutive W points PASS\n"
                      + "D13 derive writes derived artifacts exclusively; raw source IDs never replaced PASS\n",
                      encoding="utf-8", newline="\n")
    print(f"RESIDENCY_DERIVER_R17_QA PASS branches={len(lines)} events={len(plan['events'])} evidence={output}")


def _d13_raw_names(store) -> list[str]:
    names = []
    manifest = store.directory / "raw_manifest_r17"
    for sidecar in sorted(manifest.glob("*.json")):
        data = json.loads(sidecar.read_text(encoding="utf-8"))
        name = data.get("name")
        if not isinstance(name, str) or Path(name).name != name:
            raise DeriveError("D13_raw_manifest_name_invalid")
        store.verify_raw(name)
        names.append(name)
    if not names:
        raise DeriveError("D13_raw_sources_missing")
    return names


def _cache_evidence_from_result(sample_id: str, replica: int, result: dict[str, Any],
                                lp_map: list[dict[str, Any]], emon_sha: str,
                                validation_sha: str) -> list[dict[str, Any]]:
    by_sample_core: dict[tuple[str, int], dict[str, Any]] = {}
    for item in result.get("results", []):
        sample = item.get("sample", {})
        key = (str(sample.get("sample_id", "")), int(item.get("core", -1)))
        by_sample_core.setdefault(key, {})[item.get("channel")] = item
    pcore_by_lp = {int(item["worker_lp"]): int(item["p_core"]) for item in lp_map}
    evidence = []
    for lp in LP_BY_WORKER:
        p_core = pcore_by_lp.get(lp)
        if p_core is None:
            raise DeriveError(f"D11_S20_LP_MAP_MISSING:{lp}")
        channels = by_sample_core.get((str(sample_id), p_core), {})
        l2, llc = channels.get("L2_DEMAND_DATA_READ"), channels.get("LLC_DEMAND_DATA_READ")
        if not isinstance(l2, dict) or not isinstance(llc, dict):
            raise DeriveError(f"D11_S20_CACHE_SAMPLE_MISSING:{sample_id}:LP{lp}")
        lm, cm = l2.get("metrics", {}), llc.get("metrics", {})
        evidence.append({"lp": lp, "p_core": p_core, "scope": "LP_WORKER",
            "valid": (l2.get("state") == "VALID" and llc.get("state") == "VALID" and
                      lm.get("l2_fraction_state") == "ESTIMABLE" and cm.get("llc_fraction_state") == "ESTIMABLE"),
            "replica": replica, "source_event_id": sample_id, "source_raw_sha256": emon_sha,
            "source_s20_validation_sha256": validation_sha,
            "l2_numerator": lm.get("l2_numerator"), "l2_denominator": lm.get("l2_denominator"),
            "llc_numerator": cm.get("llc_numerator"), "llc_denominator": cm.get("llc_denominator"),
            "l2_miss_fraction": lm.get("l2_fraction"), "llc_miss_fraction": cm.get("llc_fraction")})
    return evidence


def _attach_s20_evidence(event_rows: list[dict[str, Any]], store: SessionAttemptStore,
                         raw_names: list[str]) -> dict[str, Any]:
    """Recompute S-20 from immutable capture inputs and bind every event row to it."""
    required = {"emon_raw.txt", "clock_map.csv", "block_marks_r17.csv",
                "effective_config_source.json", "events.edp", "session_configuration_r17.json",
                "residency_plan_r17.json", "residency_smoke_schedule_r17.tsv"}
    if not required.issubset(raw_names):
        raise DeriveError("D11_S20_raw_lineage_incomplete")
    config_path = store.directory / "effective_config.json"
    compat_path = store.directory / "effective_config.json.lineage.json"
    validation_path = store.directory / "s20_validation_r17.json"
    validation_lineage_path = store.directory / "derived_lineage_r17" / "s20_validation_r17.json.json"
    if not config_path.is_file() or not compat_path.is_file() or not validation_path.is_file() or not validation_lineage_path.is_file():
        raise DeriveError("D11_S20_derived_lineage_missing")
    config = s20_validator.load_config(config_path)
    config_hash = hashlib.sha256(config_path.read_bytes()).hexdigest()
    config_lineage = json.loads(compat_path.read_text(encoding="utf-8"))
    if config_lineage.get("derived_sha256") != config_hash:
        raise DeriveError("D11_effective_config_hash_mismatch")
    for name, expected in config_lineage.get("sources", {}).items():
        if store.verify_raw(name) != expected:
            raise DeriveError(f"D11_effective_config_raw_hash_mismatch:{name}")
    validation_bytes = validation_path.read_bytes()
    validation_lineage = json.loads(validation_lineage_path.read_text(encoding="utf-8"))
    if validation_lineage.get("sha256") != hashlib.sha256(validation_bytes).hexdigest():
        raise DeriveError("D11_S20_validation_hash_mismatch")
    validation_sources = validation_lineage.get("source_raw_sha256", {})
    for name in required:
        if validation_sources.get(name) != store.verify_raw(name):
            raise DeriveError(f"D11_S20_validation_raw_provenance_missing:{name}")
    result = s20_validator.validate_capture(store.directory / "emon_raw.txt", store.directory / "clock_map.csv",
                                            store.directory / "block_marks_r17.csv", config_path,
                                            mode=config.get("mode", "full"))
    if result.get("state") != "VALID" or config.get("mode") != "full":
        raise DeriveError(f"D11_S20_FULL_NOT_VALID:{result.get('state')}:{config.get('mode')}")
    emon_sha = store.verify_raw("emon_raw.txt")
    validation_sha = hashlib.sha256(validation_bytes).hexdigest()
    ref = {"directory": str(store.directory), "files": {"emon_raw": "emon_raw.txt", "clock_map": "clock_map.csv",
           "block_marks": "block_marks_r17.csv", "effective_config": "effective_config.json",
           "effective_config_source": "effective_config_source.json", "events": "events.edp"},
           "mode": "full", "f_tsc_qualified_hz": float(config["f_tsc_qualified_hz"]),
           "lp_map": config["lp_map"]}
    for row in event_rows:
        row["s20_attempt"] = ref
        if (row.get("phase") != "residency" or row.get("variant") != "A" or
                row.get("m") != 1 or row.get("K") != 16):
            continue
        source_id = row.get("event_id")
        replica = row.get("replica")
        row["cache_evidence"] = _cache_evidence_from_result(
            str(source_id), int(replica), result, config["lp_map"], emon_sha, validation_sha)
    return result


def derive_files(plan_path: Path, event_path: Path, episode_path: Path,
                 attempt_directory: Path, output_path: Path | None = None) -> dict[str, Any]:
    plan, _all_ids = planlib.verify_plan(plan_path)
    expected_ids = {event["id"] for event in plan["events"]
                    if event.get("phase") in {"calibration", "residency", "aa"}}
    event_rows = read_jsonl(event_path)
    episode_rows = read_jsonl(episode_path)
    if attempt_directory.exists():
        store = SessionAttemptStore(attempt_directory)
        metadata = json.loads((store.directory / "attempt_metadata.json").read_text(encoding="utf-8"))
        if metadata.get("protocol") != "D13-write-once-v1":
            raise DeriveError("D13_attempt_protocol_missing")
    else:
        store = SessionAttemptStore.create(attempt_directory, session_id=f"residency-r17-{plan['sha256'][:12]}",
                                           attempt_id=attempt_directory.name)
    for source, name in ((plan_path, "residency_plan_r17.json"),
                         (event_path, "residency_events_r17.jsonl"),
                         (episode_path, "residency_episodes_r17.jsonl")):
        if (store.directory / name).is_file():
            if hashlib.sha256(source.read_bytes()).hexdigest() != store.verify_raw(name):
                raise DeriveError(f"D13_existing_raw_source_differs:{name}")
        else:
            store.ingest_raw(source, name)
    raw_names = _d13_raw_names(store)
    s20_result = None
    if event_rows and all(row.get("label") in {"SMOKE", "SMOKE_ONLY"} for row in event_rows):
        result = derive(plan, event_rows, episode_rows,
                        telemetry_check=lambda row: (False, "smoke_has_no_adjudication"))
    else:
        s20_result = _attach_s20_evidence(event_rows, store, raw_names)
        result = derive(plan, event_rows, episode_rows, telemetry_check=telemetry_valid)
    result = dict(result)
    executed_ids = [row.get("event_id") for row in event_rows]
    result.setdefault("executed_event_ids", executed_ids)
    duplicates = sorted(eid for eid, count in Counter(executed_ids).items() if count > 1)
    exact_union = not duplicates and set(executed_ids) == expected_ids
    result["event_union"] = {"expected_count": len(expected_ids), "executed_count": len(executed_ids),
                             "unique_executed_count": len(set(executed_ids)), "exact": exact_union,
                             "duplicate_ids": duplicates[:8],
                             "missing_count": len(expected_ids - set(executed_ids)),
                             "extra_count": len(set(executed_ids) - expected_ids)}
    result["plan_sha256"] = plan["sha256"]
    derived_bytes = (json.dumps(result, sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n").encode("utf-8")
    derived_name = output_path.name if output_path is not None else "residency_derived_r17.json"
    if output_path is not None and output_path.parent.resolve() != store.directory:
        raise DeriveError("D13_derived_output_must_be_inside_attempt")
    store.write_derived(derived_name, derived_bytes, source_raw=raw_names,
                         metadata={"deriver": "D11-r17", "S20": "T0P/collector/s20_validator_r17.py",
                                   "numeric_metrics_used": bool(result.get("numeric_metrics_used", False)),
                                   "S20_recomputed_from_raw": s20_result is not None})
    derived_ids = result.get("derived_event_ids", executed_ids)
    ids_bytes = b"".join((json.dumps({"event_id": eid}, separators=(",", ":")) + "\n").encode("ascii")
                        for eid in derived_ids)
    store.write_derived("residency_derived_event_ids_r17.jsonl", ids_bytes, source_raw=raw_names,
                         metadata={"role": "exact union of event IDs consumed by derivation"})
    if exact_union and result.get("state") not in {"NO_ADJUDICABLE_CONTROLS", "SMOKE_ONLY"}:
        planlib.verify_union(expected_ids, executed_ids, derived_ids)
    return {**result, "attempt_directory": str(store.directory)}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    test = sub.add_parser("self-test")
    test.add_argument("--output", type=Path, default=DEFAULT_EVIDENCE)
    run = sub.add_parser("derive")
    run.add_argument("--plan", type=Path, required=True)
    run.add_argument("--events", type=Path, required=True)
    run.add_argument("--episodes", type=Path, required=True)
    run.add_argument("--attempt-directory", type=Path, required=True)
    run.add_argument("--output", type=Path, required=True)
    ns = parser.parse_args(argv)
    try:
        if ns.command == "self-test":
            self_test(ns.output)
            return 0
        result = derive_files(ns.plan, ns.events, ns.episodes, ns.attempt_directory, ns.output)
        print(f"RESIDENCY_DERIVE_R17 state={result.get('state')} numeric_metrics_used={result.get('numeric_metrics_used')} reason={result.get('reason','none')} event_union_exact={result.get('event_union',{}).get('exact')} output={ns.output}")
        return 0 if result.get("state") in {"RESIDENCIA_PASS", "RESIDENCIA_FAIL", "L2_NOT_ESTABLISHED", "SMOKE_ONLY"} else 2
    except (OSError, DeriveError, planlib.PlanError, KeyError, TypeError, ValueError, ImportError) as exc:
        print(f"RESIDENCY_DERIVE_R17 REJECT {type(exc).__name__}: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
