#!/usr/bin/env python3
"""Frozen S-24 acquisition plan: qualification units and complete residence blocks."""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Iterable

SEED = "4F4D454741543031"
SCHEMA = "T0P_S25_ACQUISITION_PLAN_R18_V1"
M_VALUES = (1, 4, 8, 16)
K_ARCH = (1, 2, 4, 8, 16)
W_GRID = (256, 288, 512, 768, 1024, 1280, 1536, 1792,
          2048, 2560, 3072, 4096, 6144, 8192, 12288, 16384)
VARIANTS = ("A", "B", "C")
REPLICAS = tuple(range(1, 32))
CORES = tuple(f"P{i}" for i in range(8)) + ("8P",)
FRONTIERS = ("prepared", "dynamic")
QUALIFICATION_PER_SEGMENT = 12
W_PER_REPLICA_SEGMENT = len(W_GRID)
ROOT = Path(__file__).resolve().parents[2]
DEFAULT_PLAN = Path(__file__).resolve().parent / "logs_r18" / "obj_r18" / "residency_plan_r18.json"
DEFAULT_EVIDENCE = Path(__file__).resolve().parent / "logs_r18" / "obj_r18" / "plan_qa_r18.txt"


class PlanError(ValueError):
    pass


def canonical_bytes(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=True, allow_nan=False).encode("ascii")


def digest(value: Any) -> str:
    return hashlib.sha256(canonical_bytes(value)).hexdigest()


def order_key(kind: str, identity: str) -> str:
    return hashlib.sha256(f"T0P-S24|{SEED}|{kind}|{identity}".encode("ascii")).hexdigest()


def output_for_w(w_kib: int) -> int:
    den = 8 * 288
    groups = (2 * w_kib * 1024 + den) // (2 * den)
    out = 8 * groups
    if out < 8 or out % 8:
        raise PlanError(f"O_FORMULA_INVALID:W={w_kib}:O={out}")
    return out


def _condition(kind: str, variant: str, m: int, k: int, w: int, o: int) -> dict[str, Any]:
    cid = f"{kind}|{variant}|m{m}|K{k}|W{w}|O{o}"
    return {"id": cid, "kind": kind, "variant": variant, "m": m, "K": k,
            "W_target_kib": w, "W_real_kib": 288 * o / 1024, "O": o}


def _candidate_names(family: str, m: int) -> tuple[str, ...]:
    if family == "ggml":
        return ("ggml_flat", "ggml_repack")
    return ("torch_mm", "torch_matmul", "torch_linear", "torch_mv") if m == 1 else (
        "torch_mm", "torch_matmul", "torch_linear")


def _qualification_events() -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    units = []
    for frontier in FRONTIERS:
        for core in CORES:
            for m in M_VALUES:
                uid = f"{core}|m{m}|{frontier}|O1024"
                units.append({"id": uid, "core": core, "m": m,
                              "frontier": frontier, "O": 1024,
                              "order_key": order_key("qualification-unit", uid)})
    units.sort(key=lambda row: (row["order_key"], row["id"].encode("ascii")))
    segments: dict[str, list[str]] = defaultdict(list)
    for index, unit in enumerate(units):
        segment = f"Q{index // QUALIFICATION_PER_SEGMENT + 1:02d}"
        unit["segment_id"] = segment
        segments[segment].append(unit["id"])

    events: list[dict[str, Any]] = []
    for unit in units:
        uid, core, m, frontier = unit["id"], unit["core"], unit["m"], unit["frontier"]
        candidates = [("reference_selection_ggml", name) for name in _candidate_names("ggml", m)]
        candidates += [("reference_selection_torch", name) for name in _candidate_names("torch", m)]
        selection = []
        for replica in REPLICAS:
            per_replica = []
            for phase, provider in candidates:
                eid = f"{phase}|{core}|m{m}|{frontier}|r{replica}|{provider}"
                per_replica.append({"id": eid, "phase": phase, "provider": provider,
                                    "replica": replica, "unit_id": uid,
                                    "segment_id": unit["segment_id"],
                                    "pair_group_id": f"{uid}|selection|r{replica}",
                                    "order_key": order_key("selection-event", eid)})
            selection.extend(sorted(per_replica, key=lambda row: (row["order_key"], row["id"])))

        comparison = []
        for replica in REPLICAS:
            group = []
            for provider in ("omega", "ggml_winner", "torch_winner"):
                eid = f"competency|{core}|m{m}|{frontier}|r{replica}|{provider}"
                group.append({"id": eid, "phase": "competency", "provider": provider,
                              "replica": replica, "unit_id": uid,
                              "segment_id": unit["segment_id"],
                              "pair_group_id": f"{uid}|comparison|r{replica}",
                              "order_key": order_key("comparison-event", eid)})
            comparison.extend(sorted(group, key=lambda row: (row["order_key"], row["id"])))
        unit_events = selection + comparison
        for event in unit_events:
            event.update({"core": core, "m": m, "frontier": frontier, "O": 1024,
                          "configuration": "O1024|I512|8P-map"})
        events.extend(unit_events)
    return units, events


