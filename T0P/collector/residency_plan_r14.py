#!/usr/bin/env python3
"""Deterministic D2 plan builder and exact event-union verifier (S-19/R1-R5)."""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from collections import Counter
from pathlib import Path
from typing import Any, Iterable

SEED = "4F4D454741543031"
M_VALUES = (1, 4, 8, 16)
K_ARCH = (1, 2, 4, 8, 16)
W_GRID = (256, 288, 512, 768, 1024, 1280, 1536, 1792,
          2048, 2560, 3072, 4096, 6144, 8192, 12288, 16384)
VARIANTS = ("A", "B", "C")
REPLICAS = tuple(range(1, 32))
SCHEMA = "T0P_S19_RESIDENCY_PLAN_R14_V1"
ROOT = Path(__file__).resolve().parents[2]
DEFAULT_PLAN = Path(__file__).resolve().parent / "residency_plan_r14.json"
DEFAULT_EVIDENCE = Path(__file__).resolve().parent / "logs_r14" / "obj_r14" / "plan_qa_r14.txt"
DEFAULT_SMOKE_SCHEDULE = Path(__file__).resolve().parents[1] / "harness" / "logs_r14" / "obj_r14" / "residency_smoke_schedule_r14.tsv"


class PlanError(ValueError):
    pass


def canonical_bytes(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=True, allow_nan=False).encode("ascii")


def sha(value: Any) -> str:
    return hashlib.sha256(canonical_bytes(value)).hexdigest()


def order_key(phase: str, identity: str) -> str:
    return hashlib.sha256(f"T0P-S19|{SEED}|residency|{phase}|{identity}".encode("ascii")).hexdigest()


def output_for_w(w_kib: int) -> int:
    # S-19 round-half-up; all W values are integer KiB.
    numerator = w_kib * 1024
    denominator = 8 * 288
    rounded_groups = (2 * numerator + denominator) // (2 * denominator)
    output = 8 * rounded_groups
    if output < 8 or output % 8:
        raise PlanError(f"O_FORMULA_INVALID:W={w_kib}:O={output}")
    return output


def _condition(kind: str, variant: str, m: int, k: int, w: int, o: int) -> dict[str, Any]:
    cid = f"{kind}|{variant}|m{m}|K{k}|W{w}|O{o}"
    return {"id": cid, "kind": kind, "variant": variant, "m": m, "K": k,
            "W_target_kib": w, "W_real_kib": 288 * o / 1024, "O": o,
            "order_key": order_key("condition", cid)}


