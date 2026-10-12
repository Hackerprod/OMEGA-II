#!/usr/bin/env python3
"""Single-entry r10 qualification driver; SMOKE values are never qualification inputs."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import statistics
import sys
from pathlib import Path

THRESHOLDS = {"G_ggml": 0.90, "G_Torch": 1.20, "H_KQ_MAC_per_ns": 33.1, "D_prime_hot": 2.00, "plateau_delta_max": 0.03}
CORE_SET = {*(f"P{i}" for i in range(8)), "8P"}
M_SET = {1, 4, 8, 16}
FRONTIERS = {"MD", "35"}


class QualificationError(ValueError):
    pass


def _pairs(pairs):
    out = {}
    for k, v in pairs:
        if k in out:
            raise QualificationError(f"duplicate_json_key:{k}")
        out[k] = v
    return out


def strict_json(text: str):
    def bad_constant(value):
        raise QualificationError(f"nonfinite_json_constant:{value}")
    try:
        value = json.loads(text, object_pairs_hook=_pairs, parse_constant=bad_constant)
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        raise QualificationError(f"invalid_json:{exc}") from exc
    def finite(v):
        if isinstance(v, float) and not math.isfinite(v):
            raise QualificationError("nonfinite_json_number")
        if isinstance(v, dict):
            for x in v.values(): finite(x)
        elif isinstance(v, list):
            for x in v: finite(x)
    finite(value)
    return value


def read_jsonl(path: Path):
    rows = []
    with path.open("r", encoding="utf-8-sig", newline="") as f:
        for line_no, line in enumerate(f, 1):
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
    raw = path.read_bytes()
    match = re.search(rb',"sha256":"([0-9a-f]{64})"}$', raw)
    if not match:
        raise QualificationError("plan_sha256_field_not_canonical")
    body = raw[:match.start()] + b"}"
    actual = hashlib.sha256(body).hexdigest()
    if actual != match.group(1).decode("ascii"):
        raise QualificationError("plan_sha256_mismatch")
    plan = strict_json(raw.decode("utf-8"))
    if plan.get("schema") != "T0P_S19_PLAN_V1" or not isinstance(plan.get("events"), list):
        raise QualificationError("plan_schema_invalid")
    return plan["sha256"], plan


def _finite_positive(x):
    return isinstance(x, (int, float)) and not isinstance(x, bool) and math.isfinite(x) and x > 0


def telemetry_valid(row):
    if row.get("telemetry_status") != "VALID" or row.get("status") not in {"VALID", "PASS", "SMOKE_TIMED"}:
        return False, "TELEMETRY_STATUS_NOT_VALID"
    detail = row.get("telemetry_detail")
    if not isinstance(detail, dict):
        return False, "S20_DETAIL_MISSING"
    for key in ("f_tsc_hz", "f_tsc_reported_hz", "f_active_hz", "pkg_power_w"):
        if not _finite_positive(detail.get(key)):
            return False, f"S20_{key.upper()}_NOT_ESTIMABLE"
    if abs(detail["f_tsc_hz"] / detail["f_tsc_reported_hz"] - 1.0) > .03:
        return False, "S20_F_TSC_DEVIATION_GT_3_PERCENT"
    if detail.get("thermal_current") is not False:
        return False, "S20_THERMAL_CURRENT_ACTIVE_OR_UNKNOWN"
    if not _finite_positive(detail.get("tcc_margin_c")) or detail["tcc_margin_c"] < 5:
        return False, "S20_TCC_MARGIN_LT_5C_OR_MISSING"
    if detail.get("thermal_core_seen") is not True or detail.get("thermal_pkg_seen") is not True:
        return False, "S20_THERMAL_STATUS_MISSING"
    return True, "PASS"


def eligible_records(rows):
    good, bad = [], []
    for row in rows:
        ok, reason = telemetry_valid(row)
        if ok and row.get("large_pages") is True and row.get("affinity") == "PASS":
            good.append(row)
        else:
            bad.append({"provider": row.get("provider"), "cell": row.get("cell"), "replica": row.get("replica"), "reason": reason if not ok else "PAGE_OR_AFFINITY_INVALID"})
    return good, bad


def best_reference(variants):
    eligible = [(name, values) for name, values in variants.items() if values and all(_finite_positive(v) for v in values)]
    if not eligible:
        raise QualificationError("no_eligible_reference")
    medians = [(statistics.median(values), name) for name, values in eligible]
    # The choice is made solely from each reference's own time median; ASCII id breaks ties.
    return min(medians, key=lambda pair: (pair[0], pair[1]))


def _synthetic_gate(attempt):
    if attempt.get("telemetry") != "VALID" or attempt.get("instrument") is not True:
        return {"state": "HOLD_INSTRUMENTATION", "campaign": "CLOSED_Q4_256_THIS_I7", "reason": "instrument_invalid", "residency": "NOT_STARTED"}
    if not isinstance(attempt.get("plateau_delta"), (int, float)) or attempt["plateau_delta"] > THRESHOLDS["plateau_delta_max"]:
        return {"state": "INVALID_IMPLEMENTATION", "campaign": "CLOSED_Q4_256_THIS_I7", "reason": "P_instr plateau unqualified", "residency": "NOT_STARTED"}
    for key, threshold in (("G_ggml", .90), ("G_Torch", 1.20), ("H_KQ_median", 33.1), ("H_KQ_worker_min", 33.1), ("D_prime_hot", 2.00)):
        value = attempt.get(key)
        if not _finite_positive(value) or value < threshold:
            return {"state": "FALLIDA_CUALIFICACION", "campaign": "CLOSED_Q4_256_THIS_I7", "reason": f"{key}_below_{threshold}", "residency": "NOT_STARTED"}
    return {"state": "QUALIFIED_FOR_RESIDENCY", "campaign": "OPEN", "reason": "all_gates_pass", "residency": "NOT_STARTED"}


def decide_attempts(attempts):
    if not attempts:
        return {"state": "HOLD_INSTRUMENTATION", "campaign": "CLOSED_Q4_256_THIS_I7", "reason": "no_attempt", "tries": 0, "retry_remaining": 0, "residency": "NOT_STARTED"}
    for index, attempt in enumerate(attempts[:2], 1):
        result = _synthetic_gate(attempt)
        result["tries"] = index
        result["retry_remaining"] = 1 if result["state"] == "HOLD_INSTRUMENTATION" and index == 1 and len(attempts) == 1 else 0
        if result["state"] != "HOLD_INSTRUMENTATION":
            return result
        if index == 1 and len(attempts) > 1:
            continue
        return result
    result = _synthetic_gate(attempts[min(len(attempts), 2)-1])
    result.update(tries=min(len(attempts), 2), retry_remaining=0, residency="NOT_STARTED")
    return result


def _all_smoke(rows):
    return all(row.get("label") == "SMOKE" for row in rows)


def _coords(row):
    if all(k in row for k in ("frontier", "core")):
        frontier, core = str(row["frontier"]), str(row["core"])
    else:
        parts = str(row.get("plan_event", "")).split("|")
        if len(parts) < 6:
            raise QualificationError("unit_coordinates_missing")
        core = parts[1]
        frontier = {"prepared": "35", "dynamic": "MD"}.get(parts[3], parts[3])
    m = row.get("m")
    if isinstance(m, bool) or not isinstance(m, int):
        raise QualificationError("unit_m_invalid")
    return frontier, core, m


def _block_samples(rows):
    groups = {}
    for row in rows:
        frontier, core, m = _coords(row)
        groups.setdefault((frontier, core, m, row.get("provider"), row.get("cell")), {}).setdefault(row.get("replica"), []).append(row)
    out = {}
    for key, replicas in groups.items():
        samples = []
        for replica, worker_rows in replicas.items():
            core = key[1]
            if core == "8P":
                if len(worker_rows) != 8:
                    raise QualificationError(f"worker_coverage_invalid:{key}:{replica}")
                by_worker = {r.get("worker"): r for r in worker_rows}
                if set(by_worker) != set(range(8)):
                    raise QualificationError(f"worker_ids_invalid:{key}:{replica}")
            else:
                if len(worker_rows) != 1:
                    raise QualificationError(f"single_pcore_worker_coverage_invalid:{key}:{replica}")
                by_worker = {worker_rows[0].get("worker"): worker_rows[0]}
                expected_lp = 2 * int(core[1:])
                if worker_rows[0].get("lp") != expected_lp:
                    raise QualificationError(f"single_pcore_affinity_invalid:{key}:{replica}")
            # A P-core unit contains one worker; 8P contains workers 0..7.
            # Use the rows actually present so both geometries yield their intended median.
            times = [float(r["t_app_us"]) for r in by_worker.values()]
            if not all(_finite_positive(t) for t in times):
                raise QualificationError(f"sample_time_invalid:{key}:{replica}")
            samples.append({"replica": replica, "time_us": statistics.median(times), "rows": by_worker})
        out[key] = samples
    return out


def _gate_official(rows, summary, plan_sha):
    if not all(row.get("label") == "OFFICIAL" for row in rows):
        raise QualificationError("official_mode_requires_OFFICIAL_rows")
    invalid = []
    for row in rows:
        ok, reason = telemetry_valid(row)
        if not ok:
            invalid.append({"provider": row.get("provider"), "cell": row.get("cell"), "replica": row.get("replica"), "reason": reason})
    if invalid:
        return {"state": "HOLD_INSTRUMENTATION", "campaign": "CLOSED_Q4_256_THIS_I7", "residency": "NOT_STARTED", "reason": "INVALID_TELEMETRY_OR_S20", "invalid_samples": len(invalid), "invalid_reasons": invalid, "plan_sha256": plan_sha, "numeric_metrics_used": False}
    for row in rows:
        if row.get("provider") != "P_instr" and (row.get("large_pages") is not True or row.get("affinity") != "PASS"):
            return {"state": "HOLD_INSTRUMENTATION", "campaign": "CLOSED_Q4_256_THIS_I7", "residency": "NOT_STARTED", "reason": "PAGE_OR_AFFINITY_INVALID", "numeric_metrics_used": False, "plan_sha256": plan_sha}
    grouped = _block_samples([r for r in rows if r.get("provider") not in {"BW_DRAM", "P_instr"}])
    if len(summary) == 0:
        raise QualificationError("summary_empty")
    required = {(frontier, core, m) for frontier in FRONTIERS for core in CORE_SET for m in M_SET}
    present = {(frontier, core, m) for frontier, core, m, _p, _cell in grouped}
    missing = sorted(required - present)
    if missing:
        return {"state": "HOLD_INSTRUMENTATION", "campaign": "CLOSED_Q4_256_THIS_I7", "residency": "NOT_STARTED", "reason": "MATRIX_COVERAGE_INCOMPLETE", "missing_units": [list(x) for x in missing], "plan_sha256": plan_sha, "numeric_metrics_used": False}

    def series(frontier, core, m, provider):
        hits = [(key, samples) for key, samples in grouped.items() if key[:4] == (frontier, core, m, provider)]
        if len(hits) != 1:
            raise QualificationError(f"provider_unit_ambiguous_or_missing:{frontier}:{core}:{m}:{provider}")
        samples = hits[0][1]
        if len(samples) != 31:
            raise QualificationError(f"N_not_31:{frontier}:{core}:{m}:{provider}")
        return samples

    gates = []
    reference_log = []
    for frontier, core, m in sorted(required):
        omega = series(frontier, core, m, "omega")
        omega_med = statistics.median(s["time_us"] for s in omega)
        for family, variants, threshold in (("ggml", ("ggml_flat", "ggml_repack"), .90), ("Torch", ("torch_mm", "torch_matmul", "torch_linear", "torch_mv"), 1.20)):
            choices = {}
            for variant in variants:
                try:
                    samples = series(frontier, core, m, variant)
                except QualificationError as exc:
                    if str(exc).startswith("provider_unit_ambiguous_or_missing"):
                        continue
                    raise
                choices[variant] = [s["time_us"] for s in samples]
            ref_med, ref_name = best_reference(choices)
            ratio = omega_med / ref_med
            reference_log.append({"frontier": frontier, "core": core, "m": m, "family": family, "selected": ref_name, "median_time_us": ref_med, "G": ratio})
            gates.append({"gate": "G_"+family, "frontier": frontier, "core": core, "m": m, "value": ratio, "threshold": threshold, "pass": ratio >= threshold})

    def tsc_hz(row):
        detail = row["telemetry_detail"]
        return float(detail["f_tsc_hz"])

    def mac_per_iter(row):
        value = row.get("mac_per_iteration")
        if _finite_positive(value):
            return float(value)
        output = row.get("O", 1024)
        return float(output) * 4096 * int(row["m"])

    def tsc_delta(row):
        d = int(row["tsc_end"])-int(row["tsc_start"])
        if d <= 0:
            raise QualificationError("nonpositive_TSC_interval")
        return d

    def omega_8p_perf(m):
        blocks = [samples for (f, c, mm, p, cell), samples in grouped.items() if c == "8P" and mm == m and p == "omega"]
        if len(blocks) != 2:
            # MD and 35 frontiers each have one OMEGA series for the requested KQ m.
            raise QualificationError(f"H_KQ_OMEGA_8P_SERIES_MISSING:m{m}")
        all_values, per_worker = [], {w: [] for w in range(8)}
        for samples in blocks:
            if len(samples) != 31:
                raise QualificationError(f"H_KQ_N_not_31:m{m}")
            for s in samples:
                worker_rates = []
                for w, row in s["rows"].items():
                    ns = tsc_delta(row) * 1e9 / tsc_hz(row)
                    rate = mac_per_iter(row) * int(row["R"]) / ns
                    worker_rates.append(rate)
                    per_worker[w].append(rate)
                first = min(int(r["tsc_start"]) for r in s["rows"].values())
                last = max(int(r["tsc_end"]) for r in s["rows"].values())
                ns = (last-first)*1e9/tsc_hz(next(iter(s["rows"].values())))
                total_macs = sum(mac_per_iter(r)*int(r["R"]) for r in s["rows"].values())
                all_values.append(total_macs/ns)
        return statistics.median(all_values), min(statistics.median(v) for v in per_worker.values())

    hkq = {m: omega_8p_perf(m) for m in (1, 8)}
    hkq_med = statistics.median(v[0] for v in hkq.values())
    hkq_worker_min = min(v[1] for v in hkq.values())
    gates.append({"gate": "H-KQ_median", "value": hkq_med, "threshold": 33.1, "pass": hkq_med >= 33.1})
    gates.append({"gate": "H-KQ_worker_min", "value": hkq_worker_min, "threshold": 33.1, "pass": hkq_worker_min >= 33.1})

    def instrument_series(provider, predicate, n=31):
        matched = [r for r in rows if r.get("provider") == provider and predicate(r)]
        by_rep = {}
        for r in matched:
            by_rep.setdefault(r.get("replica"), []).append(r)
        if len(by_rep) != n:
            raise QualificationError(f"{provider}_N_not_{n}")
        for rep, rs in by_rep.items():
            if len(rs) != 8 or {r.get("worker") for r in rs} != set(range(8)):
                raise QualificationError(f"{provider}_worker_coverage_invalid:{rep}")
        return by_rep

    instr_rates = {}
    for chains in (1, 2, 4, 8, 10, 12, 14):
        by_rep = instrument_series("P_instr", lambda r, c=chains: int(r.get("m", 0)) == c)
        rates = []
        for rs in by_rep.values():
            for r in rs:
                duration_ns = tsc_delta(r)*1e9/tsc_hz(r)
                rates.append(int(r["R"])*8*chains*32/duration_ns)
        instr_rates[chains] = statistics.median(rates)
    plateau_delta = abs(instr_rates[12]-instr_rates[14])/((instr_rates[12]+instr_rates[14])/2)
    gates.append({"gate": "P_instr_plateau_delta12_14", "value": plateau_delta, "threshold": .03, "pass": plateau_delta <= .03})

    bw_reps = instrument_series("BW_DRAM", lambda r: True)
    bandwidths = []
    for rs in bw_reps.values():
        first = min(int(r["tsc_start"]) for r in rs);last = max(int(r["tsc_end"]) for r in rs)
        hz = statistics.median(tsc_hz(r) for r in rs)
        time_ns = (last-first)*1e9/hz
        bytes_read = sum(int(r["R"])*256*1024*1024 for r in rs)
        bandwidths.append(bytes_read/time_ns)
    bw = statistics.median(bandwidths)
    p_hot = hkq[1][0]
    dprime = .5625*p_hot/bw
    gates.append({"gate": "D_prime_hot", "value": dprime, "threshold": 2.0, "pass": dprime >= 2.0})

    failed = [g for g in gates if not g["pass"]]
    if any(g["gate"].startswith("P_instr") for g in failed):
        state, reason = "INVALID_IMPLEMENTATION", "P_instr plateau unqualified"
    elif failed:
        state, reason = "FALLIDA_CUALIFICACION", ",".join(g["gate"] for g in failed)
    else:
        state, reason = "QUALIFIED_FOR_RESIDENCY", "all_numeric_gates_pass"
    return {"state": state, "campaign": "CLOSED_Q4_256_THIS_I7" if failed else "OPEN", "residency": "NOT_STARTED", "reason": reason, "plan_sha256": plan_sha, "numeric_metrics_used": True, "references": reference_log, "gates": gates, "H_KQ_by_m": {str(m): {"median": v[0], "worker_min": v[1]} for m, v in hkq.items()}, "P_instr_by_chains": instr_rates, "BW_DRAM_bytes_per_ns": bw}


def _synthetic_official_rows():
    variants = {"omega": 100.0, "ggml_flat": 90.0, "ggml_repack": 91.0,
                "torch_mm": 80.0, "torch_matmul": 81.0, "torch_linear": 82.0, "torch_mv": 83.0}
    detail = {"f_tsc_hz": 2.5e9, "f_tsc_reported_hz": 2.5e9, "f_active_hz": 2.5e9,
              "pkg_power_w": 100.0, "thermal_current": False, "thermal_core_seen": True,
              "thermal_pkg_seen": True, "tcc_margin_c": 20.0}
    rows = []
    def add(provider, cell, m, replica, worker, lp, time_us, *, frontier=None, core=None, r=1, mac=None):
        row = {"label": "OFFICIAL", "provider": provider, "cell": cell, "m": m, "replica": replica,
               "worker": worker, "lp": lp, "t_app_us": time_us, "R": r,
               "tsc_start": 1_000_000, "tsc_end": 2_501_000_000,
               "telemetry_status": "VALID", "status": "VALID", "telemetry_detail": detail,
               "large_pages": True, "affinity": "PASS"}
        if frontier is not None: row["frontier"] = frontier
        if core is not None: row["core"] = core
        if mac is not None: row["mac_per_iteration"] = mac
        rows.append(row)
    for frontier in sorted(FRONTIERS):
        for core in sorted(CORE_SET):
            workers = range(8) if core == "8P" else (int(core[1:]),)
            for m in sorted(M_SET):
                for provider, time_us in variants.items():
                    for replica in range(1, 32):
                        for worker in workers:
                            add(provider, provider, m, replica, worker, 2*worker, time_us,
                                frontier=frontier, core=core,
                                r=100_000_000 if provider == "omega" else 1,
                                mac=400 if provider == "omega" else None)
    for chains in (1, 2, 4, 8, 10, 12, 14):
        for replica in range(1, 32):
            for worker in range(8):
                add("P_instr", f"P_instr_U8_c{chains}", chains, replica, worker, 2*worker,
                    2_050_000.0, r=840_000//chains)
    for replica in range(1, 32):
        for worker in range(8):
            add("BW_DRAM", "BW_DRAM_256MiB_8P", 0, replica, worker, 2*worker,
                2_050_000.0, r=1)
    return rows


def qualify(raw_path: Path, summary_path: Path, plan_path: Path, attempt_number: int = 1):
    plan_sha, _ = verify_plan(plan_path)
    rows, summary = read_jsonl(raw_path), read_jsonl(summary_path)
    if _all_smoke(rows):
        valid = sum(1 for row in rows if row.get("telemetry_status") == "VALID")
        invalid = len(rows)-valid
        return {"label": "SMOKE", "state": "SMOKE_ONLY", "campaign": "CLOSED_Q4_256_THIS_I7", "residency": "NOT_STARTED", "plan_sha256": plan_sha, "raw_rows": len(rows), "summary_rows": len(summary), "valid_rows": valid, "invalid_rows": invalid, "numeric_metrics_used": False, "reason": "smoke data are excluded from scientific thresholds"}
    try:
        result = _gate_official(rows, summary, plan_sha)
    except QualificationError as exc:
        result = {"state": "HOLD_INSTRUMENTATION", "campaign": "CLOSED_Q4_256_THIS_I7", "residency": "NOT_STARTED", "reason": str(exc), "numeric_metrics_used": False, "plan_sha256": plan_sha}
    result["tries"] = attempt_number
    result["retry_remaining"] = 1 if result["state"] == "HOLD_INSTRUMENTATION" and attempt_number == 1 else 0
    return result


def self_test(out: Path):
    good = {"telemetry": "VALID", "instrument": True, "G_ggml": .95, "G_Torch": 1.25, "H_KQ_median": 34.0, "H_KQ_worker_min": 33.2, "D_prime_hot": 2.1, "plateau_delta": .02}
    cases = [
        ("all_pass", [good], "QUALIFIED_FOR_RESIDENCY"),
        ("G_ggml_below_0.90", [{**good, "G_ggml": .899}], "FALLIDA_CUALIFICACION"),
        ("G_Torch_below_1.20", [{**good, "G_Torch": 1.199}], "FALLIDA_CUALIFICACION"),
        ("H_KQ_median_below_33.1", [{**good, "H_KQ_median": 33.099}], "FALLIDA_CUALIFICACION"),
        ("H_KQ_worker_min_below_33.1", [{**good, "H_KQ_worker_min": 33.099}], "FALLIDA_CUALIFICACION"),
        ("D_prime_below_2.00", [{**good, "D_prime_hot": 1.999}], "FALLIDA_CUALIFICACION"),
        ("plateau_unstable", [{**good, "plateau_delta": .030001}], "INVALID_IMPLEMENTATION"),
        ("telemetry_invalid", [{**good, "telemetry": "INVALID_TELEMETRY", "instrument": False}], "HOLD_INSTRUMENTATION"),
        ("second_attempt_invalid", [{**good, "telemetry": "INVALID_TELEMETRY", "instrument": False}, {**good, "telemetry": "INVALID_TELEMETRY", "instrument": False}], "HOLD_INSTRUMENTATION"),
    ]
    lines = []
    for name, attempts, expected in cases:
        payload = json.dumps({"attempts": attempts}, separators=(",", ":"))
        parsed = strict_json(payload)
        result = decide_attempts(parsed["attempts"])
        if result["state"] != expected or result["residency"] != "NOT_STARTED":
            raise QualificationError(f"synthetic_case_failed:{name}:{result}")
        if result["state"] != "QUALIFIED_FOR_RESIDENCY" and result["campaign"] != "CLOSED_Q4_256_THIS_I7":
            raise QualificationError(f"adverse_case_not_closed:{name}:{result}")
        if name != "all_pass" and result["state"] in {"FALLIDA_CUALIFICACION", "INVALID_IMPLEMENTATION", "HOLD_INSTRUMENTATION"} and result["residency"] != "NOT_STARTED":
            raise QualificationError(f"adverse_case_continued:{name}")
        lines.append(f"case={name} state={result['state']} campaign={result['campaign']} reason={result['reason']} tries={result['tries']} retry_remaining={result['retry_remaining']} residency={result['residency']} continue_residency=NO PASS")
    refs = {"ggml_repack": [10, 10], "ggml_flat": [10, 10], "omega": [100, 100]}
    choice = best_reference({k: v for k, v in refs.items() if k.startswith("ggml_")})
    if choice != (10, "ggml_flat"):
        raise QualificationError(f"reference_selector_tie_failed:{choice}")
    lines.append("reference_selector=independent_of_OMEGA median_tie=ASCII_ggml_flat PASS")
    core_rows = [{"frontier": "MD", "core": "P0", "m": 1, "provider": "omega", "cell": "x", "replica": 1, "worker": 0, "lp": 0, "t_app_us": 123.0}]
    core_rows.extend({"frontier": "MD", "core": "8P", "m": 1, "provider": "omega", "cell": "x", "replica": 1, "worker": w, "lp": 2*w, "t_app_us": 100.0+w} for w in range(8))
    sample_check = _block_samples(core_rows)
    if sample_check[("MD", "P0", 1, "omega", "x")][0]["time_us"] != 123.0 or sample_check[("MD", "8P", 1, "omega", "x")][0]["time_us"] != 103.5:
        raise QualificationError("worker_coverage_single_core_or_8P_failed")
    lines.append("worker_coverage=P-core_one_worker_and_8P_eight_workers PASS")
    official_rows = _synthetic_official_rows()
    official_summary = [{"label": "OFFICIAL", "synthetic": True}]
    def reset_official_rows():
        for row in official_rows:
            row["telemetry_status"] = "VALID"
            row["tsc_start"] = 1_000_000
            row["tsc_end"] = 2_501_000_000
            if row["provider"] == "omega":
                row["t_app_us"] = 100.0
                row["mac_per_iteration"] = 400
            elif row["provider"] in {"ggml_flat", "ggml_repack", "torch_mm", "torch_matmul", "torch_linear", "torch_mv"}:
                row["t_app_us"] = {"ggml_flat": 90.0, "ggml_repack": 91.0, "torch_mm": 80.0, "torch_matmul": 81.0, "torch_linear": 82.0, "torch_mv": 83.0}[row["provider"]]
            elif row["provider"] == "P_instr":
                row["R"] = 840_000//int(row["m"])
            elif row["provider"] == "BW_DRAM":
                row["R"] = 1
    def official_case(name, expected_state, expected_failed_gate=None):
        result = _gate_official(official_rows, official_summary, "0"*64)
        failed_gates = sorted({gate["gate"] for gate in result.get("gates", []) if not gate["pass"]})
        if result["state"] != expected_state or result["residency"] != "NOT_STARTED":
            raise QualificationError(f"official_synthetic_failed:{name}:{result['state']}:{result.get('reason')}")
        if expected_failed_gate and expected_failed_gate not in failed_gates:
            raise QualificationError(f"official_synthetic_gate_not_failed:{name}:{failed_gates}")
        lines.append(f"official_case={name} state={result['state']} failed_gates={','.join(failed_gates) or 'none'} residency=NOT_STARTED PASS")
    official_case("all_gates_pass", "QUALIFIED_FOR_RESIDENCY")
    for row in official_rows:
        if row["provider"] == "ggml_flat": row["t_app_us"] = 130.0
        elif row["provider"] == "ggml_repack": row["t_app_us"] = 131.0
    official_case("G_ggml_below_0.90", "FALLIDA_CUALIFICACION", "G_ggml")
    reset_official_rows()
    for row in official_rows:
        if row["provider"] == "omega": row["t_app_us"] = 90.0
    official_case("G_Torch_below_1.20", "FALLIDA_CUALIFICACION", "G_Torch")
    reset_official_rows()
    for row in official_rows:
        if row["provider"] == "omega" and row.get("core") == "8P" and row["m"] in (1, 8):
            row["tsc_start"] = 1_000_000 + int(row["worker"])*5_000_000_000
            row["tsc_end"] = row["tsc_start"] + 2_500_000_000
    official_case("H_KQ_median_below_33.1", "FALLIDA_CUALIFICACION", "H-KQ_median")
    reset_official_rows()
    for row in official_rows:
        if row["provider"] == "omega" and row.get("core") == "8P" and row["m"] in (1, 8) and row["worker"] == 0: row["mac_per_iteration"] = 300
    official_case("H_KQ_worker_min_below_33.1", "FALLIDA_CUALIFICACION", "H-KQ_worker_min")
    reset_official_rows()
    for row in official_rows:
        if row["provider"] == "BW_DRAM": row["R"] = 100
    official_case("D_prime_below_2.00", "FALLIDA_CUALIFICACION", "D_prime_hot")
    reset_official_rows()
    for row in official_rows:
        if row["provider"] == "P_instr" and row["m"] == 14: row["R"] *= 2
    official_case("plateau_unstable", "INVALID_IMPLEMENTATION", "P_instr_plateau_delta12_14")
    reset_official_rows()
    official_rows[0]["telemetry_status"] = "INVALID_TELEMETRY"
    official_case("telemetry_invalid", "HOLD_INSTRUMENTATION")
    reset_official_rows()
    bad = [("duplicate_key", '{"x":1,"x":2}'), ("nan", '{"x":NaN}'), ("overflow", '{"x":1e9999}'), ("bad_number", '{"x":01}'), ("trailing", '{"x":1} trailing')]
    for name, fixture in bad:
        try: strict_json(fixture)
        except QualificationError: lines.append(f"strict_json case={name} rejected=PASS")
        else: raise QualificationError(f"strict_json_negative_accepted:{name}")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("QUALIFIER_R11_SYNTHETIC_QA PASS\n"+"\n".join(lines)+"\n", encoding="utf-8", newline="\n")
    print(f"QUALIFIER_R11_SYNTHETIC_QA PASS cases={len(cases)} strict_json_negatives={len(bad)} evidence={out}")


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--raw", type=Path)
    ap.add_argument("--summary", type=Path)
    ap.add_argument("--plan", type=Path)
    ap.add_argument("--output", type=Path)
    ap.add_argument("--attempt", type=int, choices=(1, 2), default=1)
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--self-test-output", type=Path, default=Path(__file__).resolve().parent/"logs_r11"/"synthetic_qa_r11.txt")
    ns = ap.parse_args(argv)
    try:
        if ns.self_test:
            self_test(ns.self_test_output)
            return 0
        if not all((ns.raw, ns.summary, ns.plan, ns.output)):
            ap.error("use --self-test or provide --raw --summary --plan --output")
        result = qualify(ns.raw, ns.summary, ns.plan, ns.attempt)
        ns.output.parent.mkdir(parents=True, exist_ok=True)
        ns.output.write_text(json.dumps(result, sort_keys=True, separators=(",", ":"))+"\n", encoding="utf-8", newline="\n")
        print(f"QUALIFIER_R11 state={result['state']} raw={result.get('raw_rows')} valid={result.get('valid_rows')} invalid={result.get('invalid_rows')} numeric_metrics_used={result.get('numeric_metrics_used')} residency={result['residency']} output={ns.output}")
        return 0
    except (OSError, QualificationError, KeyError, TypeError) as exc:
        print(f"QUALIFIER_R11 REJECT {type(exc).__name__}: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