def _qualification_auxiliary(q_units: list[dict[str, Any]],
                             q_events: list[dict[str, Any]]) -> dict[str, Any]:
    unit = next(row for row in q_units if row["core"] == "8P" and row["m"] == 1 and
                row["frontier"] == "prepared" and row["O"] == 1024)
    source_omega = {(row["replica"], row["provider"]): row["id"] for row in q_events
                    if row.get("unit_id") == unit["id"] and row["phase"] == "competency"}
    variants = ((1, 8), (2, 8), (4, 8), (8, 8), (10, 8), (12, 8), (14, 8), (12, 16), (14, 16))
    events = []
    for replica in REPLICAS:
        bw_id = f"BW_DRAM|8P|m1|prepared|r{replica}"
        events.append({"id": bw_id, "phase": "qualification_instrument", "provider": "BW_DRAM",
                       "replica": replica, "unit_id": unit["id"], "segment_id": unit["segment_id"],
                       "core": "8P", "m": 1, "frontier": "prepared", "O": 1024,
                       "all_workers_concurrent": True, "planned_from": "S-24"})
        for chains, updates in variants:
            pid = f"P_instr|c{chains}|U{updates}|r{replica}"
            events.append({"id": pid, "phase": "qualification_instrument", "provider": "P_instr",
                           "replica": replica, "unit_id": unit["id"], "segment_id": unit["segment_id"],
                           "core": "8P", "m": 1, "frontier": "prepared", "O": 1024,
                           "chains": chains, "U": updates, "all_workers_concurrent": True,
                           "planned_from": "S-19"})
        omega_id = source_omega[(replica, "omega")]
        h_id = f"H-KQ|8P|m1|prepared|r{replica}"
        d_id = f"D_prime|8P|m1|prepared|r{replica}"
        events.append({"id": h_id, "phase": "qualification_derived", "provider": "H-KQ",
                       "replica": replica, "unit_id": unit["id"], "segment_id": unit["segment_id"],
                       "core": "8P", "m": 1, "frontier": "prepared", "O": 1024,
                       "source_event_ids": [omega_id], "planned_from": "S-19"})
        events.append({"id": d_id, "phase": "qualification_derived", "provider": "D_prime",
                       "replica": replica, "unit_id": unit["id"], "segment_id": unit["segment_id"],
                       "core": "8P", "m": 1, "frontier": "prepared", "O": 1024,
                       "source_event_ids": [omega_id, bw_id], "planned_from": "S-19"})
    return {"unit_id": unit["id"], "segment_id": unit["segment_id"],
            "shared_segment_with": ["H-KQ", "D_prime", "BW_DRAM"],
            "all_workers_concurrent": True, "events": events,
            "event_count": len(events), "BW_DRAM_replicas": 31,
            "P_instr_variants": [f"c{c}_U{u}" for c, u in variants],
            "H-KQ_and_D_prime_source_events": "linked_by_source_event_ids"}


def s25_replica_orders() -> list[dict[str, Any]]:
    orders = []
    for replica in REPLICAS:
        keyed = []
        for w in W_GRID:
            label = f"T0P-S25|{SEED}|replica={replica}|W={w}"
            keyed.append((hashlib.sha256(label.encode("ascii")).hexdigest(), w))
        keyed.sort(key=lambda item: (item[0], item[1]))
        orders.append({"replica": replica, "W_target_kib": [w for _, w in keyed],
                       "order_keys": [key for key, _ in keyed]})
    return orders