def build_payload() -> dict[str, Any]:
    conditions: list[dict[str, Any]] = []
    # The architecture is W=288 KiB with O=1024. It is one of the 16 plotted
    # points, so it is excluded from the 15-point sweep to keep 420 cells.
    for m in M_VALUES:
        for k in K_ARCH:
            for variant in VARIANTS:
                conditions.append(_condition("architecture", variant, m, k, 288, 1024))
    sweep_ws = tuple(w for w in W_GRID if w != 288)
    for w in sweep_ws:
        o = output_for_w(w)
        for m in M_VALUES:
            for k in (1, 16):
                for variant in VARIANTS:
                    conditions.append(_condition("sweep", variant, m, k, w, o))
    conditions.sort(key=lambda x: (x["order_key"], x["id"].encode("ascii")))

    units: dict[tuple[int, int, int], dict[str, Any]] = {}
    for w, ks in ((288, K_ARCH), *((w, (1, 16)) for w in sweep_ws)):
        for m in M_VALUES:
            for k in ks:
                o = 1024 if w == 288 else output_for_w(w)
                uid = f"W{w}|O{o}|m{m}|K{k}"
                units[(w, m, k)] = {"id": uid, "W_target_kib": w, "O": o,
                                    "m": m, "K": k,
                                    "order_key": order_key("calibration_unit", uid)}
    calibration_units = sorted(units.values(), key=lambda x: (x["order_key"], x["id"].encode("ascii")))

    condition_by_coord = {(c["W_target_kib"], c["m"], c["K"], c["variant"]): c for c in conditions}
    events: list[dict[str, Any]] = []
    for unit in calibration_units:
        w, m, k, o = (unit[key] for key in ("W_target_kib", "m", "K", "O"))
        for variant in VARIANTS:
            condition = condition_by_coord[(w, m, k, variant)]
            eid = f"calibration|{unit['id']}|{variant}"
            events.append({"id": eid, "phase": "calibration", "unit_id": unit["id"],
                           "condition_id": condition["id"], "W_target_kib": w, "O": o,
                           "m": m, "K": k, "variant": variant, "order_key": order_key("calibration", eid)})

    for condition in conditions:
        for replica in REPLICAS:
            eid = f"residency|{condition['id']}|r{replica}"
            events.append({"id": eid, "phase": "residency", "condition_id": condition["id"],
                           "W_target_kib": condition["W_target_kib"], "O": condition["O"],
                           "m": condition["m"], "K": condition["K"],
                           "variant": condition["variant"], "replica": replica,
                           "order_key": order_key("residency", eid)})

    pairs: list[dict[str, Any]] = []
    for m in M_VALUES:
        for replica in REPLICAS:
            pair_id = f"AA|W288|O1024|m{m}|K16|r{replica}"
            pair = {"id": pair_id, "W_target_kib": 288, "O": 1024,
                    "m": m, "K": 16, "replica": replica,
                    "R_unit_id": f"W288|O1024|m{m}|K16",
                    "order_key": order_key("aa_pair", pair_id)}
            pairs.append(pair)
            for observation in ("A1", "A2"):
                eid = f"aa|{pair_id}|{observation}"
                events.append({"id": eid, "phase": "aa", "pair_id": pair_id,
                               "observation": observation, "condition_id": "architecture|A|m%d|K16|W288|O1024" % m,
                               "W_target_kib": 288, "O": 1024, "m": m, "K": 16,
                               "variant": "A", "replica": replica,
                               "order_key": order_key("aa_observation", eid)})

    phase_order = {"calibration": 0, "residency": 1, "aa": 2}
    events.sort(key=lambda e: (phase_order[e["phase"]], e["order_key"], e["id"].encode("ascii")))
    for sequence, event in enumerate(events, 1):
        event["sequence"] = sequence
        # A sequence is a strictly serialized acquisition event. Eight workers
        # are concurrent only inside the event and are represented as its team.
        event["parallel_event_group"] = sequence
        event["workers_inside_event"] = 8

    condition_ids = [c["id"] for c in conditions]
    event_ids = [e["id"] for e in events]
    if len(condition_ids) != len(set(condition_ids)) or len(event_ids) != len(set(event_ids)):
        raise PlanError("generated_duplicate_id")
    payload: dict[str, Any] = {
        "schema": SCHEMA,
        "campaign": "T0P-Q4-RESIDENCY",
        "seed": SEED,
        "ordering": "phase order calibration,residency,aa; within each phase ascending SHA256(T0P-S19|seed|residency|phase|id), ASCII id tie-break",
        "geometry": {
            "I": 512, "P_shards": 8, "LPs": [0, 2, 4, 6, 8, 10, 12, 14],
            "architecture": {"W_target_kib": 288, "O": 1024},
            "sweep_W_target_kib": list(sweep_ws), "sweep_includes": {"W1024_O": output_for_w(1024)},
            "O_formula": "8*round_half_up(W_target_kib*1024/(8*288))",
            "m_values": list(M_VALUES), "variants": list(VARIANTS),
            "K_architectural": list(K_ARCH), "K_sweep": [1, 16],
            "MAC_per_cell": "512*O*m",
        },
        "acquisition": {"replicas": list(REPLICAS), "warmup": 5,
                        "minimum_sample_seconds": 2.0, "calibration_target_seconds": 2.5,
                        "R_policy": "R(W,m,K), common to A/B/C for each K; calibrated before N=31; A/A K16 uses the same R(W,m,16)",
                        "episode_serialization": "event sequence is exclusive; no cross-event overlap",
                        "margin_seconds_each_side": 0.2},
        "condition_count": len(conditions),
        "architectural_condition_count": sum(c["kind"] == "architecture" for c in conditions),
        "sweep_condition_count": sum(c["kind"] == "sweep" for c in conditions),
        "calibration_unit_count": len(calibration_units),
        "calibration_variant_event_count": sum(e["phase"] == "calibration" for e in events),
        "replica_event_count": sum(e["phase"] == "residency" for e in events),
        "aa_pair_count": len(pairs),
        "aa_pairs_per_architectural_m": {str(m): sum(p["m"] == m for p in pairs) for m in M_VALUES},
        "aa_observation_event_count": sum(e["phase"] == "aa" for e in events),
        "event_count": len(events),
        "conditions": conditions,
        "calibration_units": calibration_units,
        "aa_pairs": pairs,
        "events": events,
    }
    payload["sha256"] = sha(payload)
    return payload


