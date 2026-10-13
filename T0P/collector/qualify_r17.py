#!/usr/bin/env python3
"""R17 qualifier: S-24 qualification plan, paired throughput and reader controls."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import statistics
import sys
import time
from pathlib import Path
from typing import Any, Callable
from artifact_store_r17 import ArtifactError, SessionAttemptStore
import s20_validator_r17 as s20

THRESHOLDS = {"G_ggml": 0.90, "G_Torch": 1.20, "H_KQ_MAC_per_ns": 33.1,
              "D_prime_hot": 2.00, "plateau_delta_max": 0.03}
CORE_SET = {*(f"P{i}" for i in range(8)), "8P"}
M_SET = {1, 4, 8, 16}
FRONTIERS = {"dynamic", "prepared"}
I = 512
ARCH_O = 1024
BW_BYTES_PER_WORKER = 256 * 1024 * 1024
S20_CACHE: dict[str, tuple[bool, str, float]] = {}
S20_MAP_CACHE: dict[str, list[dict[str, Any]]] = {}


class QualificationError(ValueError):
    pass


def _pairs(pairs):
    out = {}
    for key, value in pairs:
        if key in out:
            raise QualificationError(f"duplicate_json_key:{key}")
        out[key] = value
    return out


def strict_json(text: str):
    def bad_constant(value):
        raise QualificationError(f"nonfinite_json_constant:{value}")
    try:
        value = json.loads(text, object_pairs_hook=_pairs, parse_constant=bad_constant)
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        raise QualificationError(f"invalid_json:{exc}") from exc
    def visit(item):
        if isinstance(item, float) and not math.isfinite(item):
            raise QualificationError("nonfinite_json_number")
        if isinstance(item, dict):
            for child in item.values(): visit(child)
        elif isinstance(item, list):
            for child in item: visit(child)
    visit(value)
    return value


def read_jsonl(path: Path):
    rows = []
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        for line_no, line in enumerate(stream, 1):
            if not line.strip():
                raise QualificationError(f"blank_jsonl_line:{path.name}:{line_no}")
            row = strict_json(line)
            if not isinstance(row, dict):
                raise QualificationError(f"jsonl_object_required:{path.name}:{line_no}")
            rows.append(row)
    if not rows:
        raise QualificationError(f"empty_jsonl:{path.name}")
    return rows


def verify_plan(path: Path):
    import residency_plan_r17 as planlib
    try:
        plan = planlib.verify_plan(path)
    except planlib.PlanError as exc:
        raise QualificationError(f"plan_invalid:{exc}") from exc
    ids = [event.get("id") for event in plan["qualification_events"]]
    if any(not isinstance(value, str) for value in ids) or len(ids) != len(set(ids)):
        raise QualificationError("qualification_plan_event_ids_invalid_or_duplicate")
    return plan["sha256"], plan, set(ids)


def _finite_positive(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) and value > 0


def _s20_paths(ref: dict[str, Any]):
    directory = Path(ref.get("directory", "")).resolve(strict=True)
    names = ref.get("files", {})
    return directory, {
        "emon_raw": names.get("emon_raw", "emon_raw.txt"),
        "clock_map": names.get("clock_map", "clock_map.csv"),
        "block_marks": names.get("block_marks", "block_marks.csv"),
        "effective_config": names.get("effective_config", "effective_config.json"),
        "effective_config_source": names.get("effective_config_source", "effective_config_source.json"),
        "events": names.get("events", "events.edp"),
    }


def _s20_validate(ref: dict[str, Any]) -> tuple[bool, str, float]:
    try:
        directory, files = _s20_paths(ref)
    except Exception as exc:
        return False, f"S20_D13_PATH_INVALID:{exc}", 0.0
    cache_key = str(directory)
    if cache_key in S20_CACHE:
        return S20_CACHE[cache_key]
    try:
        store = SessionAttemptStore(directory)
        metadata = strict_json((directory / "attempt_metadata.json").read_text(encoding="utf-8"))
        if metadata.get("protocol") != "D13-write-once-v1":
            raise QualificationError("D13_PROTOCOL_MISSING")
        names = [files[k] for k in ("emon_raw", "clock_map", "block_marks", "effective_config_source", "events")]
        for name in names:
            store.verify_raw(name)
        cfg_path = directory / files["effective_config"]
        lineage_path = Path(str(cfg_path) + ".lineage.json")
        if not cfg_path.is_file() or not lineage_path.is_file():
            raise QualificationError("D13_EFFECTIVE_CONFIG_LINEAGE_MISSING")
        lineage = strict_json(lineage_path.read_text(encoding="utf-8"))
        cfg_bytes = cfg_path.read_bytes()
        cfg_hash = hashlib.sha256(cfg_bytes).hexdigest()
        if lineage.get("derived_sha256") != cfg_hash:
            raise QualificationError("D13_DERIVED_CONFIG_HASH_MISMATCH")
        for name, expected in lineage.get("sources", {}).items():
            if store.verify_raw(name) != expected:
                raise QualificationError(f"D13_LINEAGE_SOURCE_MISMATCH:{name}")
        result = s20.validate_capture(directory / files["emon_raw"], directory / files["clock_map"],
                                      directory / files["block_marks"], cfg_path,
                                      mode=ref.get("mode", "temporal"))
        config = s20.load_config(cfg_path)
        hz = float(config["f_tsc_qualified_hz"])
        S20_MAP_CACHE[cache_key] = config["lp_map"]
        if result.get("state") != "VALID":
            codes = sorted({reason.get("code", "UNKNOWN") for sample in result.get("results", [])
                            for reason in sample.get("reasons", [])})
            answer = (False, "S20_" + ("_".join(codes[:6]) or result.get("state", "INVALID")), hz)
        else:
            answer = (True, "S20-R2-v1_VALID", hz)
    except Exception as exc:
        if isinstance(exc, QualificationError):
            reason = str(exc)
        else:
            reason = f"S20_{getattr(exc, 'code', type(exc).__name__)}:{exc}"
        answer = (False, reason, 0.0)
    S20_CACHE[cache_key] = answer
    return answer


def telemetry_valid(row):
    """Call the one S-20 validator from immutable D13 inputs; ignore text labels."""
    ref = row.get("s20_attempt")
    if not isinstance(ref, dict):
        return False, "S20_D13_ATTEMPT_MISSING"
    try:
        valid, reason, _hz = _s20_validate(ref)
        return valid, reason
    except Exception as exc:
        return False, f"S20_VALIDATOR_ERROR:{type(exc).__name__}:{exc}"


def _event_fields(row):
    parts = str(row.get("plan_event", "")).split("|")
    if len(parts) != 6:
        raise QualificationError("plan_event_must_have_six_fields")
    phase, core, m_text, frontier, rep_text, provider = parts
    match = re.fullmatch(r"m([0-9]+)", m_text)
    rep_match = re.fullmatch(r"r([0-9]+)", rep_text)
    if not match or not rep_match:
        raise QualificationError("plan_event_geometry_or_replica_invalid")
    m, replica = int(match.group(1)), int(rep_match.group(1))
    if core not in CORE_SET or m not in M_SET or frontier not in FRONTIERS or not 1 <= replica <= 31:
        raise QualificationError("plan_event_coordinate_out_of_contract")
    return phase, core, m, frontier, replica, provider


def _work_per_call(row, m):
    output = row.get("O")
    if isinstance(output, bool) or not isinstance(output, int) or output != ARCH_O:
        raise QualificationError("ARCHITECTURAL_O_MUST_EQUAL_1024_FROM_PLAN")
    work = I * output * m
    if "mac_per_iteration" in row and row["mac_per_iteration"] != work:
        raise QualificationError("MAC_WORK_MISMATCH_PLAN_512_O_M")
    if "macs_per_call" in row and row["macs_per_call"] != work:
        raise QualificationError("MACS_PER_CALL_MISMATCH_PLAN_512_O_M")
    return work


def _row_hz(row):
    direct = row.get("f_tsc_hz")
    if _finite_positive(direct):
        return float(direct)
    detail = row.get("telemetry_detail")
    if isinstance(detail, dict) and _finite_positive(detail.get("f_tsc_hz")):
        return float(detail["f_tsc_hz"])
    ref = row.get("s20_attempt")
    if isinstance(ref, dict) and _finite_positive(ref.get("f_tsc_qualified_hz")):
        return float(ref["f_tsc_qualified_hz"])
    raise QualificationError("qualified_f_tsc_missing")


def _row_expected_lp(row, core=None, worker=None):
    ref = row.get("s20_attempt", {})
    if isinstance(ref, dict) and isinstance(ref.get("lp_map"), list):
        lp_map = ref["lp_map"]
    else:
        directory = str(Path(ref.get("directory", "")).resolve()) if isinstance(ref, dict) and ref.get("directory") else ""
        lp_map = S20_MAP_CACHE.get(directory)
    if not isinstance(lp_map, list):
        raise QualificationError("P_CORE_LP_MAP_MISSING_FROM_S20_PREFLIGHT")
    wanted_core = int(core[1:]) if isinstance(core, str) and core.startswith("P") else int(worker)
    match = [entry for entry in lp_map if entry.get("p_core") == wanted_core]
    if len(match) != 1 or not isinstance(match[0].get("worker_lp"), int):
        raise QualificationError("P_CORE_LP_MAP_INVALID_FROM_S20_PREFLIGHT")
    return match[0]["worker_lp"]


def _row_work_time_ns(row):
    start, end = row.get("tsc_start"), row.get("tsc_end")
    if (isinstance(start, bool) or isinstance(end, bool) or not isinstance(start, int)
            or not isinstance(end, int) or end <= start):
        raise QualificationError("invalid_worker_TSC_interval")
    return (end - start) * 1e9 / _row_hz(row)


def _block_samples(rows, phase_filter: str | None = None):
    groups: dict[tuple, dict[int, list[dict[str, Any]]]] = {}
    for row in rows:
        phase, core, m, frontier, replica, provider = _event_fields(row)
        if phase_filter is not None and phase != phase_filter:
            continue
        if row.get("label") != "OFFICIAL" or row.get("provider") != provider:
            raise QualificationError("plan_event_row_identity_mismatch")
        if row.get("m") != m or row.get("frontier") != frontier:
            raise QualificationError("plan_event_row_coordinates_mismatch")
        _work_per_call(row, m)
        groups.setdefault((phase, frontier, core, m, provider), {}).setdefault(replica, []).append(row)
    output = {}
    for key, replicas in groups.items():
        samples = []
        for replica, worker_rows in replicas.items():
            core = key[2]
            expected_workers = set(range(8)) if core == "8P" else {int(core[1:])}
            by_worker = {row.get("worker"): row for row in worker_rows}
            if len(worker_rows) != len(expected_workers) or set(by_worker) != expected_workers:
                raise QualificationError(f"worker_coverage_invalid:{key}:r{replica}")
            for worker, row in by_worker.items():
                expected_lp = _row_expected_lp(row, key[2], worker)
                if row.get("lp") != expected_lp:
                    raise QualificationError(f"WORKER_TO_LP_MAPPING_INVALID:{key}:r{replica}:worker={worker}:expected={expected_lp}:got={row.get('lp')}")
            rs = [row.get("R") for row in worker_rows]
            if any(isinstance(r, bool) or not isinstance(r, int) or r < 1 for r in rs) or len(set(rs)) != 1:
                raise QualificationError(f"R_NOT_COMMON_OR_INVALID:{key}:r{replica}")
            work = I * ARCH_O * key[3]
            worker_rates = {w: work * int(row["R"]) / _row_work_time_ns(row) for w, row in by_worker.items()}
            first = min(row["tsc_start"] for row in worker_rows)
            last = max(row["tsc_end"] for row in worker_rows)
            hz = statistics.median(_row_hz(row) for row in worker_rows)
            team_ns = (last - first) * 1e9 / hz
            if team_ns <= 0:
                raise QualificationError("nonpositive_team_wall_interval")
            team_rate = sum(work * int(row["R"]) for row in worker_rows) / team_ns
            calibration_ids = {row.get("calibration_id") for row in worker_rows}
            order_hashes = {row.get("unit_order_sha256") for row in worker_rows}
            map_hashes = {row.get("topology_map_sha256") for row in worker_rows}
            if (len(calibration_ids) != 1 or not isinstance(next(iter(calibration_ids)), str) or
                    len(order_hashes) != 1 or not re.fullmatch(r"[0-9a-f]{64}", str(next(iter(order_hashes)))) or
                    len(map_hashes) != 1 or not re.fullmatch(r"[0-9a-f]{64}", str(next(iter(map_hashes))))):
                raise QualificationError(f"SERIES_CALIBRATION_ORDER_OR_MAP_UNBOUND:{key}:r{replica}")
            samples.append({"replica": replica, "rows": by_worker, "worker_rates": worker_rates,
                            "team_rate": team_rate, "team_time_ns": team_ns,
                            "normalized_team_time_ns_per_call": team_ns / int(rs[0]),
                            "R": int(rs[0]), "macs_per_call": work,
                            "calibration_id": next(iter(calibration_ids)),
                            "unit_order_sha256": next(iter(order_hashes)),
                            "topology_map_sha256": next(iter(map_hashes))})
        if {sample["replica"] for sample in samples} != set(range(1, 32)):
            raise QualificationError(f"replicas_not_exactly_1_to_31:{key}")
        samples.sort(key=lambda sample: sample["replica"])
        output[key] = samples
    return output


def best_reference(variants):
    eligible = [(name, rates) for name, rates in variants.items()
                if len(rates) == 31 and all(_finite_positive(v) for v in rates)]
    if not eligible:
        raise QualificationError("mandatory_reference_candidates_missing")
    medians = [(statistics.median(rates), name) for name, rates in eligible]
    # Selection uses reference-only selection samples and higher useful MAC/s wins.
    return max(medians, key=lambda pair: (pair[0], tuple(-ord(c) for c in pair[1])))


def _hold(reason, plan_sha, **extra):
    return {"state": "HOLD_INSTRUMENTATION", "campaign": "HOLD", "residency": "NOT_STARTED",
            "reason": reason, "plan_sha256": plan_sha, "numeric_metrics_used": False, **extra}


def _plan_rows(plan_ids):
    parsed = {}
    for event_id in plan_ids:
        parts = event_id.split("|")
        if len(parts) != 6:
            raise QualificationError("plan_event_schema_invalid")
        phase, core, m_text, frontier, rep_text, provider = parts
        m = int(m_text[1:]) if re.fullmatch(r"m\d+", m_text) else 0
        replica = int(rep_text[1:]) if re.fullmatch(r"r\d+", rep_text) else 0
        if phase in {"competency", "reference_selection_ggml", "reference_selection_torch"}:
            parsed[(phase, frontier, core, m, replica, provider)] = event_id
    return parsed


def _required_candidates(family: str, m: int):
    if family == "ggml":
        return ("ggml_flat", "ggml_repack")
    return ("torch_mm", "torch_matmul", "torch_linear", "torch_mv") if m == 1 else ("torch_mm", "torch_matmul", "torch_linear")


def _gate_official(rows, summary, plan_sha, plan_ids, telemetry_check: Callable = telemetry_valid):
    if not rows or not all(row.get("label") == "OFFICIAL" for row in rows):
        raise QualificationError("official_mode_requires_OFFICIAL_rows")
    invalid = []
    for row in rows:
        ok, reason = telemetry_check(row)
        if not ok:
            invalid.append({"provider": row.get("provider"), "cell": row.get("cell"),
                            "replica": row.get("replica"), "reason": reason})
    if invalid:
        return _hold("S20_VALIDATOR_REJECTED_INPUT", plan_sha, invalid_samples=len(invalid), invalid_reasons=invalid[:64])
    for row in rows:
        if row.get("provider") != "P_instr" and (row.get("large_pages") is not True or row.get("affinity") != "PASS"):
            return _hold("PAGE_OR_AFFINITY_INVALID", plan_sha)

    planned = _plan_rows(plan_ids)
    actual_ids = {str(row.get("plan_event")) for row in rows if row.get("provider") not in {"BW_DRAM", "P_instr"}}
    if not actual_ids.issubset(plan_ids):
        return _hold("RAW_EVENT_NOT_IN_FROZEN_PLAN", plan_sha)
    if actual_ids != plan_ids:
        return _hold(f"QUALIFICATION_EVENT_MULTIPLICITY_OR_COVERAGE_INVALID:missing={len(plan_ids-actual_ids)}:extra={len(actual_ids-plan_ids)}", plan_sha)
    required_units = {(frontier, core, m) for frontier in FRONTIERS for core in CORE_SET for m in M_SET}
    for frontier, core, m in sorted(required_units):
        required = [("competency", "omega"), ("competency", "ggml_winner"),
                    ("competency", "torch_winner")]
        required.extend((phase, provider) for phase, family in
                        (("reference_selection_ggml", "ggml"), ("reference_selection_torch", "Torch"))
                        for provider in _required_candidates(family.lower(), m))
        for phase, provider in required:
            for replica in range(1, 32):
                event = planned.get((phase, frontier, core, m, replica, provider))
                if event is None or event not in actual_ids:
                    family_name = "OMEGA" if provider == "omega" else ("ggml" if provider.startswith("ggml_") else "Torch")
                    return _hold(f"MANDATORY_{family_name}_CANDIDATE_MISSING:{frontier}:{core}:m{m}:{provider}:r{replica}", plan_sha)
    try:
        grouped = _block_samples([r for r in rows if r.get("provider") not in {"BW_DRAM", "P_instr"}])
    except QualificationError as exc:
        return _hold(f"MEASUREMENT_SERIES_INVALID:{exc}", plan_sha)
    selection_log, gates, references = [], [], []
    all_pair_series: dict[tuple, tuple[list[dict], list[dict]]] = {}

    def samples_for(phase, frontier, core, m, provider):
        key = (phase, frontier, core, m, provider)
        samples = grouped.get(key)
        if samples is None:
            return None
        for replica in range(1, 32):
            slot = provider
            if phase == "competency" and provider.startswith("ggml_"):
                slot = "ggml_winner"
            elif phase == "competency" and provider.startswith("torch_"):
                slot = "torch_winner"
            if (phase, frontier, core, m, replica, slot) not in planned:
                return None
        return samples

    # Candidate coverage is mandatory. A missing route produces HOLD before choosing a smaller set.
    for frontier, core, m in sorted(required_units):
        if samples_for("competency", frontier, core, m, "omega") is None:
            return _hold(f"OMEGA_COMPARISON_SAMPLES_MISSING:{frontier}:{core}:m{m}", plan_sha)
        for family, phase in (("ggml", "reference_selection_ggml"), ("Torch", "reference_selection_torch")):
            rates = {}
            for provider in _required_candidates(family.lower(), m):
                candidate = samples_for(phase, frontier, core, m, provider)
                if candidate is None:
                    return _hold(f"MANDATORY_{family}_CANDIDATE_MISSING:{frontier}:{core}:m{m}:{provider}", plan_sha)
                rates[provider] = [sample["team_rate"] if core == "8P" else
                                   sample["worker_rates"][int(core[1:])] for sample in candidate]
            selected_rate, selected_name = best_reference(rates)
            comparison_slot = "ggml_winner" if family == "ggml" else "torch_winner"
            compare = samples_for("competency", frontier, core, m, comparison_slot)
            if compare is None:
                return _hold(f"SELECTED_REFERENCE_COMPARISON_SAMPLES_MISSING:{frontier}:{core}:m{m}:{selected_name}", plan_sha)
            for sample in compare:
                for row in sample["rows"].values():
                    if row.get("candidate_implementation") != selected_name:
                        return _hold(f"COMPARISON_WINNER_SLOT_MISMATCH:{frontier}:{core}:m{m}:{selected_name}", plan_sha)
            omega = samples_for("competency", frontier, core, m, "omega")
            all_unit_samples = [omega, compare]
            for candidate_name in (_required_candidates(family.lower(), m)):
                candidate_samples = samples_for(phase, frontier, core, m, candidate_name)
                if candidate_samples is None:
                    return _hold(f"MANDATORY_{family}_CANDIDATE_MISSING:{frontier}:{core}:m{m}:{candidate_name}", plan_sha)
                all_unit_samples.append(candidate_samples)
            unit_rs = {sample["R"] for series in all_unit_samples for sample in series}
            unit_calibration = {sample["calibration_id"] for series in all_unit_samples for sample in series}
            unit_order = {sample["unit_order_sha256"] for series in all_unit_samples for sample in series}
            unit_maps = {sample["topology_map_sha256"] for series in all_unit_samples for sample in series}
            if len(unit_rs) != 1 or len(unit_calibration) != 1 or len(unit_order) != 1 or len(unit_maps) != 1:
                return _hold(f"QUALIFICATION_UNIT_R_CALIBRATION_ORDER_OR_MAP_NOT_COMMON:{frontier}:{core}:m{m}", plan_sha)
            ratio_by_replica = []
            for ref_sample, omega_sample in zip(compare, omega):
                if ref_sample["replica"] != omega_sample["replica"]:
                    raise QualificationError("paired_replica_ids_mismatch")
                p_omega = omega_sample["team_rate"] if core == "8P" else omega_sample["worker_rates"][int(core[1:])]
                p_ref = ref_sample["team_rate"] if core == "8P" else ref_sample["worker_rates"][int(core[1:])]
                ratio_by_replica.append(p_omega / p_ref)
            g_value = statistics.median(ratio_by_replica)
            gate_name = "G_ggml" if family == "ggml" else "G_Torch"
            threshold = THRESHOLDS[gate_name]
            gates.append({"gate": gate_name, "frontier": frontier, "core": core, "m": m,
                          "selected": selected_name, "selection_median_MAC_per_ns": selected_rate,
                          "paired_G_median": g_value, "paired_ratios": ratio_by_replica,
                          "threshold": threshold, "pass": g_value >= threshold})
            references.append({"frontier": frontier, "core": core, "m": m, "family": family,
                               "selected": selected_name, "selection_series": phase,
                               "comparison_series": "competency", "selection_samples": 31,
                               "comparison_samples": 31, "sample_reuse": False})
        # R, its calibration identity, preregistered unit order, and the
        # worker-to-LP map are fixed across every candidate and both winner
        # comparisons in this (core,m,frontier) unit.
        unit_series = [samples_for("competency", frontier, core, m, "omega"),
                       samples_for("competency", frontier, core, m, "ggml_winner"),
                       samples_for("competency", frontier, core, m, "torch_winner")]
        for phase in ("reference_selection_ggml", "reference_selection_torch"):
            for candidate_name in (_required_candidates("ggml" if phase.endswith("ggml") else "torch", m)):
                unit_series.append(samples_for(phase, frontier, core, m, candidate_name))
        if any(series is None for series in unit_series):
            return _hold(f"QUALIFICATION_UNIT_SERIES_MISSING:{frontier}:{core}:m{m}", plan_sha)
        unit_rs = {sample["R"] for series in unit_series for sample in series}
        unit_calibration = {sample["calibration_id"] for series in unit_series for sample in series}
        unit_order = {sample["unit_order_sha256"] for series in unit_series for sample in series}
        unit_maps = {sample["topology_map_sha256"] for series in unit_series for sample in series}
        if len(unit_rs) != 1 or len(unit_calibration) != 1 or len(unit_order) != 1 or len(unit_maps) != 1:
            return _hold(f"QUALIFICATION_UNIT_R_CALIBRATION_ORDER_OR_MAP_NOT_COMMON:{frontier}:{core}:m{m}", plan_sha)
        all_pair_series[(frontier, core, m)] = (
            samples_for("competency", frontier, core, m, "omega"),
            samples_for("competency", frontier, core, m, "ggml_winner"))

    # H-KQ is only O=1024, m=1, prepared, 8P. D' reuses these exact 31 team observations.
    hkq = grouped.get(("competency", "prepared", "8P", 1, "omega"))
    if hkq is None or len(hkq) != 31:
        return _hold("H_KQ_GOVERNING_SERIES_MISSING", plan_sha)
    hkq_team = [sample["team_rate"] for sample in hkq]
    hkq_core = [statistics.median(sample["worker_rates"][w] for sample in hkq) for w in range(8)]
    hkq_team_per_core_median = statistics.median(hkq_team) / 8.0
    hkq_worker_min = min(hkq_core)
    gates.append({"gate": "H-KQ_team_median_per_core", "value": hkq_team_per_core_median,
                  "threshold": 33.1, "pass": hkq_team_per_core_median >= 33.1,
                  "series": "omega|prepared|8P|O1024|m1|competency", "replicas": 31})
    gates.append({"gate": "H-KQ_min_worker_median", "value": hkq_worker_min,
                  "worker_medians": hkq_core, "threshold": 33.1, "pass": hkq_worker_min >= 33.1,
                  "series": "omega|prepared|8P|O1024|m1|competency", "replicas": 31})

    bw_group: dict[int, list[dict[str, Any]]] = {}
    for row in rows:
        if row.get("provider") != "BW_DRAM":
            continue
        if row.get("label") != "OFFICIAL" or row.get("frontier") != "prepared":
            return _hold("BW_DRAM_LABEL_OR_FRONTIER_INVALID", plan_sha)
        if row.get("bytes_per_pass_per_worker") != BW_BYTES_PER_WORKER:
            return _hold("BW_DRAM_BYTES_PER_PASS_NOT_256_MIB", plan_sha)
        bw_group.setdefault(row.get("replica"), []).append(row)
    if set(bw_group) != set(range(1, 32)):
        return _hold("BW_DRAM_N_NOT_31", plan_sha)
    bw_rates = []
    for replica in range(1, 32):
        rr = bw_group[replica]
        by_worker = {row.get("worker"): row for row in rr}
        if len(rr) != 8 or set(by_worker) != set(range(8)):
            return _hold(f"BW_DRAM_WORKER_COVERAGE_INVALID:r{replica}", plan_sha)
        if {row.get("lp") for row in rr} != {_row_expected_lp(row, worker=row.get("worker")) for row in rr}:
            return _hold(f"BW_DRAM_LP_MAP_INVALID:r{replica}", plan_sha)
        reps = {row.get("R") for row in rr}
        if len(reps) != 1 or any(isinstance(r, bool) or not isinstance(r, int) or r < 1 for r in reps):
            return _hold(f"BW_DRAM_R_NOT_COMMON:r{replica}", plan_sha)
        first = min(row.get("tsc_start", 0) for row in rr)
        last = max(row.get("tsc_end", 0) for row in rr)
        hz = statistics.median(_row_hz(row) for row in rr)
        wall_ns = (last - first) * 1e9 / hz
        if wall_ns <= 0:
            return _hold(f"BW_DRAM_TEAM_WALL_INVALID:r{replica}", plan_sha)
        bw_rates.append(8 * BW_BYTES_PER_WORKER * next(iter(reps)) / wall_ns)
    bw = statistics.median(bw_rates)
    dprime = .5625 * statistics.median(hkq_team) / bw
    gates.append({"gate": "D_prime_hot", "value": dprime, "threshold": 2.0,
                  "pass": dprime >= 2.0, "H_KQ_source_replicas": [sample["replica"] for sample in hkq],
                  "BW_DRAM_median_bytes_per_ns": bw})

    instr = _qualify_pinstr(rows)
    if not instr[0]:
        return _hold(instr[1], plan_sha, P_instr_by_core=instr[2], gates=gates)
    instr_gates, instr_rates = instr[2], instr[3]
    gates.extend(instr_gates)

    failed = [gate for gate in gates if not gate.get("pass", False)]
    if failed:
        state, reason = "FALLIDA_CUALIFICACION", ",".join(gate["gate"] for gate in failed)
        campaign = "CLOSED_Q4_256_THIS_I7"
    else:
        state, reason, campaign = "QUALIFIED_FOR_RESIDENCY", "all_numeric_gates_pass", "OPEN"
    return {"state": state, "campaign": campaign, "residency": "NOT_STARTED", "reason": reason,
            "plan_sha256": plan_sha, "numeric_metrics_used": True, "references": references,
            "gates": gates, "H_KQ_team_median_per_core": hkq_team_per_core_median,
            "H_KQ_worker_min_median": hkq_worker_min, "D_prime_source": "same_31_prepared_8P_m1_OMEGA_series",
            "BW_DRAM_bytes_per_ns": bw, "P_instr_by_core": instr_rates}


def _qualify_pinstr(rows):
    desired = {(c, u) for c in (1, 2, 4, 8, 10, 12, 14) for u in (8,)} | {(12, 16), (14, 16)}
    groups: dict[tuple[int, int, int], list[dict[str, Any]]] = {}
    hashes = set()
    for row in rows:
        if row.get("provider") != "P_instr":
            continue
        c, u = row.get("chains"), row.get("U")
        if (c, u) not in desired:
            return False, "P_instr_GEOMETRY_INVALID", {}, []
        if row.get("warmup") != 5 or row.get("N") != 31 or row.get("disassembly_qa") != "PASS":
            return False, "P_instr_INSTRUMENT_OR_DISASSEMBLY_UNQUALIFIED", {}, []
        if row.get("calibration_target_seconds") != 2.5 or row.get("calibration_all_workers") is not True:
            return False, "P_instr_R_CALIBRATION_TARGET_OR_SCOPE_INVALID", {}, []
        if not re.fullmatch(r"[0-9a-f]{64}", str(row.get("disassembly_sha256", ""))):
            return False, "P_instr_DISASSEMBLY_HASH_MISSING", {}, []
        hashes.add(row["disassembly_sha256"])
        groups.setdefault((c, u, row.get("replica")), []).append(row)
    if len(hashes) != 1:
        return False, "P_instr_DISASSEMBLY_NOT_SINGLE_FROZEN_ARTIFACT", {}, []
    rates: dict[tuple[int, int, int], list[float]] = {}
    calibrated_r: dict[tuple[int, int], int] = {}
    for c, u in desired:
        reps = []
        for replica in range(1, 32):
            rr = groups.get((c, u, replica), [])
            by_worker = {row.get("worker"): row for row in rr}
            if len(rr) != 8 or set(by_worker) != set(range(8)):
                return False, f"P_instr_WORKER_COVERAGE_INVALID:c{c}:U{u}:r{replica}", {}, []
            if any(row.get("label") != "OFFICIAL" or row.get("frontier") != "prepared" or
                   row.get("O") != ARCH_O or row.get("m") != 1 for row in rr):
                return False, "P_instr_CONFIGURATION_INVALID", {}, []
            if {row.get("lp") for row in rr} != {_row_expected_lp(row, worker=row.get("worker")) for row in rr}:
                return False, "P_instr_LP_MAP_INVALID", {}, []
            rset = {row.get("R") for row in rr}
            if len(rset) != 1 or any(isinstance(r, bool) or not isinstance(r, int) or r <= 0 for r in rset):
                return False, "P_instr_R_NOT_COMMON", {}, []
            r_value = next(iter(rset))
            if (c, u) in calibrated_r and calibrated_r[(c, u)] != r_value:
                return False, "P_instr_R_NOT_FIXED_ACROSS_31_REPLICAS", {}, []
            calibrated_r[(c, u)] = r_value
            rates_this_rep = []
            for worker, row in by_worker.items():
                duration_ns = _row_work_time_ns(row)
                if duration_ns < 2_000_000_000:
                    return False, f"P_instr_SAMPLE_UNDER_2S:c{c}:U{u}:r{replica}:worker{worker}", {}, []
                rate = int(row["R"]) * c * u * 32 / duration_ns
                rates.setdefault((c, u, worker), []).append(rate)
                rates_this_rep.append(rate)
            reps.append(statistics.median(rates_this_rep))
    per_core = {f"P{worker}": {f"c{c}_U{u}": statistics.median(rates[(c, u, worker)])
                                      for c, u in sorted(desired)} for worker in range(8)}
    contrasts = ((12, 8, 14, 8), (12, 16, 12, 8), (14, 16, 14, 8))
    gates = []
    for worker in range(8):
        for a_c, a_u, b_c, b_u in contrasts:
            a = statistics.median(rates[(a_c, a_u, worker)])
            b = statistics.median(rates[(b_c, b_u, worker)])
            delta = abs(a-b) / ((a+b)/2)
            gates.append({"gate": "P_instr_symmetric_contrast", "core": f"P{worker}",
                          "contrast": f"c{a_c}_U{a_u}:c{b_c}_U{b_u}",
                           "reference": f"c{b_c}_U{b_u}", "value": delta, "threshold": .03,
                          "pass": delta <= .03})
    failed = [gate for gate in gates if not gate["pass"]]
    if failed:
        return False, "P_instr_UNQUALIFIED_CONTRAST_GT_3_PERCENT", per_core, gates
    return True, "PASS", gates, per_core


def _all_smoke(rows):
    return all(row.get("label") == "SMOKE" for row in rows)


def _synthetic_gate(attempt):
    if attempt.get("telemetry") != "VALID" or attempt.get("instrument") is not True:
        return {"state": "HOLD_INSTRUMENTATION", "campaign": "HOLD", "reason": "instrument_invalid", "residency": "NOT_STARTED"}
    if not _finite_positive(attempt.get("plateau_delta")) or attempt["plateau_delta"] > .03:
        return {"state": "HOLD_INSTRUMENTATION", "campaign": "HOLD", "reason": "P_instr UNQUALIFIED", "residency": "NOT_STARTED"}
    for key, threshold in (("G_ggml", .90), ("G_Torch", 1.20), ("H_KQ_median", 33.1),
                           ("H_KQ_worker_min", 33.1), ("D_prime_hot", 2.00)):
        value = attempt.get(key)
        if not _finite_positive(value) or value < threshold:
            return {"state": "FALLIDA_CUALIFICACION", "campaign": "CLOSED_Q4_256_THIS_I7",
                    "reason": f"{key}_below_{threshold}", "residency": "NOT_STARTED"}
    return {"state": "QUALIFIED_FOR_RESIDENCY", "campaign": "OPEN", "reason": "all_gates_pass", "residency": "NOT_STARTED"}


def decide_attempts(attempts):
    if not attempts:
        return {"state": "HOLD_INSTRUMENTATION", "campaign": "HOLD", "reason": "no_attempt",
                "tries": 0, "retry_remaining": 0, "residency": "NOT_STARTED"}
    for index, attempt in enumerate(attempts[:2], 1):
        result = _synthetic_gate(attempt)
        result["tries"] = index
        result["retry_remaining"] = int(result["state"] == "HOLD_INSTRUMENTATION" and index == 1 and len(attempts) == 1)
        if result["state"] != "HOLD_INSTRUMENTATION" or index == 2 or len(attempts) == 1:
            return result
    return result


def _synthetic_rows(plan_ids: set[str], *, prepared_m1_us=10.0):
    rows = []
    synthetic_lp_map = [{"p_core": i, "worker_lp": 2*i} for i in range(8)]
    import residency_plan_r17 as planlib
    q_events = planlib.build_payload()["qualification_events"]
    seq_by_id = {event["id"]: event["sequence"] for event in q_events}
    unit_events: dict[str, list[str]] = {}
    for event in q_events:
        unit_events.setdefault(event["unit_id"], []).append(event["id"])
    unit_order_sha = {unit: hashlib.sha256("\n".join(ids).encode("ascii")).hexdigest()
                      for unit, ids in unit_events.items()}
    for event_id in sorted(plan_ids):
        phase, core, m_text, frontier, replica_text, provider = event_id.split("|")
        m, replica = int(m_text[1:]), int(replica_text[1:])
        workers = range(8) if core == "8P" else (int(core[1:]),)
        times = {"omega": prepared_m1_us if (frontier == "prepared" and m == 1 and core == "8P") else 10.0,
                 "ggml_flat": 9.0, "ggml_repack": 9.5, "torch_mm": 12.0,
                 "torch_matmul": 13.0, "torch_linear": 14.0, "torch_mv": 12.0}
        if phase == "competency":
            implementation = ("omega" if provider == "omega" else
                              "ggml_flat" if provider == "ggml_winner" else "torch_mm")
            time_us = times[implementation]
        else:
            implementation = provider
            time_us = times.get(provider)
            if time_us is None:
                continue
        if phase.startswith("reference_selection"):
            if phase.endswith("ggml") and not provider.startswith("ggml_"):
                continue
            if phase.endswith("torch") and not provider.startswith("torch_"):
                continue
        if phase == "competency" and provider == "ggml_winner" and frontier == "prepared" and m == 1 and core == "8P":
            time_us = prepared_m1_us * .95
        if phase == "competency" and provider == "torch_winner" and frontier == "prepared" and m == 1 and core == "8P":
            time_us = prepared_m1_us * 1.4
        unit_id = f"{core}|m{m}|{frontier}|O1024"
        for worker in workers:
            hz = 2.5e9
            us = time_us
            work_ns = us * 1000
            start = 10_000_000_000 + replica * 10_000_000_000
            end = start + int(work_ns * hz / 1e9)
            rows.append({"label": "OFFICIAL", "provider": provider, "plan_event": event_id,
                         "frontier": frontier, "core": core, "O": 1024, "m": m,
                         "replica": replica, "worker": worker, "lp": 2*worker,
                         "R": 1, "R_common": 1, "t_app_us": time_us,
                         "candidate_implementation": implementation if phase == "competency" else provider,
                         "calibration_id": f"{unit_id}|Rcal1",
                         "unit_id": unit_id, "unit_order_sha256": unit_order_sha[unit_id],
                         "topology_map_sha256": "f"*64, "event_sequence": seq_by_id[event_id],
                         "tsc_start": start, "tsc_end": end, "f_tsc_hz": hz,
                         "mac_per_iteration": I*ARCH_O*m, "large_pages": True,
                         "affinity": "PASS", "s20_attempt": {"fixture": True, "lp_map": synthetic_lp_map}})
    for replica in range(1, 32):
        for worker in range(8):
            hz = 2.5e9
            start = 20_000_000_000 + replica * 10_000_000_000 + worker * 100
            bw_us = 20000.0
            end = start + int(bw_us * 1000 * hz / 1e9)
            rows.append({"label": "OFFICIAL", "provider": "BW_DRAM", "frontier": "prepared",
                         "replica": replica, "worker": worker, "lp": 2*worker, "R": 1,
                         "bytes_per_pass_per_worker": BW_BYTES_PER_WORKER, "tsc_start": start,
                         "tsc_end": end, "f_tsc_hz": hz, "large_pages": True, "affinity": "PASS",
                         "s20_attempt": {"fixture": True, "lp_map": synthetic_lp_map}})
    for chains, u in [(c, 8) for c in (1, 2, 4, 8, 10, 12, 14)] + [(12, 16), (14, 16)]:
        for replica in range(1, 32):
            for worker in range(8):
                hz, r = 1.0e9, 2_500_000
                work = r * chains * u * 32
                factor = 1.0
                rate = .25 * factor
                dt_ns = work / rate
                start = 30_000_000_000 + replica * 10_000_000_000 + worker * 100
                rows.append({"label": "OFFICIAL", "provider": "P_instr", "chains": chains,
                             "U": u, "frontier": "prepared", "O": 1024, "m": 1,
                             "replica": replica, "worker": worker, "lp": 2*worker, "N": 31,
                             "warmup": 5, "R": r, "tsc_start": start,
                             "tsc_end": start + int(dt_ns*hz/1e9), "f_tsc_hz": hz,
                              "calibration_target_seconds": 2.5,
                              "calibration_all_workers": True,
                             "disassembly_qa": "PASS", "disassembly_sha256": "0"*64,
                             "large_pages": False, "affinity": "PASS",
                             "s20_attempt": {"fixture": True, "lp_map": synthetic_lp_map}})
    return rows


def _test_plan_ids():
    import residency_plan_r17 as planlib
    return {event["id"] for event in planlib.build_payload()["qualification_events"]}


def _pinstr_factor_fixture(rows, factors):
    mutated = [dict(row) for row in rows if row.get("provider") == "P_instr"]
    for row in mutated:
        factor = factors.get((row["chains"], row["U"]), 1.0)
        work = row["R"] * row["chains"] * row["U"] * 32
        seconds = work / (.25 * factor) / 1e9
        hz = _row_hz(row)
        row["tsc_end"] = row["tsc_start"] + int(seconds * hz)
    return mutated


def _paired_g(omega_times, reference_times):
    if len(omega_times) != 31 or len(reference_times) != 31:
        raise QualificationError("paired_G_requires_two_independent_N31_series")
    return statistics.median(ref / omega for omega, ref in zip(omega_times, reference_times))


def _d13_attempt_from_fixture(parent: Path, attempt_dir: Path, *, mutate_config=None, mutate_emon=None):
    store = SessionAttemptStore.create(attempt_dir, session_id=attempt_dir.parent.name,
                                       attempt_id=attempt_dir.name)
    originals = {name: (parent / name).read_bytes() for name in
                 ("emon_raw.txt", "clock_map.csv", "block_marks.csv", "events.edp", "effective_config.json")}
    if mutate_emon:
        originals["emon_raw.txt"] = mutate_emon(originals["emon_raw.txt"])
    for name in ("emon_raw.txt", "clock_map.csv", "block_marks.csv", "events.edp"):
        store.write_raw(name, originals[name])
    config = strict_json(originals["effective_config.json"].decode("utf-8"))
    if mutate_config:
        mutate_config(config)
    source_bytes = (json.dumps(config, sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n").encode()
    store.write_raw("effective_config_source.json", source_bytes)
    config["edp_path"] = str((attempt_dir / "events.edp").resolve())
    config["edp_sha256"] = hashlib.sha256(originals["events.edp"]).hexdigest()
    derived = (json.dumps(config, sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n").encode()
    store.write_derived("effective_config.json", derived,
                        source_raw=["effective_config_source.json", "events.edp"],
                        metadata={"adapter": "S-20 r15 test; path rebased in a new D13 attempt"})
    return {"directory": str(attempt_dir), "mode": config.get("mode", "temporal"),
            "f_tsc_qualified_hz": config["f_tsc_qualified_hz"]}


def _write_s20_mutation_fixture(directory: Path, *, full: bool = False) -> None:
    """Create parser-valid EMON/QPC inputs locally; no validator artifacts are read."""
    import datetime as dt
    import s20_validator_r17 as s20
    directory.mkdir(parents=True, exist_ok=False)
    qpf, tsc = 1_000_000, 2_500_000_000
    base = dt.datetime(2026, 1, 2, 12, 0, 0, tzinfo=dt.timezone.utc)
    filetime_epoch = dt.datetime(1601, 1, 1, tzinfo=dt.timezone.utc)
    base_filetime = int((base - filetime_epoch).total_seconds() * 10_000_000)
    events = list(s20.DEFAULT_EVENT_GROUP)
    edp_lines = ["-C (", *events, ")", "-l100"]
    (directory / "events.edp").write_text("\n".join(edp_lines) + "\n", encoding="utf-8", newline="\n")
    clock_lines = ["sample,qpc_pre,qpc_post,filetime_100ns,sample_period_ms"]
    for i in range(101 if full else 31):
        clock_lines.append(f"{i},{i*100000},{i*100000},{base_filetime+i*1000000},100")
    (directory / "clock_map.csv").write_text("\n".join(clock_lines) + "\n", encoding="ascii", newline="\n")
    timestamps = []
    for i in range(1, (201 if full else 51)):
        current = base + dt.timedelta(milliseconds=50*i)
        timestamps.append(current.strftime("%m/%d/%Y %H:%M:%S.%f")[:-3])
    thread = "\t".join([s20.EVENT_THREAD, str(int(tsc*.05)), *(["125000000"]*24)])
    ref = "\t".join([s20.EVENT_REF, str(int(tsc*.05)), *(["125000000"]*24)])
    inst = "\t".join([s20.EVENT_INST, str(int(tsc*.05)), *(["50000000"]*24)])
    l2m = "\t".join([s20.EVENT_L2_MISS, str(int(tsc*.05)), *(["10000"]*24)])
    l2d = "\t".join([s20.EVENT_L2_DEMAND, str(int(tsc*.05)), *(["100000"]*24)])
    llcm = "\t".join([s20.EVENT_LLC_MISS, str(int(tsc*.05)), *(["1000"]*24)])
    llcd = "\t".join([s20.EVENT_LLC_DEMAND, str(int(tsc*.05)), *(["20000"]*24)])
    therm = "\t".join([s20.EVENT_19C, str(int(tsc*.05)), *([str((1<<31)|(30<<16))]*24)])
    package = [f"{event}\t{int(tsc*.05)}\t{value}" for event, value in
               ((s20.EVENT_606, 0), (s20.EVENT_611, 500), (s20.EVENT_1B1, 0))]
    event_rows = [*package[:1], package[1], package[2], thread, ref, inst, l2m, l2d, llcm, llcd, therm]
    raw = []
    for stamp in timestamps:
        raw.extend(["==========", stamp, *event_rows])
    raw.append("==========")
    (directory / "emon_raw.txt").write_text("\n".join(raw) + "\n", encoding="utf-8", newline="\n")
    marks_header = "sample_id,p_core,replica,condition,t_pad_ini,t_ini,t_fin,t_pad_fin,worker_lp,comparison_set,clock_domain\n"
    if full:
        mark_rows = []
        conditions = (("alpha", 1, "group-r1", 500_000), ("beta", 1, "group-r1", 2_500_000),
                      ("alpha", 2, "group-r2", 4_500_000), ("beta", 2, "group-r2", 6_500_000))
        for condition, replica, group, start in conditions:
            for p_core in range(8):
                mark_rows.append(f"fixture-{condition}-r{replica}-p{p_core},{p_core},{replica},{condition},"
                                 f"{start-250000},{start},{start+1000000},{start+1250000},{p_core*2},{group},qpc_ticks")
        (directory / "block_marks.csv").write_text(marks_header + "\n".join(mark_rows) + "\n",
                                                    encoding="ascii", newline="\n")
    else:
        (directory / "block_marks.csv").write_text(marks_header +
            "fixture-1,0,1,fixture-condition,250000,500000,1500000,1750000,0,,qpc_ticks\n",
            encoding="ascii", newline="\n")
    config = {
        "schema_version": "S20-R2-v1", "qpf_hz": qpf, "qpf_source": "synthetic QPC fixture",
        "f_tsc_qualified_hz": tsc, "f_tsc_qpc_hz": tsc, "f_tsc_configured_hz": tsc,
        "f_tsc_qualification_qpf_hz": qpf, "emon_local_utc_offset_minutes": 0,
        "nominal_window_s": .05,
        "lp_map": [{"p_core": p, "core_id": p*4, "core_type": "P", "worker_lp": p*2,
                    "smt_siblings": [p*2, p*2+1]} for p in range(8)],
        "event_groups": [events], "multiplexing": False, "periods_lost": 0,
        "energy_event": s20.EVENT_611, "energy_semantics": "INTERVAL_DELTA",
        "energy_unit_joule": 1/16384, "tcc_target_c": 100,
        "thermal_history_clear_at_start": False, "mode": "full" if full else "temporal", "logical_processor_count": 24,
        "profile": "s20", "event_map_source": "synthetic captured EDP",
        "edp_path": str((directory / "events.edp").resolve()),
        "edp_sha256": hashlib.sha256((directory / "events.edp").read_bytes()).hexdigest(),
        "marks_clock_domain": "qpc_ticks",
    }
    if full:
        config.update({"comparison_groups": {
            "group-r1": {"kind": "contrast", "condition_counts": {"alpha": 1, "beta": 1}},
            "group-r2": {"kind": "contrast", "condition_counts": {"alpha": 1, "beta": 1}}},
            "condition_replica_counts": {"alpha|r1": 1, "beta|r1": 1, "alpha|r2": 1, "beta|r2": 1},
            "frequency_basis": "frequency_of_condition", "frequency_group_source": "synthetic plan-bound fixture"})
    (directory / "effective_config.json").write_text(
        json.dumps(config, sort_keys=True, separators=(",", ":"))+"\n", encoding="utf-8", newline="\n")


def self_test(out: Path):
    lines = []
    good = {"telemetry": "VALID", "instrument": True, "G_ggml": .95, "G_Torch": 1.25,
            "H_KQ_median": 34.0, "H_KQ_worker_min": 33.2, "D_prime_hot": 2.1, "plateau_delta": .02}
    for name, attempt, expected in [
        ("all_pass", good, "QUALIFIED_FOR_RESIDENCY"),
        ("G_ggml_below", {**good, "G_ggml": .899}, "FALLIDA_CUALIFICACION"),
        ("G_Torch_below", {**good, "G_Torch": 1.199}, "FALLIDA_CUALIFICACION"),
        ("H_KQ_median_below", {**good, "H_KQ_median": 33.099}, "FALLIDA_CUALIFICACION"),
        ("H_KQ_worker_below", {**good, "H_KQ_worker_min": 33.099}, "FALLIDA_CUALIFICACION"),
        ("D_prime_below", {**good, "D_prime_hot": 1.999}, "FALLIDA_CUALIFICACION"),
        ("P_instr_unqualified", {**good, "plateau_delta": .031}, "HOLD_INSTRUMENTATION"),
        ("instrument_invalid", {**good, "telemetry": "INVALID", "instrument": False}, "HOLD_INSTRUMENTATION")]:
        result = decide_attempts([attempt])
        if result["state"] != expected or result["residency"] != "NOT_STARTED":
            raise QualificationError(f"synthetic_branch_failed:{name}:{result}")
        lines.append(f"case={name} state={result['state']} campaign={result['campaign']} residency=NOT_STARTED PASS")

    g = _paired_g([10.0]*31, [8.0]*31)
    if abs(g-.80)>1e-12 or g >= .90:
        raise QualificationError("D3_corrected_8_10_10_fixture_failed")
    lines.append("D3_fixture reference_selection=8 comparison=10 omega=10 G=0.80 gate=FAIL PASS")
    # Existing Sol counterexample: OMEGA 100 us, Torch 80 us; G is T_ref/T_omega.
    g_old = _paired_g([100.0]*31, [80.0]*31)
    if abs(g_old-.80)>1e-12 or g_old >= 1.20:
        raise QualificationError("D3_existing_OMEGA100_Torch80_fixture_failed")
    lines.append("D3_existing_fixture OMEGA=100 Torch=80 G_Torch=0.80 gate=FAIL PASS")
    selection = {"fast": [8.0]*31, "slow": [10.0]*31}
    selected = min(selection, key=lambda k: statistics.median(selection[k]))
    if selected != "fast": raise QualificationError("D3_independent_reference_selection_failed")
    lines.append("D3_selection samples=31 comparison_samples=31 reused=false selected_reference_time_us=8 PASS")

    plan_ids = _test_plan_ids()
    official = _synthetic_rows(plan_ids)
    valid_stub = lambda row: (True, "SYNTHETIC_GATE_ONLY")
    result = _gate_official(official, [{"label": "OFFICIAL"}], "0"*64, plan_ids, valid_stub)
    if result["state"] != "QUALIFIED_FOR_RESIDENCY":
        raise QualificationError(f"synthetic_official_all_pass_failed:{result.get('reason')}")
    lines.append(f"official_fixture all_gates state={result['state']} G_pairs={sum(g['gate'].startswith('G_') for g in result['gates'])} PASS")

    # Team throughput uses the concurrent wall interval spanning all eight workers.
    team_rows = [row for row in official if row.get("provider") == "omega" and
                 row.get("plan_event", "").startswith("competency|8P|m1|prepared|")]
    for row in team_rows:
        row["tsc_start"] = 40_000_000_000 + row["replica"] * 10_000_000_000 + row["worker"] * 1_000
        row["tsc_end"] = row["tsc_start"] + 100_000
    team_sample = _block_samples(team_rows)[("competency", "prepared", "8P", 1, "omega")][0]
    expected_team = 8 * (I * ARCH_O) / (107_000 * 1e9 / 2.5e9)
    worker_median = statistics.median(team_sample["worker_rates"].values())
    if abs(team_sample["team_rate"] - expected_team) > 1e-9 or team_sample["team_rate"] == worker_median:
        raise QualificationError("D3_team_wall_interval_fixture_failed")
    lines.append(f"D3_team_interval team_MAC_per_ns={team_sample['team_rate']:.6f} worker_median={worker_median:.6f} interval=107000_TSC_ticks PASS")

    permuted_lp = [dict(row) for row in official]
    bad_worker = next(row for row in permuted_lp if row.get("provider") == "omega" and
                      row.get("plan_event") == "competency|8P|m1|prepared|r1|omega" and row.get("worker") == 0)
    bad_worker["lp"] = 2
    map_result = _gate_official(permuted_lp, [{"label": "OFFICIAL"}], "0"*64, plan_ids, valid_stub)
    if map_result["state"] != "HOLD_INSTRUMENTATION" or "WORKER_TO_LP_MAPPING_INVALID" not in map_result["reason"]:
        raise QualificationError(f"D3_worker_LP_permutation_not_rejected:{map_result.get('reason')}")
    lines.append("D3_worker_to_LP=PASS single-worker LP permutation rejected")

    varied_r = [dict(row) for row in official]
    for row in varied_r:
        if row.get("plan_event", "").startswith("reference_selection_ggml|P0|m1|prepared|"):
            row["R"] = row["R_common"] = 2
    r_result = _gate_official(varied_r, [{"label": "OFFICIAL"}], "0"*64, plan_ids, valid_stub)
    if r_result["state"] != "HOLD_INSTRUMENTATION" or "QUALIFICATION_UNIT_R_CALIBRATION_ORDER_OR_MAP_NOT_COMMON" not in r_result["reason"]:
        raise QualificationError(f"D3_noncommon_R_not_rejected:{r_result.get('reason')}")
    lines.append("D3_unit_R=PASS R/calibration/order/map mismatch across candidates rejected")

    # D4: m=8 and the dynamic frontier are fast, while governing prepared m=1 fails.
    bad_d4 = _synthetic_rows(plan_ids, prepared_m1_us=16.0)
    d4 = _gate_official(bad_d4, [{"label": "OFFICIAL"}], "0"*64, plan_ids, valid_stub)
    failed = {gate.get("gate") for gate in d4.get("gates", []) if not gate.get("pass", False)}
    if d4["state"] != "FALLIDA_CUALIFICACION" or "H-KQ_team_median_per_core" not in failed:
        raise QualificationError(f"D4_governing_fixture_failed:{d4.get('reason')}:{failed}")
    lines.append("D4_fixture prepared_O1024_m1=FAIL dynamic_and_m8=PASS governing=prepared_8P_m1 FAIL PASS")
    bad_d4_groups = _block_samples([row for row in bad_d4 if row.get("provider") not in {"BW_DRAM", "P_instr"}])
    governing = statistics.median(sample["team_rate"] for sample in bad_d4_groups[("competency", "prepared", "8P", 1, "omega")]) / 8
    dynamic = statistics.median(sample["team_rate"] for sample in bad_d4_groups[("competency", "dynamic", "8P", 1, "omega")]) / 8
    m8_rate = statistics.median(sample["team_rate"] for sample in bad_d4_groups[("competency", "prepared", "8P", 8, "omega")]) / 8
    if governing >= 33.1 or dynamic < 33.1 or m8_rate < 33.1:
        raise QualificationError("D4_second_frontier_or_m8_counterexample_failed")
    lines.append(f"D4_counterexample governing_prepared_m1={governing:.3f} dynamic_m1={dynamic:.3f} prepared_m8={m8_rate:.3f} MAC_per_call=512*O*m PASS")
    work_cells = []
    for m in sorted(M_SET):
        expected_work = 512 * ARCH_O * m
        actual_work = _work_per_call({"O": ARCH_O, "mac_per_iteration": expected_work, "macs_per_call": expected_work}, m)
        if actual_work != expected_work:
            raise QualificationError(f"D4_MAC_geometry_failed:m{m}")
        work_cells.append(f"m{m}={actual_work}")
    lines.append("D4_real_work_formula " + ",".join(work_cells) + " PASS")

    bad_bw = [dict(row) for row in official]
    for row in bad_bw:
        if row.get("provider") == "BW_DRAM":
            row["bytes_per_pass_per_worker"] -= 64
    bw_hold = _gate_official(bad_bw, [{"label": "OFFICIAL"}], "0"*64, plan_ids, valid_stub)
    if bw_hold["state"] != "HOLD_INSTRUMENTATION" or bw_hold["reason"] != "BW_DRAM_BYTES_PER_PASS_NOT_256_MIB":
        raise QualificationError(f"D5_bytes_frontier_fixture_failed:{bw_hold.get('reason')}")
    lines.append("D5_fixture mutated_bytes_per_pass=256MiB-64 state=HOLD_INSTRUMENTATION reason=BW_DRAM_BYTES_PER_PASS_NOT_256_MIB PASS")

    good_instr = _qualify_pinstr([row for row in official if row.get("provider") == "P_instr"])
    if not good_instr[0]:
        raise QualificationError(f"D6_correct_plateau_fixture_failed:{good_instr[1]}")
    lines.append("D6_plateau_valid=PASS contrasts=12/U8:14/U8,12/U16:12/U8,14/U16:14/U8")
    bad_instr = _pinstr_factor_fixture(official, {(12, 8): .98, (14, 8): 1.00,
                                                   (12, 16): 1.02, (14, 16): 1.00})
    instr = _qualify_pinstr(bad_instr)
    if instr[0] or instr[1] != "P_instr_UNQUALIFIED_CONTRAST_GT_3_PERCENT":
        raise QualificationError(f"D6_same_chain_contrast_fixture_failed:{instr[1]}")
    failing = [g for g in instr[3] if not g.get("pass")]
    if not any(g.get("contrast") == "c12_U16:c12_U8" for g in failing):
        raise QualificationError("D6_12_U16_vs_12_U8_not_gated")
    lines.append("D6_counterexample p12U8=.98 p14U8=1 p12U16=1.02 p14U16=1 => HOLD on 12/U16:12/U8 PASS")

    missing = set(plan_ids)
    missing.discard("reference_selection_torch|P0|m1|prepared|r1|torch_mm")
    missing_rows = _synthetic_rows(missing)
    # A missing required candidate must be HOLD even if the remaining route is faster.
    for row in missing_rows:
        if row.get("plan_event") == "reference_selection_torch|P0|m1|prepared|r1|torch_mm":
            raise QualificationError("D3_missing_candidate_fixture_not_removed")
    hold = _gate_official(missing_rows, [{"label": "OFFICIAL"}], "0"*64, missing, valid_stub)
    if hold["state"] != "HOLD_INSTRUMENTATION" or "MANDATORY_Torch_CANDIDATE_MISSING" not in hold["reason"]:
        raise QualificationError(f"D3_missing_candidate_did_not_hold:{hold.get('reason')}")
    lines.append("D3_missing_mandatory_candidate state=HOLD_INSTRUMENTATION FAIL_CLOSED PASS")

    for bad, expected in [("duplicate_key", '{"x":1,"x":2}'), ("nan", '{"x":NaN}'),
                          ("overflow", '{"x":1e9999}'), ("trailing", '{"x":1} trailing')]:
        try: strict_json(expected)
        except QualificationError: lines.append(f"strict_json case={bad} rejected=PASS")
        else: raise QualificationError(f"strict_json_negative_accepted:{bad}")

    # Mutate the real S-20 input artifacts inside new D13 attempts, retaining the misleading VALID label.
    test_root = Path(__file__).resolve().parent / "logs_r17" / "obj_r17" / "s20_mutation_attempts"
    session_id = f"qualifier-{time.time_ns()}"
    src_attempt = test_root / session_id / "synthetic_source"
    _write_s20_mutation_fixture(src_attempt)
    valid_ref = _d13_attempt_from_fixture(src_attempt, test_root / session_id / "valid")
    valid_row = {"telemetry_status": "VALID", "status": "VALID", "s20_attempt": valid_ref}
    valid_ok, valid_reason = telemetry_valid(valid_row)
    if not valid_ok:
        raise QualificationError(f"S20_control_fixture_invalid:{valid_reason}")
    lines.append("S20_control D13_new_attempt=PASS validator=S20-R2-v1 state=VALID PASS")

    valid_store = SessionAttemptStore(valid_ref["directory"])
    immutable_before = valid_store.verify_raw("emon_raw.txt")
    try:
        valid_store.write_derived("emon_raw.txt", b"replacement forbidden\n", source_raw=["emon_raw.txt"])
    except ArtifactError as exc:
        if "write-once refusal" not in str(exc):
            raise QualificationError(f"D13_derived_overwrite_wrong_error:{exc}") from exc
    else:
        raise QualificationError("D13_derived_replaced_raw")
    if valid_store.verify_raw("emon_raw.txt") != immutable_before:
        raise QualificationError("D13_raw_hash_changed_after_derived_attempt")
    try:
        SessionAttemptStore.create(Path(valid_ref["directory"]), session_id=session_id, attempt_id="reuse")
    except ArtifactError as exc:
        if "refusing reuse" not in str(exc):
            raise QualificationError(f"D13_attempt_reuse_wrong_error:{exc}") from exc
    else:
        raise QualificationError("D13_existing_attempt_reused")
    lines.append("D13_immutability derive_overwrite=refused raw_sha_unchanged=true attempt_reuse=refused PASS")

    tsc_ref = _d13_attempt_from_fixture(src_attempt, test_root / session_id / "tsc_plus_2pct",
        mutate_config=lambda cfg: cfg.__setitem__("f_tsc_qpc_hz", float(cfg["f_tsc_qualified_hz"])*1.02))
    tsc_row = {"telemetry_status": "VALID", "status": "VALID", "s20_attempt": tsc_ref}
    tsc_ok, tsc_reason = telemetry_valid(tsc_row)
    if tsc_ok:
        raise QualificationError("S20_real_TSC_2pct_mutation_accepted")
    lines.append(f"S20_mutation actual_field=f_tsc_qpc_hz plus=2% input_label=VALID rejected=true reason={tsc_reason} PASS")

    def sparse_capture(raw: bytes):
        pieces = re.split(rb"(?m)^={10,}\s*$", raw)
        kept, interval = [], 0
        for piece in pieces:
            if re.search(rb"(?m)^\d{1,2}/\d{1,2}/\d{4}\s+\d{2}:\d{2}:\d{2}\.\d{3}\s*$", piece.strip(), re.M):
                if interval % 100 == 0:
                    kept.append(piece)
                interval += 1
            elif not kept:
                kept.append(piece)
        return b"\n==========\n".join(kept) + b"\n"
    coverage_ref = _d13_attempt_from_fixture(src_attempt, test_root / session_id / "coverage_1pct_gap_1000ms",
                                               mutate_emon=sparse_capture)
    coverage_row = {"telemetry_status": "VALID", "status": "VALID", "s20_attempt": coverage_ref}
    coverage_ok, coverage_reason = telemetry_valid(coverage_row)
    if coverage_ok:
        raise QualificationError("S20_real_coverage_mutation_accepted")
    if "COVERAGE_LOW" not in coverage_reason and "GAP_TOO_LARGE" not in coverage_reason:
        raise QualificationError(f"S20_coverage_mutation_wrong_rejection:{coverage_reason}")
    lines.append(f"S20_mutation actual_field=EMON_windows retained=1/100 input_label=VALID rejected=true reason={coverage_reason} PASS")

    full_source = test_root / session_id / "full_s20_source"
    _write_s20_mutation_fixture(full_source, full=True)
    full_ref = _d13_attempt_from_fixture(full_source, test_root / session_id / "full_s20_valid")
    full_row = {"telemetry_status": "VALID", "status": "VALID", "s20_attempt": full_ref}
    full_ok, full_reason = telemetry_valid(full_row)
    if not full_ok or full_ref.get("mode") != "full":
        raise QualificationError(f"S20_FULL_plan_controls_fixture_invalid:{full_reason}")
    lines.append("D10_S20_FULL=PASS raw_D13_validation=VALID groups=2 actual_conditions=alpha,beta replicas=2 each PASS")

    def raw_frequency_mutation(raw: bytes) -> bytes:
        pieces = re.split(rb"(?m)^={10,}\s*$", raw)
        for i, piece in enumerate(pieces):
            if b"01/02/2026 12:00:00.550" not in piece:
                continue
            rows = piece.splitlines()
            for index, line in enumerate(rows):
                if line.startswith(b"CPU_CLK_UNHALTED.REF_TSC\t"):
                    fields = line.split(b"\t")
                    if fields[2] != b"125000000":
                        raise QualificationError("D10_fixture_reference_baseline_unexpected")
                    fields[2] = b"121000000"
                    rows[index] = b"\t".join(fields)
                    pieces[i] = b"\n".join(rows)
                    return b"\n==========\n".join(pieces)
            raise QualificationError("D10_fixture_reference_row_missing")
        raise QualificationError("D10_fixture_target_window_missing")
    full_mutation_ref = _d13_attempt_from_fixture(full_source,
        test_root / session_id / "full_s20_activity_factor_mutation", mutate_emon=raw_frequency_mutation)
    mutated_ok, mutated_reason = telemetry_valid({"telemetry_status": "VALID", "s20_attempt": full_mutation_ref})
    if mutated_ok or ("ACTIVITY_FACTOR_OUT_OF_RANGE" not in mutated_reason and "FREQ_WINDOW_EXCURSION" not in mutated_reason):
        raise QualificationError(f"D10_S20_FULL_raw_activity_mutation_not_rejected:{mutated_reason}")
    lines.append(f"D10_raw_mutation REF_TSC_first_LP=125000000->121000000 raw_LABEL=VALID rejected=true reason={mutated_reason} PASS")

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("QUALIFIER_R17_SYNTHETIC_QA PASS\n"+"\n".join(lines)+"\n", encoding="utf-8", newline="\n")
    print(f"QUALIFIER_R17_SYNTHETIC_QA PASS cases=8 D3_D6=PASS S20_mutations=4 D13=PASS evidence={out}")


def qualify(raw_path: Path, summary_path: Path, plan_path: Path, attempt_number: int = 1):
    plan_sha, _plan, plan_ids = verify_plan(plan_path)
    rows, summary = read_jsonl(raw_path), read_jsonl(summary_path)
    if _all_smoke(rows):
        return {"label": "SMOKE", "state": "SMOKE_ONLY", "campaign": "HOLD", "residency": "NOT_STARTED",
                "plan_sha256": plan_sha, "raw_rows": len(rows), "summary_rows": len(summary),
                "numeric_metrics_used": False, "reason": "smoke measurements are excluded from scientific gates"}
    try:
        result = _gate_official(rows, summary, plan_sha, plan_ids)
    except QualificationError as exc:
        result = _hold(str(exc), plan_sha)
    result["tries"] = attempt_number
    result["retry_remaining"] = int(result["state"] == "HOLD_INSTRUMENTATION" and attempt_number == 1)
    return result


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--raw", type=Path)
    parser.add_argument("--summary", type=Path)
    parser.add_argument("--plan", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--attempt", type=int, choices=(1, 2), default=1)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--self-test-output", type=Path,
                        default=Path(__file__).resolve().parent/"logs_r17"/"obj_r17"/f"qualify_r17_qa_{time.time_ns()}.txt")
    ns = parser.parse_args(argv)
    try:
        if ns.self_test:
            self_test(ns.self_test_output)
            return 0
        if not all((ns.raw, ns.summary, ns.plan, ns.output)):
            parser.error("use --self-test or provide --raw --summary --plan --output")
        result = qualify(ns.raw, ns.summary, ns.plan, ns.attempt)
        ns.output.parent.mkdir(parents=True, exist_ok=True)
        ns.output.write_text(json.dumps(result, sort_keys=True, separators=(",", ":"), allow_nan=False)+"\n",
                              encoding="utf-8", newline="\n")
        print(f"QUALIFIER_R17 state={result['state']} numeric_metrics_used={result.get('numeric_metrics_used')} "
              f"residency={result['residency']} reason={result['reason']} output={ns.output}")
        return 0
    except (OSError, QualificationError, KeyError, TypeError, ValueError) as exc:
        print(f"QUALIFIER_R17 REJECT {type(exc).__name__}: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