def _residency_payload() -> tuple[list[dict[str, Any]], list[dict[str, Any]], list[dict[str, Any]], list[dict[str, Any]], list[dict[str, Any]], list[dict[str, Any]]]:
    conditions = []
    for w in W_GRID:
        o = 1024 if w == 288 else output_for_w(w)
        ks = K_ARCH if w == 288 else (1, 16)
        kind = "architecture" if w == 288 else "sweep"
        for m in M_VALUES:
            for k in ks:
                for variant in VARIANTS:
                    conditions.append(_condition(kind, variant, m, k, w, o))
    condition_by = {(c["W_target_kib"], c["m"], c["K"], c["variant"]): c for c in conditions}

    cal_units = []
    for w in W_GRID:
        o = 1024 if w == 288 else output_for_w(w)
        ks = K_ARCH if w == 288 else (1, 16)
        for m in M_VALUES:
            for k in ks:
                uid = f"W{w}|O{o}|m{m}|K{k}"
                cal_units.append({"id": uid, "W_target_kib": w, "O": o,
                                  "m": m, "K": k, "order_key": order_key("residency-R", uid)})
    cal_units.sort(key=lambda row: row["order_key"])

    pairs = []
    for m in M_VALUES:
        for replica in REPLICAS:
            pid = f"AA|W288|O1024|m{m}|K16|r{replica}"
            pairs.append({"id": pid, "W_target_kib": 288, "O": 1024, "m": m,
                          "K": 16, "replica": replica,
                          "R_unit_id": f"W288|O1024|m{m}|K16"})

    # S-25 C: one segment per replica, with all 16 W blocks in the required
    # SHA256 order. A pause can occur only after this complete segment.
    replica_orders = s25_replica_orders()
    blocks = []
    for order in replica_orders:
        replica = order["replica"]
        segment = f"R{replica:02d}"
        for w in order["W_target_kib"]:
            o = 1024 if w == 288 else output_for_w(w)
            bid = f"W{w}|O{o}|r{replica}"
            blocks.append({"id": bid, "W_target_kib": w, "O": o,
                           "replica": replica,
                           "segment_id": segment,
                           "replica_order_sha256": digest(order),
                           "within_block_seed": SEED,
                           "order_key": order_key("residency-block", bid)})
    block_segments: dict[str, list[str]] = defaultdict(list)
    # Freeze the 31 replica-major W orders above before creating any
    # calibration row.  Calibration is a single prelude: each R(W,m,K) is
    # chosen once, shared by A/B/C, then held fixed for every N=31 sample.
    calibration_events: list[dict[str, Any]] = []
    for unit in cal_units:
        w, o, m, k = (unit["W_target_kib"], unit["O"], unit["m"], unit["K"])
        for variant in VARIANTS:
            condition = condition_by[(w, m, k, variant)]
            eid = f"calibration|{unit['id']}|{variant}"
            calibration_events.append({"id": eid, "phase": "calibration", "event_id": eid,
                                       "unit_id": unit["id"], "condition_id": condition["id"],
                                       "variant": variant, "replica": 0, "W_target_kib": w,
                                       "O": o, "m": m, "K": k, "R_unit_id": unit["id"],
                                       "block_id": f"cal|{unit['id']}",
                                       "atomic_group_id": f"cal|{unit['id']}",
                                       "segment_id": ""})

    events: list[dict[str, Any]] = []
    for block in blocks:
        w, o, replica = block["W_target_kib"], block["O"], block["replica"]
        segment = block["segment_id"]
        block["segment_id"] = segment
        block_segments[segment].append(block["id"])
        atomic_units: list[tuple[str, list[dict[str, Any]]]] = []

        # Each marginal comparison keeps its A/B/C measurements contiguous.
        for m in M_VALUES:
            ks = K_ARCH if w == 288 else (1, 16)
            for k in ks:
                rows = []
                for variant in VARIANTS:
                    condition = condition_by[(w, m, k, variant)]
                    eid = f"residency|{condition['id']}|r{replica}"
                    rows.append({"id": eid, "phase": "residency", "event_id": eid,
                                 "unit_id": block["id"], "condition_id": condition["id"],
                                 "variant": variant, "replica": replica,
                                 "W_target_kib": w, "O": o, "m": m, "K": k})
                atomic_units.append((f"m{m}|K{k}", rows))
        if w == 288:
            for m in M_VALUES:
                pid = f"AA|W288|O1024|m{m}|K16|r{replica}"
                obs = ("A1", "A2")
                if order_key("aa-order", pid + "|A1") > order_key("aa-order", pid + "|A2"):
                    obs = ("A2", "A1")
                rows = []
                for observation in obs:
                    eid = f"aa|{pid}|{observation}"
                    rows.append({"id": eid, "phase": "aa", "event_id": eid,
                                 "pair_id": pid, "observation": observation,
                                 "unit_id": block["id"],
                                 "condition_id": f"architecture|A|m{m}|K16|W288|O1024",
                                 "variant": "A", "replica": replica,
                                 "W_target_kib": w, "O": o, "m": m, "K": 16})
                atomic_units.append((f"aa|{pid}", rows))

        # Seeded atomic subgroups run without pauses until the W/replica block
        # ends. Calibration is in the prelude above, not interleaved here.
        sample_groups = atomic_units
        sample_groups.sort(key=lambda item: (hashlib.sha256(
            f"T0P-S25|{SEED}|replica={replica}|W={w}|group={item[0]}".encode("ascii")).hexdigest(),
            item[0].encode("ascii")))
        frozen_group_order = [group_id for group_id, _ in sample_groups]
        block["within_block_group_order"] = frozen_group_order
        block["within_block_order_sha256"] = hashlib.sha256(
            canonical_bytes(frozen_group_order)).hexdigest()
        for group_id, rows in sample_groups:
            for row in rows:
                row["segment_id"] = segment
                row["block_id"] = block["id"]
                row["atomic_group_id"] = group_id
                row["within_block_seed"] = SEED
                row["within_block_order_sha256"] = block["within_block_order_sha256"]
                row["order_key"] = order_key("residency-event", row["id"])
                events.append(row)
    return conditions, cal_units, pairs, blocks, calibration_events + events, replica_orders