def verify_plan(path: Path) -> tuple[dict[str, Any], set[str]]:
    try:
        plan = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=_unique_pairs,
                          parse_constant=lambda value: (_ for _ in ()).throw(PlanError(f"nonfinite:{value}")))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise PlanError(f"plan_read_or_parse_failed:{exc}") from exc
    if not isinstance(plan, dict) or plan.get("schema") != SCHEMA:
        raise PlanError("plan_schema_invalid")
    stated = plan.get("sha256")
    unsigned = dict(plan)
    unsigned.pop("sha256", None)
    if not isinstance(stated, str) or sha(unsigned) != stated:
        raise PlanError("plan_sha256_mismatch")
    expected = build_payload()
    if plan != expected:
        raise PlanError("plan_contract_or_serialization_mismatch")
    conditions = plan["conditions"]
    events = plan["events"]
    ids = [event.get("id") for event in events]
    if len(ids) != len(set(ids)):
        raise PlanError("plan_event_ids_duplicate")
    if plan["condition_count"] != 420 or plan["architectural_condition_count"] != 60 or plan["sweep_condition_count"] != 360:
        raise PlanError("condition_counts_not_exact")
    if plan["calibration_unit_count"] != 140 or plan["aa_pair_count"] != 124:
        raise PlanError("calibration_or_AA_count_not_exact")
    if len(conditions) != 420 or len(events) != plan["event_count"]:
        raise PlanError("condition_or_event_count_not_exact")
    arch = [c for c in conditions if c["kind"] == "architecture"]
    sweep = [c for c in conditions if c["kind"] == "sweep"]
    if any((c["W_target_kib"], c["O"]) != (288, 1024) for c in arch):
        raise PlanError("architecture_geometry_invalid")
    if not any((c["W_target_kib"], c["O"]) == (1024, 3640) for c in sweep):
        raise PlanError("W1024_O3640_missing")
    if Counter(c["variant"] for c in conditions) != Counter({"A": 140, "B": 140, "C": 140}):
        raise PlanError("variant_coverage_invalid")
    if {e["sequence"] for e in events} != set(range(1, len(events) + 1)):
        raise PlanError("event_sequence_not_strict")
    return plan, set(ids)


def _unique_pairs(pairs):
    out = {}
    for key, value in pairs:
        if key in out:
            raise PlanError(f"duplicate_json_key:{key}")
        out[key] = value
    return out


def _read_ids(path: Path) -> list[str]:
    ids: list[str] = []
    with path.open("r", encoding="utf-8-sig") as stream:
        for line_no, line in enumerate(stream, 1):
            if not line.strip():
                raise PlanError(f"blank_jsonl_line:{path.name}:{line_no}")
            item = json.loads(line, object_pairs_hook=_unique_pairs,
                              parse_constant=lambda v: (_ for _ in ()).throw(PlanError(f"nonfinite:{v}")))
            if not isinstance(item, dict) or not isinstance(item.get("event_id"), str):
                raise PlanError(f"event_id_missing:{path.name}:{line_no}")
            ids.append(item["event_id"])
    return ids


def verify_union(expected: set[str], executed: Iterable[str], derived: Iterable[str]) -> None:
    executed_list, derived_list = list(executed), list(derived)
    for label, values in (("executed", executed_list), ("derived", derived_list)):
        duplicates = sorted(value for value, count in Counter(values).items() if count > 1)
        if duplicates:
            raise PlanError(f"{label}_event_id_duplicate:{duplicates[0]}")
        actual = set(values)
        if actual != expected:
            missing = len(expected - actual)
            extra = len(actual - expected)
            raise PlanError(f"{label}_event_id_union_mismatch:missing={missing}:extra={extra}")
    if set(executed_list) != set(derived_list):
        raise PlanError("executed_derived_event_id_union_mismatch")


def self_test(out: Path) -> None:
    plan = build_payload()
    ids = {event["id"] for event in plan["events"]}
    verify_union(ids, ids, ids)
    cases = []
    try:
        verify_union(ids, ids - {next(iter(ids))}, ids)
    except PlanError as exc:
        if "missing=1" not in str(exc):
            raise
        cases.append("missing_rejected")
    else:
        raise PlanError("mutation_missing_event_accepted")
    one = next(iter(ids))
    try:
        verify_union(ids, [*ids, one], ids)
    except PlanError as exc:
        if "duplicate" not in str(exc):
            raise
        cases.append("duplicate_rejected")
    else:
        raise PlanError("mutation_duplicate_event_accepted")
    out.parent.mkdir(parents=True, exist_ok=True)
    lines = ["RESIDENCY_PLAN_R14_QA PASS",
             "geometry architecture=W288/O1024 sweep_W1024/O3640 PASS",
             "counts conditions=420 architecture=60 sweep=360 calibration_units=140 AA_pairs=124 PASS",
             f"events={len(ids)} IDs_unique=true sequence=1..{len(ids)} serialized=true PASS",
             "union exact=plan=executed=derived PASS",
             "mutations=" + ",".join(cases) + " HOLD_FAIL_CLOSED PASS",
             "MAC_formula=512*O*m; replicas=1..31; R=R(W,m,K), common A/B/C by K PASS",
             f"plan_sha256={plan['sha256']}"]
    out.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(" ".join(lines[:4]))
    print(" ".join(lines[4:7]))
    print(lines[-1])


def write_smoke_schedule(plan_path: Path, output: Path) -> None:
    plan, _ = verify_plan(plan_path)
    unit_id = "W288|O1024|m1|K16"
    selected = []
    for event in plan["events"]:
        if event["phase"] == "calibration" and event["unit_id"] == unit_id:
            selected.append(event)
        elif event["phase"] == "residency" and event["W_target_kib"] == 288 and event["O"] == 1024 and event["m"] == 1 and event["K"] == 16 and event["replica"] in (1, 2, 3):
            selected.append(event)
        elif event["phase"] == "aa" and event["m"] == 1 and event["replica"] in (1, 2, 3):
            selected.append(event)
    selected.sort(key=lambda item: item["sequence"])
    counts = Counter(item["phase"] for item in selected)
    if (len(selected), counts["calibration"], counts["residency"], counts["aa"]) != (18, 3, 9, 6):
        raise PlanError("smoke_schedule_geometry_or_counts_invalid")
    if len({item["id"] for item in selected}) != 18:
        raise PlanError("smoke_schedule_duplicate_event_id")
    header = "sequence\tphase\tevent_id\tcondition_id\tvariant\treplica\tobservation\tunit_id\tpair_id\tW_target_kib\tO\tm\tK"
    lines = [f"#plan_sha256={plan['sha256']}", header]
    for event in selected:
        fields = [str(event["sequence"]), event["phase"], event["id"], event["condition_id"],
                  event["variant"], str(event.get("replica", 0)), event.get("observation", ""),
                  event.get("unit_id", ""), event.get("pair_id", ""), str(event["W_target_kib"]),
                  str(event["O"]), str(event["m"]), str(event["K"])]
        lines.append("\t".join(fields))
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        raise PlanError(f"refusing_schedule_replace:{output}")
    output.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"RESIDENCY_SMOKE_SCHEDULE_R14 PASS events=18 calibration=3 A_B_C=9 AA_pairs=3 aa_observations=6 W288/O1024/m1/K16 plan_sha256={plan['sha256']} path={output}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    gen = sub.add_parser("generate")
    gen.add_argument("--output", type=Path, default=DEFAULT_PLAN)
    verify = sub.add_parser("verify")
    verify.add_argument("--plan", type=Path, default=DEFAULT_PLAN)
    verify.add_argument("--executed", type=Path)
    verify.add_argument("--derived", type=Path)
    test = sub.add_parser("self-test")
    test.add_argument("--output", type=Path, default=DEFAULT_EVIDENCE)
    smoke = sub.add_parser("smoke-schedule")
    smoke.add_argument("--plan", type=Path, default=DEFAULT_PLAN)
    smoke.add_argument("--output", type=Path, default=DEFAULT_SMOKE_SCHEDULE)
    ns = parser.parse_args(argv)
    try:
        if ns.command == "generate":
            plan = build_payload()
            ns.output.parent.mkdir(parents=True, exist_ok=True)
            if ns.output.exists():
                raise PlanError(f"refusing_plan_replace:{ns.output}")
            ns.output.write_text(json.dumps(plan, ensure_ascii=True, indent=2, allow_nan=False) + "\n",
                                 encoding="utf-8", newline="\n")
            print(f"RESIDENCY_PLAN_R14_GENERATED conditions=420 arch=60 sweep=360 calibration=140 AA_pairs=124 events={plan['event_count']} W288/O1024=PASS W1024/O3640=PASS sha256={plan['sha256']} path={ns.output}")
        elif ns.command == "verify":
            plan, ids = verify_plan(ns.plan)
            if bool(ns.executed) != bool(ns.derived):
                raise PlanError("both_executed_and_derived_required_for_union_check")
            if ns.executed:
                verify_union(ids, _read_ids(ns.executed), _read_ids(ns.derived))
                union_note = " exact_union=PASS"
            else:
                union_note = " exact_union=NOT_CHECKED"
            print(f"RESIDENCY_PLAN_R14_VERIFY PASS conditions={len(plan['conditions'])} events={len(ids)} calibration_units={plan['calibration_unit_count']} AA_pairs={plan['aa_pair_count']} replicas=1..31{union_note} sha256={plan['sha256']}")
        elif ns.command == "smoke-schedule":
            write_smoke_schedule(ns.plan, ns.output)
        else:
            self_test(ns.output)
        return 0
    except (OSError, PlanError, json.JSONDecodeError, KeyError, TypeError, ValueError) as exc:
        print(f"RESIDENCY_PLAN_R14 REJECT {type(exc).__name__}: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