def build_payload() -> dict[str, Any]:
    q_units, q_events = _qualification_events()
    qualification_auxiliary = _qualification_auxiliary(q_units, q_events)
    conditions, cal_units, pairs, residency_blocks, r_events, replica_orders = _residency_payload()
    all_events = q_events + r_events
    for sequence, event in enumerate(all_events, 1):
        event["sequence"] = sequence
    event_ids = [event["id"] for event in all_events]
    if len(event_ids) != len(set(event_ids)):
        raise PlanError("generated_duplicate_event_id")
    q_counts = Counter(e["phase"] for e in q_events)
    segment_map: dict[str, list[str]] = defaultdict(list)
    for unit in q_units:
        segment_map[unit["segment_id"]].append(unit["id"])
    for block in residency_blocks:
        segment_map[block["segment_id"]].append(block["id"])
    segment_map[qualification_auxiliary["segment_id"]].append(
        f"auxiliary:{qualification_auxiliary['unit_id']}")
    payload: dict[str, Any] = {
        "schema": SCHEMA,
        "campaign": "T0P-Q4-RESIDENCY",
        "seed": SEED,
        "ordering": "qualification units then S-25 replica-major 31 segments; each segment contains all 16 W in SHA256 order; seeded atomic groups inside complete (W,replica) blocks",
        "geometry": {"I": 512, "P_shards": 8, "LPs": [0, 2, 4, 6, 8, 10, 12, 14],
                     "architecture": {"W_target_kib": 288, "O": 1024},
                     "sweep_W_target_kib": [w for w in W_GRID if w != 288],
                     "sweep_includes": {"W1024_O": output_for_w(1024)},
                     "m_values": list(M_VALUES), "K_architectural": list(K_ARCH),
                     "K_sweep": [1, 16], "variants": list(VARIANTS),
                     "replicas": list(REPLICAS), "MAC_per_cell": "512*O*m"},
        "qualification": {"units": q_units, "selection_events": q_counts["reference_selection_ggml"] + q_counts["reference_selection_torch"],
                          "comparison_events": q_counts["competency"],
                          "selection_samples": 31, "comparison_samples": 31,
                          "selection_rule": "median reference-only candidate rate; ties by frozen provider id",
                          "comparison_slots": ["omega", "ggml_winner", "torch_winner"],
                          "winner_position_frozen": True,
                          "auxiliary_units": [qualification_auxiliary]},
        "qualification_auxiliary_events": qualification_auxiliary["events"],
        "qualification_auxiliary_event_count": qualification_auxiliary["event_count"],
        "conditions": conditions,
        "condition_count": len(conditions),
        "calibration_units": cal_units,
        "calibration_unit_count": len(cal_units),
        "residency_blocks": residency_blocks,
        "S25_replica_orders": replica_orders,
        "S25_replica_orders_sha256": hashlib.sha256(canonical_bytes(replica_orders)).hexdigest(),
        "S25_replica_order_algorithm": "SHA256_ASCII(T0P-S25|4F4D454741543031|replica=<decimal>|W=<KiB>), ascending; tie W ascending",
        "S25_pause_rule": "between complete replica segments only; never within the 16 W blocks of a replica",
        "aa_pairs": pairs,
        "aa_pair_count": len(pairs),
        "segments": {key: value for key, value in sorted(segment_map.items())},
        "acquisition": {"warmup": 5, "qualification_minimum_seconds": 2.0,
                        "qualification_target_seconds": 2.5,
                        "residency_condition_target_seconds": 2.5,
                        "residency_condition_minimum_seconds": 2.0,
                        "residency_kernel_target_seconds": 0.150,
                        "residency_kernel_minimum_seconds": 0.100,
                        "margin_seconds_each_side": 0.200,
                        "R_policy": "R(W,m,K), common A/B/C, calibrated before N=31, fixed during N=31; A/A uses R(W,m,16)",
                        "qualification_R_policy": "one R per (configuration,m,frontier), fixed and common across workers and all 31 paired samples",
                        "no_idle_fill": True, "segments_reverify_seconds": 180,
                        "qualification_units_per_segment": QUALIFICATION_PER_SEGMENT,
                        "residency_blocks_per_segment": W_PER_REPLICA_SEGMENT,
                        "residency_segments": 31,
                        "single_global_technical_retry": True},
        "qualification_events": q_events,
        "events": all_events,
        "event_count": len(all_events),
        "qualification_event_count": len(q_events),
        "residency_event_count": len(r_events),
        "qualification_order_sha256": hashlib.sha256(canonical_bytes([event["id"] for event in q_events])).hexdigest(),
        "residency_order_sha256": hashlib.sha256(canonical_bytes([event["id"] for event in r_events])).hexdigest(),
        "selection_event_count": q_counts["reference_selection_ggml"] + q_counts["reference_selection_torch"],
        "comparison_event_count": q_counts["competency"],
        "residency_calibration_event_count": sum(e["phase"] == "calibration" for e in r_events),
        "residency_sample_event_count": sum(e["phase"] == "residency" for e in r_events),
        "aa_observation_event_count": sum(e["phase"] == "aa" for e in r_events),
    }
    payload["sha256"] = digest(payload)
    payload["order_sha256"] = hashlib.sha256(canonical_bytes(event_ids)).hexdigest()
    # The order hash is part of the frozen plan, so refresh the plan hash once.
    payload["sha256"] = digest({k: v for k, v in payload.items() if k != "sha256"})
    return payload


def _pairs_no_duplicates(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise PlanError(f"duplicate_json_key:{key}")
        result[key] = value
    return result


def verify_order(plan: dict[str, Any], acquired: Iterable[str]) -> None:
    expected = [event["id"] for event in plan["events"]]
    actual = list(acquired)
    if Counter(actual) != Counter(expected):
        dup = next((x for x, n in Counter(actual).items() if n > 1), None)
        if dup is not None:
            raise PlanError(f"executed_event_duplicate:{dup}")
        missing = len(set(expected) - set(actual))
        extra = len(set(actual) - set(expected))
        raise PlanError(f"executed_event_multiplicity_mismatch:missing={missing}:extra={extra}")
    if actual != expected:
        raise PlanError("executed_event_order_mismatch")


def verify_s25_replica_order(plan: dict[str, Any]) -> None:
    expected_orders = s25_replica_orders()
    stated_orders = plan.get("S25_replica_orders")
    if stated_orders != expected_orders:
        raise PlanError("S25_replica_order_hash_input_or_sequence_invalid")
    expected_hash = hashlib.sha256(canonical_bytes(expected_orders)).hexdigest()
    if plan.get("S25_replica_orders_sha256") != expected_hash:
        raise PlanError("S25_replica_orders_sha256_invalid")
    blocks = plan.get("residency_blocks", [])
    events = plan.get("events", [])
    for order in expected_orders:
        replica = order["replica"]
        segment = f"R{replica:02d}"
        actual_blocks = [block for block in blocks if block.get("replica") == replica]
        if ([block.get("W_target_kib") for block in actual_blocks] != order["W_target_kib"] or
                any(block.get("segment_id") != segment for block in actual_blocks) or
                len({block.get("id") for block in actual_blocks}) != 16):
            raise PlanError(f"S25_replica_segment_split_or_W_order_invalid:{replica}")
        positions = [i for i, event in enumerate(events) if event.get("segment_id") == segment]
        if not positions or positions != list(range(min(positions), max(positions) + 1)):
            raise PlanError(f"S25_replica_segment_interleaved:{replica}")
        for block in actual_blocks:
            rows = [i for i, event in enumerate(events) if event.get("block_id") == block["id"]]
            if not rows or rows != list(range(min(rows), max(rows) + 1)):
                raise PlanError(f"S25_W_replica_block_split:{block['id']}")
            block_rows = [events[i] for i in rows]
            if any(row.get("within_block_order_sha256") != block.get("within_block_order_sha256") or
                   row.get("within_block_seed") != SEED for row in block_rows):
                raise PlanError(f"S25_within_block_permutation_unfrozen:{block['id']}")


def verify_qualification_auxiliary(plan: dict[str, Any]) -> None:
    auxiliary = plan.get("qualification_auxiliary_events", [])
    if len(auxiliary) != 372 or plan.get("qualification_auxiliary_event_count") != 372:
        raise PlanError("qualification_auxiliary_event_count_invalid")
    ids = [row.get("id") for row in auxiliary]
    if len(set(ids)) != len(ids):
        raise PlanError("qualification_auxiliary_duplicate_id")
    units = plan.get("qualification", {}).get("units", [])
    target = next((row for row in units if row.get("core") == "8P" and row.get("m") == 1 and
                   row.get("frontier") == "prepared" and row.get("O") == 1024), None)
    if target is None or not target.get("segment_id"):
        raise PlanError("qualification_auxiliary_shared_unit_missing")
    if any(row.get("segment_id") != target["segment_id"] or
           row.get("unit_id") != target["id"] for row in auxiliary):
        raise PlanError("BW_DRAM_HKQ_Dprime_not_in_same_qualification_segment")
    counts = Counter(row.get("provider") for row in auxiliary)
    if counts != Counter({"BW_DRAM": 31, "P_instr": 279, "H-KQ": 31, "D_prime": 31}):
        raise PlanError(f"qualification_auxiliary_provider_counts_invalid:{dict(counts)}")
    for row in auxiliary:
        if row.get("provider") == "D_prime" and len(row.get("source_event_ids", [])) != 2:
            raise PlanError("D_prime_source_provenance_missing")


def verify_block_order(plan: dict[str, Any], acquired_events: Iterable[dict[str, Any]]) -> None:
    rows = list(acquired_events)
    verify_order(plan, [row.get("event_id", row.get("id")) for row in rows])
    expected = plan["events"]
    for index, (row, event) in enumerate(zip(rows, expected), 1):
        if row.get("sequence", index) != index or row.get("event_id", row.get("id")) != event["id"]:
            raise PlanError(f"executed_event_sequence_or_id_mismatch:{index}")
        for key in ("unit_id", "block_id", "segment_id", "pair_id", "atomic_group_id"):
            if key in event and row.get(key) != event[key]:
                raise PlanError(f"executed_{key}_mismatch:{index}")


def verify_union(plan: dict[str, Any], executed: Iterable[str], derived: Iterable[str]) -> None:
    verify_order(plan, executed)
    verify_order(plan, derived)


def verify_plan(path: Path) -> dict[str, Any]:
    try:
        plan = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=_pairs_no_duplicates,
                          parse_constant=lambda value: (_ for _ in ()).throw(PlanError(f"nonfinite:{value}")))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise PlanError(f"plan_read_or_parse_failed:{exc}") from exc
    if not isinstance(plan, dict) or plan.get("schema") != SCHEMA:
        raise PlanError("plan_schema_invalid")
    stated = plan.get("sha256")
    unsigned = dict(plan)
    unsigned.pop("sha256", None)
    if not isinstance(stated, str) or digest(unsigned) != stated:
        raise PlanError("plan_sha256_mismatch")
    expected = build_payload()
    if plan != expected:
        raise PlanError("plan_order_or_contract_mismatch")
    if (plan["condition_count"], plan["calibration_unit_count"], plan["aa_pair_count"],
        plan["selection_event_count"], plan["comparison_event_count"]) != (420, 140, 124, 11718, 6696):
        raise PlanError("plan_counts_invalid")
    if plan["event_count"] != 32102 or plan["qualification"]["units"].__len__() != 72:
        raise PlanError("plan_event_or_qualification_unit_count_invalid")
    verify_s25_replica_order(plan)
    verify_qualification_auxiliary(plan)
    verify_order(plan, [event["id"] for event in plan["events"]])
    return plan


def smoke_schedule(plan: dict[str, Any], replicas: int = 3) -> list[dict[str, Any]]:
    """Choose N complete W=288 blocks from the frozen plan for a diagnostic smoke."""
    if replicas != 3:
        raise PlanError("diagnostic_smoke_requires_N3")
    events = [event for event in plan["events"]
              if event.get("phase") in {"calibration", "residency", "aa"}]
    block_rows = [block for block in plan["residency_blocks"] if block["W_target_kib"] == 288]
    sequence_by_block = {}
    for event in events:
        if event.get("phase") in {"residency", "aa"}:
            sequence_by_block.setdefault(event["block_id"], event["sequence"])
    ordered_blocks = sorted(block_rows, key=lambda block: sequence_by_block[block["id"]])
    candidates = [block for block in ordered_blocks if block["replica"] in {1, 2, 3}]
    if len(candidates) != replicas or [block["replica"] for block in candidates] != [1, 2, 3]:
        raise PlanError("smoke_replica1_calibration_block_missing")
    if (len(candidates) != replicas or any(block["W_target_kib"] != 288 for block in candidates)
            or [block["segment_id"] for block in candidates] != ["R01", "R02", "R03"]):
        raise PlanError("smoke_complete_W288_blocks_not_in_replica_segments")
    selected_blocks = {block["id"] for block in candidates}
    selected = [event for event in events
                if event.get("block_id") in selected_blocks or
                (event.get("phase") == "calibration" and event.get("W_target_kib") == 288)]
    selected.sort(key=lambda event: event["sequence"])
    if len({event["id"] for event in selected}) != len(selected):
        raise PlanError("smoke_schedule_duplicate_event")
    if sum(event["phase"] == "calibration" for event in selected) != 60:
        raise PlanError("smoke_schedule_calibration_multiplicity_invalid")
    if sum(event["phase"] == "residency" for event in selected) != 180:
        raise PlanError("smoke_schedule_residency_multiplicity_invalid")
    if sum(event["phase"] == "aa" for event in selected) != 24:
        raise PlanError("smoke_schedule_AA_multiplicity_invalid")
    return selected


def write_smoke_schedule(plan_path: Path, output: Path) -> None:
    plan = verify_plan(plan_path)
    selected = smoke_schedule(plan)
    lines = [f"#plan_sha256={plan['sha256']}",
             "#diagnostic=SMOKE_ONLY; complete W288 blocks for replicas 1..3; segments R01,R02,R03",
             "sequence\tevent_id\tphase\tblock_id\tatomic_group_id\tsegment_id"]
    lines.extend("\t".join(str(event.get(key, "")) for key in
                            ("sequence", "id", "phase", "block_id", "atomic_group_id", "segment_id"))
                 for event in selected)
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        raise PlanError(f"refusing_schedule_replace:{output}")
    output.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"RESIDENCY_PLAN_R18_SMOKE_SCHEDULE PASS N=3 complete_W288_replica_blocks=3 segments=R01,R02,R03 calibration=60 residency=180 AA=24 events={len(selected)} plan_sha256={plan['sha256']} output={output}")


def self_test(out: Path) -> list[str]:
    plan = build_payload()
    ids = [event["id"] for event in plan["events"]]
    verify_order(plan, ids)
    cases = []
    mutations: list[tuple[str, list[str], str]] = []
    mutations.append(("duplicate", ids[:-1] + [ids[-2], ids[-1]], "executed_event_duplicate"))
    mutations.append(("missing", ids[:-1], "multiplicity_mismatch"))
    swapped = ids.copy(); swapped[0], swapped[1] = swapped[1], swapped[0]
    mutations.append(("reordered", swapped, "order_mismatch"))
    aa = next(e for e in plan["events"] if e["phase"] == "aa")
    aa_pair = [e for e in plan["events"] if e.get("pair_id") == aa["pair_id"]]
    if len(aa_pair) != 2:
        raise PlanError("fixture_AA_pair_cardinality_invalid")
    separated = ids.copy()
    i0, i1 = sorted((separated.index(aa_pair[0]["id"]), separated.index(aa_pair[1]["id"])))
    if i1 + 1 < len(separated):
        separated[i1], separated[i1 + 1] = separated[i1 + 1], separated[i1]
    else:
        separated[i0 - 1], separated[i0] = separated[i0], separated[i0 - 1]
    mutations.append(("AA_pair_separated", separated, "order_mismatch"))
    for name, altered, expected in mutations:
        try:
            verify_order(plan, altered)
        except PlanError as exc:
            if expected not in str(exc):
                raise PlanError(f"mutation_{name}_wrong_rejection:{exc}") from exc
            cases.append(f"{name}=REJECTED")
        else:
            raise PlanError(f"mutation_{name}_accepted")
    # Explicitly assert every A/A pair is adjacent and every block/unit stays
    # wholly within one segment and one contiguous sequence region.
    positions = {event["id"]: i for i, event in enumerate(plan["events"])}
    for pair in plan["aa_pairs"]:
        pfx = f"aa|{pair['id']}|"
        pair_ids = [event["id"] for event in plan["events"] if event["id"].startswith(pfx)]
        if len(pair_ids) != 2 or abs(positions[pair_ids[0]] - positions[pair_ids[1]]) != 1:
            raise PlanError(f"AA_pair_not_adjacent:{pair['id']}")
    verify_s25_replica_order(plan)
    verify_qualification_auxiliary(plan)
    split_plan = json.loads(json.dumps(plan))
    split_plan["residency_blocks"][0]["segment_id"] = "R99"
    try:
        verify_s25_replica_order(split_plan)
    except PlanError as exc:
        if "segment_split" not in str(exc) and "W_order" not in str(exc):
            raise PlanError(f"S25_segment_mutation_wrong_rejection:{exc}") from exc
        cases.append("replica_W_split=REJECTED")
    else:
        raise PlanError("S25_replica_W_segment_split_accepted")
    auxiliary_mutation = json.loads(json.dumps(plan))
    next(row for row in auxiliary_mutation["qualification_auxiliary_events"]
         if row["provider"] == "BW_DRAM")["segment_id"] = "Q99"
    try:
        verify_qualification_auxiliary(auxiliary_mutation)
    except PlanError as exc:
        if "same_qualification_segment" not in str(exc):
            raise PlanError(f"OP4_BW_DRAM_mutation_wrong_rejection:{exc}") from exc
        cases.append("BW_DRAM_segment_mismatch=REJECTED")
    else:
        raise PlanError("OP4_BW_DRAM_segment_mismatch_accepted")
    if plan["selection_event_count"] + plan["comparison_event_count"] != 18414:
        raise PlanError("qualification_event_count_invalid")
    smoke = smoke_schedule(plan)
    if len(smoke) != 264 or sorted({event["segment_id"] for event in smoke if event["segment_id"]}) != ["R01", "R02", "R03"]:
        raise PlanError("smoke_schedule_not_three_complete_blocks")
    out.parent.mkdir(parents=True, exist_ok=True)
    lines = ["RESIDENCY_PLAN_R18_QA PASS",
             "qualification_units=72 selection=11718 comparison=6696 winner_slots=OMEGA,ggml_winner,Torch_winner PASS",
             "residency conditions=420 calibration_units=140 W_replica_blocks=496 AA_pairs=124 PASS",
             "segments=qualification:6 residency:31 (16 W blocks per replica segment) indivisible_unit_boundaries=PASS",
             f"events={plan['event_count']} order_sha256={plan['order_sha256']} plan_sha256={plan['sha256']}",
             "mutations=" + ",".join(cases) + " multiplicity_order_AA_adjacency=PASS",
             "S25-C=PASS 31_replica_orders hash=PASS 16_unique_W_per_segment=PASS pause_only_between_replica_segments=PASS",
             "OP4=PASS BW_DRAM+H-KQ+Dprime planned in one Q segment; auxiliary_events=372",
             "smoke_N3=PASS complete_W288_replica_blocks=3 calibration=60 residency=180 AA=24 segments=R01,R02,R03"]
    out.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(" | ".join(lines))
    return lines


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    gen = sub.add_parser("generate"); gen.add_argument("--output", type=Path, default=DEFAULT_PLAN)
    verify = sub.add_parser("verify"); verify.add_argument("--plan", type=Path, default=DEFAULT_PLAN)
    verify.add_argument("--executed", type=Path); verify.add_argument("--derived", type=Path)
    test = sub.add_parser("self-test"); test.add_argument("--output", type=Path, default=DEFAULT_EVIDENCE)
    smoke = sub.add_parser("smoke-schedule"); smoke.add_argument("--plan", type=Path, default=DEFAULT_PLAN)
    smoke.add_argument("--output", type=Path, required=True)
    ns = parser.parse_args(argv)
    try:
        if ns.command == "generate":
            plan = build_payload()
            ns.output.parent.mkdir(parents=True, exist_ok=True)
            if ns.output.exists():
                raise PlanError(f"refusing_plan_replace:{ns.output}")
            ns.output.write_text(json.dumps(plan, ensure_ascii=True, indent=2, allow_nan=False) + "\n",
                                 encoding="utf-8", newline="\n")
            print(f"RESIDENCY_PLAN_R18_GENERATED qualifications=72 selection=11718 comparison=6696 conditions=420 calibration=140 blocks=496 AA_pairs=124 events={plan['event_count']} sha256={plan['sha256']} path={ns.output}")
        elif ns.command == "verify":
            plan = verify_plan(ns.plan)
            if bool(ns.executed) != bool(ns.derived):
                raise PlanError("both_executed_and_derived_required")
            if ns.executed:
                def read_ids(path: Path) -> list[str]:
                    rows = [json.loads(line) for line in path.read_text(encoding="utf-8-sig").splitlines() if line.strip()]
                    return [row.get("event_id", row.get("id")) for row in rows]
                verify_union(plan, read_ids(ns.executed), read_ids(ns.derived))
                exact = " exact_order_and_multiplicity=PASS"
            else:
                exact = " exact_order_and_multiplicity=NOT_CHECKED"
            print(f"RESIDENCY_PLAN_R18_VERIFY PASS qualification_units=72 selection=11718 comparison=6696 conditions=420 calibration_units=140 W_replica_blocks=496 AA_pairs=124 events={plan['event_count']}{exact} sha256={plan['sha256']}")
        elif ns.command == "smoke-schedule":
            write_smoke_schedule(ns.plan, ns.output)
        else:
            self_test(ns.output)
        return 0
    except (OSError, PlanError, json.JSONDecodeError, KeyError, TypeError, ValueError) as exc:
        print(f"RESIDENCY_PLAN_R18 REJECT {type(exc).__name__}: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
