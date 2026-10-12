#!/usr/bin/env python3
"""S-19 r11: plan, strict A6 gate, derivation and single-run orchestration."""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import math
import os
import statistics
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
COLLECTOR = Path(__file__).resolve().parent
HARNESS = ROOT / "T0P" / "harness"
LOGS = COLLECTOR / "logs_r11"
OBJ = COLLECTOR / "obj_r11"
PLAN = COLLECTOR / "residency_plan_r11.json"
PAGE = COLLECTOR / "PAGE_EXPECTED_COLLECTOR_r11.json"
SEED = "4F4D454741543031"
SCHEMA = "T0P_S18_A6_AUTHORIZED_V1"
WS_KIB = (256, 288, 512, 768, 1024, 1280, 1536, 1792, 2048, 2560, 3072, 4096, 6144, 8192, 12288, 16384)
M_VALUES = (1, 4, 8, 16)
K_ARCH = (1, 2, 4, 8, 16)
VARIANTS = ("A", "B", "C")
AUTH_KEYS = {"schema", "campaign", "session_id", "issued_at_utc", "judge", "sol_decision_id", "scope", "master_seed", "plan_sha256", "artifacts", "usage_policy"}


class PipelineError(ValueError):
    pass


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical(value: Any) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False).encode("utf-8")


def _pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise PipelineError(f"duplicate_json_key:{key}")
        result[key] = value
    return result


def _same_json_exact(actual: Any, expected: Any) -> bool:
    if type(actual) is not type(expected):
        return False
    if isinstance(expected, dict):
        return actual.keys() == expected.keys() and all(_same_json_exact(actual[k], expected[k]) for k in expected)
    if isinstance(expected, list):
        return len(actual) == len(expected) and all(_same_json_exact(a, b) for a, b in zip(actual, expected))
    return actual == expected


def _constant(value):
    raise PipelineError(f"nonfinite_json_constant:{value}")


def strict_json_bytes(data: bytes) -> Any:
    if data.startswith(b"\xef\xbb\xbf"):
        raise PipelineError("utf8_bom_not_allowed")
    try:
        text = data.decode("utf-8", errors="strict")
        value = json.loads(text, object_pairs_hook=_pairs, parse_constant=_constant)
    except PipelineError:
        raise
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise PipelineError(f"invalid_json:{exc}") from exc

    def finite(node):
        if isinstance(node, float) and not math.isfinite(node):
            raise PipelineError("nonfinite_json_number")
        if isinstance(node, dict):
            for child in node.values():
                finite(child)
        elif isinstance(node, list):
            for child in node:
                finite(child)

    finite(value)
    return value


def strict_jsonl(path: Path) -> list[dict[str, Any]]:
    rows = []
    raw = path.read_bytes()
    if raw.startswith(b"\xef\xbb\xbf"):
        raise PipelineError("jsonl_utf8_bom_not_allowed")
    for number, line in enumerate(raw.splitlines(), 1):
        if not line.strip():
            raise PipelineError(f"blank_jsonl_line:{number}")
        row = strict_json_bytes(line)
        if not isinstance(row, dict):
            raise PipelineError(f"jsonl_object_required:{number}")
        rows.append(row)
    return rows


def half_up(numerator: int, denominator: int) -> int:
    if numerator < 0 or denominator <= 0:
        raise PipelineError("half_up_domain")
    return (2 * numerator + denominator) // (2 * denominator)


def output_for_w(w_kib: int) -> int:
    return 8 * half_up(w_kib * 1024, 8 * 288)


def plan_payload() -> dict[str, Any]:
    candidates = []
    # Architectural point: O=1024, the 60-point A/B/C x m x K matrix.
    for variant in VARIANTS:
        for m in M_VALUES:
            for k in K_ARCH:
                cid = f"arch|{variant}|m{m}|K{k}|W1024|O1024"
                key = sha(f"T0P-S19|{SEED}|residency|{cid}".encode())
                candidates.append({"id": cid, "kind": "architectural", "variant": variant, "m": m, "K": k, "W_target_kib": 1024, "O": 1024, "order_key": key})
    # The O=1024 target is already counted above; the remaining 15 sizes have K={1,16}.
    for w in WS_KIB:
        if w == 1024:
            continue
        o = output_for_w(w)
        for variant in VARIANTS:
            for m in M_VALUES:
                for k in (1, 16):
                    cid = f"sweep|{variant}|m{m}|K{k}|W{w}|O{o}"
                    key = sha(f"T0P-S19|{SEED}|residency|{cid}".encode())
                    candidates.append({"id": cid, "kind": "sweep", "variant": variant, "m": m, "K": k, "W_target_kib": w, "O": o, "order_key": key})
    candidates.sort(key=lambda row: (row["order_key"], row["id"].encode("ascii")))
    order_sha = sha("\n".join(row["id"] for row in candidates).encode("utf-8"))
    return {
        "schema": "T0P_S19_RESIDENCY_PLAN_R11_V1",
        "campaign": "T0P-Q4-RESIDENCY",
        "seed": SEED,
        "ordering": "ascending SHA256(T0P-S19|seed|residency|condition_id), ASCII id tie-break",
        "architecture": {"I": 512, "O": 1024, "P_shards": 8, "LPs": [0, 2, 4, 6, 8, 10, 12, 14]},
        "W_target_kib": list(WS_KIB),
        "m_values": list(M_VALUES),
        "variants": list(VARIANTS),
        "K_architectural": list(K_ARCH),
        "K_sweep": [1, 16],
        "N": 31,
        "warmup": 5,
        "minimum_sample_seconds": 2.0,
        "calibration_target_seconds": 2.5,
        "R_policy": "one R per (W_target_kib,m), common to A/B/C and every K; calibrated before N=31",
        "flush_and_conditioning_outside_kernel_only": True,
        "conditions": candidates,
        "condition_count": len(candidates),
        "plan_order_sha256": order_sha,
    }


def write_plan(path: Path = PLAN) -> dict[str, Any]:
    payload = plan_payload()
    plan_sha = sha(canonical(payload))
    document = dict(payload)
    document["sha256"] = plan_sha
    path.write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    return document


def verify_plan(path: Path = PLAN) -> dict[str, Any]:
    plan = strict_json_bytes(path.read_bytes())
    if not isinstance(plan, dict) or plan.get("schema") != "T0P_S19_RESIDENCY_PLAN_R11_V1":
        raise PipelineError("plan_schema_invalid")
    given = plan.get("sha256")
    if not isinstance(given, str) or len(given) != 64:
        raise PipelineError("plan_sha256_invalid")
    payload = dict(plan)
    del payload["sha256"]
    if sha(canonical(payload)) != given:
        raise PipelineError("plan_sha256_mismatch")
    expected = plan_payload()
    if not _same_json_exact(payload, expected):
        raise PipelineError("plan_content_mismatch")
    if len(plan["conditions"]) != 420 or sum(r["kind"] == "architectural" for r in plan["conditions"]) != 60 or sum(r["kind"] == "sweep" for r in plan["conditions"]) != 360:
        raise PipelineError("plan_count_invalid")
    if [r["order_key"] for r in plan["conditions"]] != sorted(r["order_key"] for r in plan["conditions"]):
        raise PipelineError("plan_order_not_sha_sorted")
    return plan


def write_smoke_subset(plan: dict[str, Any], path: Path = COLLECTOR / "test_r11" / "residency_smoke_conditions_r11.csv"):
    want = {(1024, 1, 1), (1024, 4, 4), (1024, 8, 16), (256, 1, 1), (16384, 1, 1)}
    rows = [r for r in plan["conditions"] if (r["W_target_kib"], r["m"], r["K"]) in want and r["variant"] == "A"]
    rows.sort(key=lambda r: (r["W_target_kib"], r["m"], r["K"]))
    if len(rows) != len(want):
        raise PipelineError(f"smoke_subset_missing_conditions:{len(rows)}")
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="ascii", newline="\n") as f:
        f.write("condition,W_target_kib,O,m,K\n")
        for row in rows:
            f.write(f"{row['id']},{row['W_target_kib']},{row['O']},{row['m']},{row['K']}\n")
    return path


def _median(values: list[float]) -> float:
    return float(statistics.median(values))


def _nearest_rank(values: list[float], p: float) -> float:
    if not values:
        raise PipelineError("empty_percentile")
    ordered = sorted(values)
    return ordered[max(0, math.ceil(p * len(ordered)) - 1)]


def derive_residency(rows: list[dict[str, Any]], qualification_state: str = "QUALIFIED_FOR_RESIDENCY") -> dict[str, Any]:
    if rows and all(r.get("label") == "SMOKE_ONLY" for r in rows):
        return {"label": "SMOKE_ONLY", "state": "SMOKE_ONLY", "numeric_metrics_used": False, "rows": len(rows), "residency": "NOT_ADJUDICATED"}
    if not rows:
        raise PipelineError("residency_rows_empty")
    if qualification_state not in {"QUALIFIED_FOR_RESIDENCY", "FALLIDA_CUALIFICACION", "HOLD_INSTRUMENTATION", "INVALID_IMPLEMENTATION"}:
        raise PipelineError("qualification_state_unknown")
    if qualification_state == "FALLIDA_CUALIFICACION":
        return {"state": "FALLO_COMPETENCIA", "residency": "NOT_STARTED", "numeric_metrics_used": False, "reason": "valid_competence_cut"}
    if qualification_state in {"HOLD_INSTRUMENTATION", "INVALID_IMPLEMENTATION"}:
        return {"state": "NO_ADJUDICABLE_CONTROLES", "residency": "NOT_STARTED", "numeric_metrics_used": False, "reason": qualification_state}

    times: dict[tuple[str, int, int, int, int], dict[int, float]] = {}
    aa: list[float] = []
    by_condition: dict[tuple[int, int, int, int, str], list[dict[str, Any]]] = {}
    sample_groups: dict[tuple[str, int, int, int, int, int], dict[int, dict[str, Any]]] = {}
    common_r: dict[tuple[int, int], set[int]] = {}
    for row in rows:
        if row.get("label") not in {"OFFICIAL", "SYNTHETIC"}:
            raise PipelineError("non_official_row_in_numeric_derivation")
        if row.get("phase") == "aa_control" or row.get("variant") == "AA":
            one, two = row.get("T_AA_1_us"), row.get("T_AA_2_us")
            if not all(type(x) in (int, float) and math.isfinite(x) and x > 0 for x in (one, two)):
                raise PipelineError("aa_control_invalid")
            aa.append(abs(one-two)/((one+two)/2))
            continue
        variant, m, k, w, o, rep = (row.get("variant"), row.get("m"), row.get("K"), row.get("W_target_kib"), row.get("O"), row.get("replica"))
        if variant not in VARIANTS or any(type(x) is not int or x <= 0 for x in (m, k, w, o, rep)):
            raise PipelineError("residency_key_invalid")
        time_us = row.get("T_X_us", row.get("t_app_us"))
        if type(time_us) not in (int, float) or not math.isfinite(time_us) or time_us <= 0:
            raise PipelineError("residency_time_nonpositive")
        if row.get("status") not in {"VALID", "SYNTHETIC_VALID"}:
            raise PipelineError("residency_sample_invalid")
        if row.get("large_pages") is not True or row.get("affinity") != "PASS" or row.get("numerical") not in {"S12_PASS", "PASS"}:
            raise PipelineError("residency_control_invalid")
        addresses = row.get("weight_addresses")
        if not isinstance(addresses, list) or not addresses or any(type(a) is not int or a <= 0 for a in addresses):
            raise PipelineError("weight_address_missing_or_zero")
        if variant == "B" and (len(addresses) != k or len(set(addresses)) != k):
            raise PipelineError("B_copy_coverage_or_disjointness_invalid")
        if variant in {"A", "C"} and (len(addresses) != 1 or type(row.get("copy_count")) is not int or row.get("copy_count") != 1):
            raise PipelineError("A_C_address_contract_invalid")
        worker, lp = row.get("worker"), row.get("lp")
        if type(worker) is not int or not 0 <= worker < 8 or type(lp) is not int or lp != 2*worker:
            raise PipelineError("worker_affinity_or_shard_invalid")
        r_value = row.get("R")
        if type(r_value) is not int or r_value <= 0:
            raise PipelineError("residency_R_invalid")
        if type(row.get("N")) is not int or row["N"] != 31:
            raise PipelineError("residency_N_not_31")
        common_r.setdefault((w, m), set()).add(r_value)
        group_key = (variant, w, o, m, k, rep)
        worker_group = sample_groups.setdefault(group_key, {})
        if worker in worker_group:
            raise PipelineError(f"duplicate_worker_sample:{group_key}:{worker}")
        worker_group[worker] = row
        key = (variant, w, o, m, k)
        by_condition.setdefault((w, o, m, k, variant), []).append(row)

    for (variant, w, o, m, k, rep), worker_group in sample_groups.items():
        if set(worker_group) != set(range(8)):
            raise PipelineError(f"worker_coverage_not_8:{variant}:W{w}:m{m}:K{k}:rep{rep}")
        observations = [float(worker_group[i].get("T_X_us", worker_group[i].get("t_app_us"))) for i in range(8)]
        if max(observations)-min(observations) > max(0.01, max(observations)*1e-6):
            raise PipelineError(f"worker_team_time_disagreement:{variant}:W{w}:m{m}:K{k}:rep{rep}")
        all_addresses = []
        for i in range(8):
            row = worker_group[i]
            worker_addresses = row["weight_addresses"]
            if variant == "B" and (len(worker_addresses) != k or len(set(worker_addresses)) != k):
                raise PipelineError("B_worker_copy_coverage_invalid")
            if variant in {"A", "C"} and len(worker_addresses) != 1:
                raise PipelineError("A_C_worker_single_address_invalid")
            all_addresses.extend(worker_addresses)
        if len(set(all_addresses)) != len(all_addresses):
            raise PipelineError("weight_addresses_alias_across_workers")
        times.setdefault((variant, w, o, m, k), {})[rep] = statistics.median(observations)

    for (w, m), values_r in common_r.items():
        if len(values_r) != 1:
            raise PipelineError(f"R_not_common_by_W_m:W{w}:m{m}")
    expected_cells = {(c["variant"], c["W_target_kib"], c["O"], c["m"], c["K"]) for c in plan_payload()["conditions"]}
    observed_cells = {(v, w, o, m, k) for v, w, o, m, k, _rep in sample_groups}
    if observed_cells != expected_cells:
        raise PipelineError(f"residency_condition_coverage_invalid:observed={len(observed_cells)}:expected={len(expected_cells)}")
    if set(common_r) != {(w, m) for w in WS_KIB for m in M_VALUES}:
        raise PipelineError("R_mapping_coverage_invalid")
    for cell in expected_cells:
        reps = times.get(cell, {})
        if len(reps) != 31 or set(reps) != set(range(1, 32)):
            raise PipelineError(f"N31_or_replica_coverage_invalid:{cell}")
    if any(type(r.get("sample_seconds")) not in (int, float) or r["sample_seconds"] < 2.0 for r in rows if r.get("phase") == "residency"):
        raise PipelineError("measured_sample_duration_below_2_seconds")

    if len(aa) != 31:
        raise PipelineError(f"aa_control_N_not_31:{len(aa)}")
    r_noise = _nearest_rank(aa, .95)
    series = []
    controls_ok = True
    resid_metrics = {}

    def values(variant, w, o, m, k):
        v = times.get((variant, w, o, m, k), {})
        if len(v) != 31 or set(v) != set(range(1, 32)):
            raise PipelineError(f"N31_or_replica_coverage_invalid:{variant}:W{w}:m{m}:K{k}")
        return v

    # Cell marginals are computed within replica before taking the median.
    for w in WS_KIB:
        o = 1024 if w == 1024 else output_for_w(w)
        ks = K_ARCH if w == 1024 else (1, 16)
        for m in M_VALUES:
            if 16 not in ks or 1 not in ks:
                continue
            for variant in VARIANTS:
                t1, t16 = values(variant, w, o, m, 1), values(variant, w, o, m, 16)
                marginal = [(t16[r]-t1[r])/15.0 for r in range(1, 32)]
                if any(v <= 0 for v in marginal):
                    controls_ok = False
                series.append({"W_target_kib": w, "W_real_kib": 288.0*o/1024.0, "O": o, "m": m, "variant": variant, "c_X": _median(marginal), "marginal_r": marginal})

    arch_c = {x["variant"]: x["c_X"] for x in series if x["W_target_kib"] == 1024 and x["m"] == 1}
    if any(not math.isfinite(v) or v <= 0 for v in arch_c.values()) or len(arch_c) != 3:
        controls_ok = False
    rho = arch_c.get("A", math.nan)/arch_c.get("B", math.nan) if arch_c.get("B", 0) else math.nan
    delta_bc = abs(arch_c.get("B", math.nan)-arch_c.get("C", math.nan))/((arch_c.get("B", math.nan)+arch_c.get("C", math.nan))/2) if arch_c.get("B", 0)+arch_c.get("C", 0) else math.nan

    # Throughput speedup uses the median episode time at K=16; useful MAC count is 512*O*m per application.
    p_by_m = {}
    for m in M_VALUES:
        t = values("A", 1024, 1024, m, 16)
        p_by_m[m] = _median([512.0*1024*m*16/t[r] for r in range(1, 32)])
    speedups = {m: p_by_m[m]/p_by_m[1] for m in M_VALUES}
    max_speedup = max(speedups[8], speedups[16])

    # Full W curve, using m=1. A knee requires two successive points above the initial 3-point plateau.
    curve = []
    for w in WS_KIB:
        o = 1024 if w == 1024 else output_for_w(w)
        t1, t16 = values("A", w, o, 1, 1), values("A", w, o, 1, 16)
        c = _median([(t16[r]-t1[r])/15.0 for r in range(1, 32)])
        curve.append({"W_target_kib": w, "W_real_kib": 288.0*o/1024.0, "O": o, "c_A": c, "g": c/(512.0*o)})
    curve.sort(key=lambda x: x["W_target_kib"])
    plateau = statistics.median([x["g"] for x in curve[:3]])
    knee = None
    for i in range(3, len(curve)-1):
        if curve[i]["g"] >= plateau*1.15 and curve[i+1]["g"] >= plateau*1.15:
            knee = curve[i]["W_target_kib"]
            break

    # L2 attribution only from the prequalified same-scope L2/LLC event stream.
    event_points = []
    event_valid = True
    for point in curve:
        rows_at = by_condition.get((point["W_target_kib"], point["O"], 1, 16, "A"), [])
        if len(rows_at) != 31*8 or {r.get("replica") for r in rows_at} != set(range(1,32)) or any(r.get("telemetry_status") != "VALID" or r.get("l2_event_qualified") is not True for r in rows_at):
            event_valid = False
            break
        l2_access = sum(int(r.get("l2_access", 0)) for r in rows_at)
        l2_miss = sum(int(r.get("l2_miss", 0)) for r in rows_at)
        llc_access = sum(int(r.get("llc_access", 0)) for r in rows_at)
        llc_miss = sum(int(r.get("llc_miss", 0)) for r in rows_at)
        if l2_access <= 0 or llc_access <= 0 or not (0 <= l2_miss <= l2_access and 0 <= llc_miss <= llc_access):
            event_valid = False
            break
        event_points.append({"W_target_kib": point["W_target_kib"], "O": point["O"], "l2_miss_fraction": l2_miss/l2_access, "llc_miss_fraction": llc_miss/llc_access})
    l2_state, l2_knee, b_l2 = "NOT_ESTABLISHED", None, None
    if event_valid and knee is not None and len(event_points) == len(curve):
        base = statistics.median(x["l2_miss_fraction"] for x in event_points[:3])
        for i in range(3, len(event_points)-1):
            a, b = event_points[i], event_points[i+1]
            if a["l2_miss_fraction"]-base >= .20 and b["l2_miss_fraction"]-base >= .20 and a["llc_miss_fraction"] <= .10 and b["llc_miss_fraction"] <= .10:
                l2_state, l2_knee, b_l2 = "ESTABLISHED", a["W_target_kib"], 288.0*a["O"]/1024.0
                break

    controls_ok &= math.isfinite(r_noise) and r_noise <= .05 and math.isfinite(delta_bc) and delta_bc <= .15
    control_values = {"rho": rho, "rho_max": .50, "rho_strong": .25, "delta_BC": delta_bc, "delta_BC_max": .15, "R_noise_P95": r_noise, "R_noise_max": .05, "marginals_positive": controls_ok}
    if not controls_ok:
        state = "NO_ADJUDICABLE_CONTROLES"
    elif rho > .50 or max_speedup < 1.50:
        state = "FALLO_RESIDENCIA_VALIDO"
    elif l2_state != "ESTABLISHED":
        state = "L2_NOT_ESTABLISHED"
    else:
        state = "RESIDENCIA_PASS"
    return {
        "state": state, "residency": state, "qualification": qualification_state,
        "numeric_metrics_used": True, "controls": control_values,
        "S_m": {str(k): v for k, v in speedups.items()}, "max_S8_S16": max_speedup,
        "residency_gate": {"rho_pass": rho <= .50, "delta_BC_pass": delta_bc <= .15, "S_pass": max_speedup >= 1.50},
        "L2_RESIDENCY": l2_state, "C_L2_knee_kib": l2_knee, "B_L2_operational_kib_per_worker": b_l2,
        "W_curve": curve, "L2_LLC_curve": event_points if event_valid else [], "c_X_by_W_m_variant": series,
    }


def _hash_inventory(paths: list[Path]) -> str:
    items = []
    for path in sorted(paths, key=lambda p: str(p).replace("\\", "/").lower()):
        if not path.is_file():
            raise PipelineError(f"hash_input_missing:{path}")
        rel = str(path.relative_to(ROOT)).replace("\\", "/")
        items.append(f"{sha(path.read_bytes())}  {rel}")
    return sha(("\n".join(items)+"\n").encode("utf-8"))


def _source_files() -> list[Path]:
    return [
        HARNESS / "main_r11.cpp", HARNESS / "harness_r11.hpp", HARNESS / "residency_r11.cpp",
        HARNESS / "provider_common_r11.hpp", HARNESS / "provider_omega_r11.cpp",
        HARNESS / "provider_ggml_r11.cpp", HARNESS / "provider_ggml_repack_r11.cpp",
        HARNESS / "provider_torch_r11.cpp", HARNESS / "provider_aux_r11.cpp",
        HARNESS / "json_strict_r11.hpp", HARNESS / "build_r11.cmd", HARNESS / "validate_r11.py",
        HARNESS / "telemetry_r11.py", HARNESS / "preflight_r11.ps1", HARNESS / "run_smoke_r11.ps1",
        HARNESS / "qualify_r11.py", HARNESS / "README_r11.md",
        COLLECTOR / "official_run_r11.py", COLLECTOR / "COLLECTOR_SPEC_r11.md",
        COLLECTOR / "OFFICIAL_RUN_r11.md", COLLECTOR / "qualify_r11.py", COLLECTOR / "README_r11.md",
        HARNESS / "HARNESS_SPEC_r11.md",
    ]


def _library_paths() -> dict[str, Path]:
    torch = Path(r"C:\Projects\OMEGA\.venv_cuda\Lib\site-packages\torch\lib")
    gg = ROOT / "T0P" / "ref_ggml" / "build_llama" / "bin"
    return {"ggml.dll": gg/"ggml.dll", "ggml-cpu.dll": gg/"ggml-cpu.dll", "ggml-base.dll": gg/"ggml-base.dll", "torch.dll": torch/"torch.dll", "torch_cpu.dll": torch/"torch_cpu.dll", "c10.dll": torch/"c10.dll"}


def artifact_hashes(plan: dict[str, Any]) -> dict[str, Any]:
    exe = HARNESS / "harness_r11.exe"
    manifest = COLLECTOR / "SHA256SUMS_collector_r11.txt"
    harness_manifest = HARNESS / "SHA256SUMS_harness_r11.txt"
    protocol = [COLLECTOR/"COLLECTOR_SPEC_r11.md", COLLECTOR/"OFFICIAL_RUN_r11.md", HARNESS/"HARNESS_SPEC_r11.md"]
    sources = _source_files()
    libraries = _library_paths()
    absent = [str(p) for p in [exe, manifest, harness_manifest, PAGE, PLAN, *protocol, *sources, *libraries.values()] if not p.is_file()]
    if absent:
        raise PipelineError("auth_artifacts_missing:" + ",".join(absent))
    contracts = [PAGE, PLAN, COLLECTOR/"COLLECTOR_SPEC_r11.md", HARNESS/"HARNESS_SPEC_r11.md"]
    return {
        "executable_sha256": sha(exe.read_bytes()),
        "sources_sha256": _hash_inventory(sources),
        "protocol_sha256": _hash_inventory(protocol),
        "manifest_sha256": sha(manifest.read_bytes()),
        "harness_manifest_sha256": sha(harness_manifest.read_bytes()),
        "plan_sha256": plan["sha256"],
        "plan_order_sha256": plan["plan_order_sha256"],
        "seed_sha256": sha(SEED.encode("ascii")),
        "contracts_sha256": _hash_inventory(contracts),
        "libraries_sha256": {name: sha(path.read_bytes()) for name, path in sorted(libraries.items())},
    }


def _expected_policy() -> dict[str, Any]:
    return {
        "session_count": 1,
        "technical_retry": "one_global_technical_retry_same_plan_seed_order",
        "flags": {"official": True, "emon_continuous": True, "N": 31, "warmup": 5, "residency_R_common_by_W_m": True, "dry_accept_measurements": False},
    }


def validate_authorized_bytes(data: bytes, plan: dict[str, Any], *, require_real_session: bool = False) -> dict[str, Any]:
    auth = strict_json_bytes(data)
    if not isinstance(auth, dict) or set(auth) != AUTH_KEYS:
        raise PipelineError("A6_top_level_fields_invalid")
    if type(auth.get("schema")) is not str or auth.get("schema") != SCHEMA:
        raise PipelineError("A6_version_invalid")
    if type(auth.get("campaign")) is not str or auth.get("campaign") != "T0P-Q4-RESIDENCY":
        raise PipelineError("A6_campaign_invalid")
    for name in ("session_id", "judge", "sol_decision_id", "scope"):
        if not isinstance(auth.get(name), str) or not auth[name].strip():
            raise PipelineError(f"A6_type_or_empty:{name}")
    if auth["session_id"].startswith("fixture-") and require_real_session:
        raise PipelineError("A6_test_fixture_not_official_authorization")
    if not isinstance(auth.get("issued_at_utc"), str):
        raise PipelineError("A6_issued_at_utc_invalid")
    try:
        moment = dt.datetime.fromisoformat(auth["issued_at_utc"].replace("Z", "+00:00"))
    except (AttributeError, ValueError) as exc:
        raise PipelineError("A6_issued_at_utc_invalid") from exc
    if moment.tzinfo != dt.timezone.utc or not auth["issued_at_utc"].endswith("Z"):
        raise PipelineError("A6_issued_at_utc_not_utc")
    if auth.get("master_seed") != SEED or auth.get("plan_sha256") != plan["sha256"]:
        raise PipelineError("A6_plan_or_seed_mismatch")
    if not isinstance(auth.get("artifacts"), dict):
        raise PipelineError("A6_artifacts_type_invalid")
    actual = artifact_hashes(plan)
    got = auth["artifacts"]
    if set(got) != set(actual):
        raise PipelineError("A6_artifact_fields_invalid")
    if not _same_json_exact(got, actual):
        differing = next((key for key in actual if got.get(key) != actual[key]), "unknown")
        raise PipelineError(f"A6_artifact_hash_mismatch:{differing}")
    if not _same_json_exact(auth.get("usage_policy"), _expected_policy()):
        raise PipelineError("A6_flags_or_retry_policy_invalid")
    return auth


def make_auth_fixture(path: Path, plan: dict[str, Any]):
    doc = {
        "schema": SCHEMA, "campaign": "T0P-Q4-RESIDENCY", "session_id": "fixture-r11-dry-accept-0001",
        "issued_at_utc": "2026-10-12T12:00:00Z", "judge": "TEST FIXTURE ONLY",
        "sol_decision_id": "MD/46", "scope": "S-19 Â§Â§3â€“6; A6 parser dry-accept fixture",
        "master_seed": SEED, "plan_sha256": plan["sha256"],
        "artifacts": artifact_hashes(plan), "usage_policy": _expected_policy(),
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(doc, ensure_ascii=False, indent=2)+"\n", encoding="utf-8", newline="\n")
    return doc


def synthetic_residency_rows(*, l2=True, residency_fail=False, control_fail=False) -> list[dict[str, Any]]:
    rows = []
    for w in WS_KIB:
        o = 1024 if w == 1024 else output_for_w(w)
        ks = K_ARCH if w == 1024 else (1, 16)
        for m in M_VALUES:
            for variant in VARIANTS:
                for k in ks:
                    factor = (o/1024.0)*(1.0 if w < 768 else 1.30)/m
                    slope = {"A": (1.5 if residency_fail else 1.0), "B": 2.0, "C": 2.0}[variant] * factor
                    if control_fail and variant == "C":
                        slope *= 1.6
                    for rep in range(1, 32):
                        jitter = 1.0 + ((rep % 5)-2)*0.0005
                        for worker in range(8):
                            addresses = [10_000_000 + w*100_000_000 + worker*10_000_000 + m*100_000 + k*1000 + j*64 for j in range(k if variant == "B" else 1)]
                            row = {"label": "SYNTHETIC", "phase": "residency", "variant": variant, "condition": f"{w}-{m}-{k}-{variant}", "W_target_kib": w, "O": o, "m": m, "K": k, "replica": rep, "worker": worker, "lp": 2*worker, "N": 31, "R": 5, "sample_seconds": 2.5, "T_X_us": (k*slope*jitter), "status": "SYNTHETIC_VALID", "large_pages": True, "affinity": "PASS", "numerical": "S12_PASS", "weight_addresses": addresses, "copy_count": k if variant == "B" else 1}
                            if l2 and m == 1 and k == 16 and variant == "A":
                                miss = .05 if w <= 512 else .30
                                if not control_fail:
                                    row.update({"telemetry_status": "VALID", "l2_event_qualified": True, "l2_access": 1000, "l2_miss": int(1000*miss), "llc_access": 1000, "llc_miss": 50})
                            rows.append(row)
    for rep in range(1, 32):
        rel = .061 if control_fail and rep >= 30 else .002
        rows.append({"label": "SYNTHETIC", "phase": "aa_control", "variant": "AA", "replica": rep, "T_AA_1_us": 1000.0, "T_AA_2_us": 1000.0*(1.0+rel)})
    return rows


def run_synthetic_sequence(cut: str | None = None) -> dict[str, Any]:
    sequence = ["authorization_hashes", "preflight", "QA_clock_eviction", "qualification", "qualification_cut", "residency", "W_sweep", "final_eviction_LLC", "derivation_RESULT"]
    stop_stage = {"authorization": 0, "preflight": 1, "qa": 2, "clock": 2, "eviction": 2, "qualification_hold": 3, "qualification_fail": 4, "residency": 5, "sweep": 6, "final_check": 7}
    if cut is not None and cut not in stop_stage:
        raise PipelineError(f"unknown_orchestrator_cut:{cut}")
    log = []
    for ix, stage in enumerate(sequence):
        log.append({"stage": stage, "status": "PASS"})
        if cut is not None and ix == stop_stage[cut]:
            code = {"authorization": "A6_HASH_OR_SCHEMA_FAIL", "preflight": "PREFLIGHT_HOLD", "qa": "QA_INVALID", "clock": "CLOCK_UNQUALIFIED", "eviction": "HOLD_EVICTION_NOT_QUALIFIED", "qualification_hold": "HOLD_INSTRUMENTATION", "qualification_fail": "FALLIDA_CUALIFICACION", "residency": "RESIDENCY_CONTROL_HOLD", "sweep": "SWEEP_INVALID", "final_check": "FINAL_EVICTION_OR_LLC_HOLD"}[cut]
            log[-1]["status"] = code
            return {"state": code, "log": log, "residency_started": any(x["stage"] == "residency" for x in log)}
        if stage == "qualification_cut":
            if cut == "qualification_hold":
                continue
            if cut == "qualification_fail":
                log[-1]["status"] = "VALID_COMPETENCE_FAILURE_CLOSED"
                return {"state": "FALLIDA_CUALIFICACION", "residency": "NOT_STARTED", "log": log, "residency_started": False}
    derived = derive_residency(synthetic_residency_rows(l2=True), "QUALIFIED_FOR_RESIDENCY")
    return {"state": "READY_RESULT", "log": log, "residency_started": True, "derived": derived}


def run_synthetic_retry(technical_failures: int = 0, scientific_failure: bool = False) -> dict[str, Any]:
    if technical_failures not in (0, 1, 2):
        raise PipelineError("synthetic_retry_count_invalid")
    identity = {"plan_sha256": plan_payload_hash(), "seed": SEED, "order_sha256": plan_payload()["plan_order_sha256"]}
    attempts = []
    if scientific_failure:
        return {"state": "FALLIDA_CUALIFICACION", "retry_count": 0, "attempts": [{**identity, "attempt": 1, "status": "VALID_SCIENTIFIC_FAILURE"}]}
    for attempt in (1, 2):
        status = "TECHNICAL_INVALID" if attempt <= technical_failures else "PASS"
        attempts.append({**identity, "attempt": attempt, "status": status})
        if status == "PASS":
            return {"state": "READY_RESULT", "retry_count": attempt-1, "attempts": attempts}
        if attempt == 2:
            return {"state": "HOLD_INSTRUMENTATION", "retry_count": 1, "attempts": attempts}
    raise AssertionError("unreachable synthetic retry state")


def plan_payload_hash() -> str:
    return sha(canonical(plan_payload()))


def write_full_residency_conditions(plan: dict[str, Any], r_by_w_m: dict[str, int], path: Path | None = None) -> Path:
    if len(plan.get("conditions", [])) != 420 or len(r_by_w_m) != 64:
        raise PipelineError("full_residency_csv_plan_or_R_coverage_invalid")
    path = path or LOGS/"residency_conditions_r11.csv"
    path.parent.mkdir(parents=True, exist_ok=True)
    seen: dict[tuple[int, int], int] = {}
    with path.open("w", encoding="ascii", newline="\n") as output:
        output.write("condition,W_target_kib,O,m,K,R\n")
        for condition in plan["conditions"]:
            key = f"W{condition['W_target_kib']}_m{condition['m']}"
            r_value = r_by_w_m.get(key)
            if type(r_value) is not int or r_value <= 0:
                raise PipelineError(f"full_residency_R_missing:{key}")
            cell_key = (condition["W_target_kib"], condition["m"])
            prior = seen.setdefault(cell_key, r_value)
            if prior != r_value:
                raise PipelineError(f"full_residency_R_not_common:{key}")
            output.write(f"{condition['id']},{condition['W_target_kib']},{condition['O']},{condition['m']},{condition['K']},{r_value}\n")
    return path


def self_test() -> list[str]:
    lines = []
    p = plan_payload()
    if len(p["conditions"]) != 420 or sum(x["kind"] == "architectural" for x in p["conditions"]) != 60 or sum(x["kind"] == "sweep" for x in p["conditions"]) != 360:
        raise PipelineError("plan_420_self_test_failed")
    if [output_for_w(w) for w in WS_KIB] != [912, 1024, 1824, 2728, 3640, 4552, 5464, 6368, 7280, 9104, 10920, 14560, 21848, 29128, 43688, 58256]:
        raise PipelineError("plan_O_rounding_self_test_failed")
    lines.append("plan=PASS conditions=420 architectural=60 sweep=360 sha_order=PASS O_formula=PASS")
    cases = [
        ("competence_failure", {}, "FALLIDA_CUALIFICACION", "FALLO_COMPETENCIA"),
        ("control_failure", {"control_fail": True}, "QUALIFIED_FOR_RESIDENCY", "NO_ADJUDICABLE_CONTROLES"),
        ("valid_residency_failure", {"residency_fail": True}, "QUALIFIED_FOR_RESIDENCY", "FALLO_RESIDENCIA_VALIDO"),
        ("L2_not_established", {"l2": False}, "QUALIFIED_FOR_RESIDENCY", "L2_NOT_ESTABLISHED"),
        ("residency_pass", {}, "QUALIFIED_FOR_RESIDENCY", "RESIDENCIA_PASS"),
    ]
    for name, options, qstate, expected in cases:
        data = synthetic_residency_rows(**options)
        result = derive_residency(data, qstate)
        if result["state"] != expected:
            raise PipelineError(f"deriver_case_failed:{name}:got={result['state']}:expected={expected}")
        lines.append(f"deriver_case={name} state={expected} PASS")
    smoke = derive_residency([{"label": "SMOKE_ONLY"}])
    if smoke["numeric_metrics_used"] or smoke["state"] != "SMOKE_ONLY":
        raise PipelineError("smoke_numeric_exclusion_failed")
    lines.append("pipeline_real_smoke_label=SMOKE_ONLY numeric_metrics_used=false PASS")
    bad_json = [b'{"x":1,"x":2}', b'{"x":NaN}', b'\xff', b'{"x":1.0e999}']
    for i, raw in enumerate(bad_json):
        try:
            strict_json_bytes(raw)
        except PipelineError:
            continue
        raise PipelineError(f"strict_json_negative_accepted:{i}")
    lines.append("strict_json_negatives=duplicate,NaN,invalid_UTF8,overflow REJECT PASS")
    cuts = ("authorization", "preflight", "qa", "clock", "eviction", "qualification_hold", "qualification_fail", "residency", "sweep", "final_check")
    for cut in cuts:
        result = run_synthetic_sequence(cut)
        if result["state"] == "READY_RESULT":
            raise PipelineError(f"orchestrator_cut_not_taken:{cut}")
        if cut == "qualification_fail" and result.get("residency_started"):
            raise PipelineError("qualification_fail_did_not_cut_before_residency")
        lines.append(f"orchestrator_cut={cut} state={result['state']} residency_started={result['residency_started']} PASS")
    complete = run_synthetic_sequence()
    if complete["state"] != "READY_RESULT" or complete["derived"]["state"] != "RESIDENCIA_PASS":
        raise PipelineError("synthetic_full_pipeline_failed")
    lines.append("orchestrator_full=PASS stages=9 derived=RESIDENCIA_PASS synthetic_only=true")
    retry_once = run_synthetic_retry(technical_failures=1)
    retry_twice = run_synthetic_retry(technical_failures=2)
    scientific = run_synthetic_retry(scientific_failure=True)
    if retry_once["state"] != "READY_RESULT" or retry_once["retry_count"] != 1 or retry_once["attempts"][0]["plan_sha256"] != retry_once["attempts"][1]["plan_sha256"]:
        raise PipelineError("single_technical_retry_self_test_failed")
    if retry_twice["state"] != "HOLD_INSTRUMENTATION" or retry_twice["retry_count"] != 1 or scientific["retry_count"] != 0:
        raise PipelineError("retry_limit_or_scientific_no_retry_self_test_failed")
    lines.append("orchestrator_retry=one_global_same_plan_seed_order PASS second_invalid=HOLD scientific_failure=no_retry PASS")
    return lines


def auth_self_test(plan: dict[str, Any]) -> list[str]:
    fixture = COLLECTOR / "test_r11" / "authorized_fixture_valid.json"
    good = make_auth_fixture(fixture, plan)
    validate_authorized_bytes(fixture.read_bytes(), plan)
    lines = ["auth_fixture=VALID dry_accept=READY_TO_RUN measurements_started=false PASS"]
    cases = []
    bad_version = dict(good); bad_version["schema"] = "T0P_S18_A6_AUTHORIZED_V0"; cases.append(("version", bad_version))
    bad_type = dict(good); bad_type["master_seed"] = 42; cases.append(("type", bad_type))
    bad_hash = json.loads(json.dumps(good)); bad_hash["artifacts"]["executable_sha256"] = "0"*64; cases.append(("hash_mismatch", bad_hash))
    bad_extra = dict(good); bad_extra["unexpected"] = True; cases.append(("extra_field", bad_extra))
    bad_missing = dict(good); del bad_missing["judge"]; cases.append(("missing_field", bad_missing))
    bad_policy = json.loads(json.dumps(good)); bad_policy["usage_policy"]["flags"]["N"] = 3; cases.append(("flags", bad_policy))
    bad_bool_type = json.loads(json.dumps(good)); bad_bool_type["usage_policy"]["flags"]["official"] = 1; cases.append(("nested_boolean_type", bad_bool_type))
    for name, doc in cases:
        try:
            validate_authorized_bytes(canonical(doc), plan)
        except PipelineError:
            lines.append(f"auth_negative={name} REJECT PASS")
        else:
            raise PipelineError(f"auth_negative_accepted:{name}")
    duplicate = b'{"schema":"x","schema":"y"}'
    try:
        strict_json_bytes(duplicate)
    except PipelineError:
        lines.append("auth_negative=duplicate_key REJECT PASS")
    else:
        raise PipelineError("auth_duplicate_key_accepted")
    try:
        validate_authorized_bytes(fixture.read_bytes(), plan, require_real_session=True)
    except PipelineError as exc:
        if str(exc) != "A6_test_fixture_not_official_authorization":
            raise
        lines.append("auth_negative=test_fixture_as_official REJECT PASS")
    else:
        raise PipelineError("auth_fixture_accepted_as_official")
    return lines


def page_gate() -> dict[str, Any]:
    contract = strict_json_bytes(PAGE.read_bytes())
    if contract.get("schema") != "T0P_COLLECTOR_PAGE_CONTRACT_R11_V1":
        raise PipelineError("page_contract_schema_invalid")
    q = contract.get("qualification", {})
    r = contract.get("residency", {})
    t = contract.get("telemetry", {})
    expected_q = {"G_ggml_min": .90, "G_Torch_min": 1.20, "H_KQ_MAC_per_ns_per_worker_min": 33.1, "D_prime_hot_min": 2.0, "P_instr_delta12_14_max": .03, "N": 31, "warmup": 5, "sample_seconds_min": 2.0, "calibration_seconds_target_min": 2.5}
    expected_r = {"condition_count": 420, "architectural_conditions": 60, "W_sweep_conditions": 360, "required_variants": list(VARIANTS), "W_target_kib": list(WS_KIB), "O_formula": "8*round_half_up(W_target_kib*1024/(8*288))", "K_architectural": list(K_ARCH), "K_sweep": [1, 16], "R_common_by": ["W_target_kib", "m"], "rho_max": .50, "rho_strong": .25, "delta_BC_max": .15, "matrix_speedup_min_max_S8_S16": 1.50, "matrix_speedup_strong": 2.0, "R_noise_P95_max": .05, "knee_elevation_min": .15, "knee_consecutive_points_min": 2, "knee_plateau_initial_points": 3, "L2_miss_fraction_increase_min_pp": .20, "LLC_miss_fraction_max": .10, "TSC_QPC_error_max": .001, "instrumentation_overhead_max": .01}
    expected_t = {"nominal_interval_seconds": .05, "window_seconds_max": .1, "windows_per_core_condition_min": 20, "coverage_min": .95, "gap_seconds_max": .02, "alignment_uncertainty_ms_max": 1.0, "frequency_excursion_fraction_max": .03, "energy_unit_joules": 1/16384}
    for name, actual, expected in (("qualification", q, expected_q), ("residency", r, expected_r), ("telemetry", t, expected_t)):
        for key, value in expected.items():
            if key not in actual or not _same_json_exact(actual[key], value):
                raise PipelineError(f"page_contract_threshold_or_geometry_drift:{name}.{key}")
    if contract.get("states", {}).get("retry_technical_global_max") != 1:
        raise PipelineError("page_contract_retry_policy_drift")
    return {"status": "PASS", "schema": contract["schema"], "conditions": r["condition_count"], "N": q["N"], "thresholds": "S19+S20_EXACT"}


def write_result(result: dict[str, Any], name: str = "RESULT_r11.json") -> Path:
    LOGS.mkdir(parents=True, exist_ok=True)
    path = LOGS / name
    path.write_text(json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False)+"\n", encoding="utf-8", newline="\n")
    return path


def run_official_gate() -> int:
    LOGS.mkdir(parents=True, exist_ok=True)
    result = {"campaign": "T0P-Q4-RESIDENCY", "mode": "OFFICIAL", "measurements_started": False, "stages": []}
    try:
        plan = verify_plan()
        result["stages"].append({"stage": 1, "name": "authorization_hashes", "status": "CHECKING"})
        auth_path = COLLECTOR / "AUTHORIZED.json"
        if not auth_path.is_file():
            result["stages"][-1].update({"status": "HOLD", "reason": "AUTHORIZED.json_missing"})
            result.update({"state": "HOLD_AUTHORIZATION", "reason": f"missing:{auth_path}"})
            write_result(result, "official_fail_closed_r11.json")
            print(f"OFFICIAL_REJECT reason=AUTHORIZED.json_missing path={auth_path} measurements_started=false")
            return 20
        validate_authorized_bytes(auth_path.read_bytes(), plan, require_real_session=True)
        result["stages"][-1]["status"] = "PASS"
        # S-20's combined reader is explicitly still unqualified in the source record.
        reader_certificate = COLLECTOR/"S20_READER_QUALIFICATION_r11.json"
        result["stages"].append({"stage": 2, "name": "preflight", "status": "HOLD", "reason": "S20_reader_qualification_certificate_missing", "required_path": str(reader_certificate)})
        result.update({"state": "HOLD_INSTRUMENTATION", "reason": f"missing:{reader_certificate}; TSC/QPC + joint EMON channel reader requires qualified session certificate", "measurements_started": False})
        write_result(result, "official_fail_closed_r11.json")
        print(f"OFFICIAL_REJECT reason=S20_reader_qualification_certificate_missing path={reader_certificate} measurements_started=false")
        return 21
    except PipelineError as exc:
        result.update({"state": "HOLD_AUTHORIZATION_OR_HASHES", "reason": str(exc)})
        write_result(result, "official_fail_closed_r11.json")
        print(f"OFFICIAL_REJECT reason={exc} measurements_started=false")
        return 20


def estimate_session_from_smoke(rows: list[dict[str, Any]], smoke_capture: Path, *, emon_groups: int, emon_elapsed_seconds: float) -> dict[str, Any]:
    if not rows or not all(r.get("label") == "SMOKE_ONLY" for r in rows):
        raise PipelineError("estimate_requires_real_smoke_only_rows")
    grouped: dict[tuple[int, int, str, int, int], list[dict[str, Any]]] = {}
    for row in rows:
        grouped.setdefault((row["W_target_kib"], row["m"], row["variant"], row["K"], row["replica"]), []).append(row)
    sample_lists: dict[tuple[int, int, str, int], list[dict[str, float]]] = {}
    for (w, m, variant, k, _rep), worker_rows in grouped.items():
        sample_lists.setdefault((w, m, variant, k), []).append({
            "T_X_us": statistics.median(float(x["T_X_us"]) for x in worker_rows),
            "flush_us": max(float(x.get("flush_us", 0)) for x in worker_rows),
            "conditioning_us": max(float(x.get("conditioning_us", 0)) for x in worker_rows),
            "barrier_us": max(float(x.get("barrier_us", 0)) for x in worker_rows),
        })
    samples = {key: {field: statistics.median(x[field] for x in values) for field in ("T_X_us", "flush_us", "conditioning_us", "barrier_us")} for key, values in sample_lists.items()}
    def nearest(w: int, m: int, variant: str, k: int) -> tuple[tuple[int, int, str, int], dict[str, float]]:
        o = output_for_w(w)
        eligible = [(key, value) for key, value in samples.items() if key[2] == variant]
        key, value = min(eligible, key=lambda item: abs(math.log(output_for_w(item[0][0])/o)) + abs(math.log(item[0][1]/m)) + .5*abs(math.log(item[0][3]/k)))
        return key, value
    r_map = {}
    predicted = {}
    for w in WS_KIB:
        o = output_for_w(w)
        for m in M_VALUES:
            fastest = math.inf
            for variant in VARIANTS:
                ref_key, ref = nearest(w, m, variant, 1)
                app_us = ref["T_X_us"] / ref_key[3] * (o/output_for_w(ref_key[0])) * (m/ref_key[1])
                fastest = min(fastest, app_us)
            if not math.isfinite(fastest) or fastest <= 0:
                raise PipelineError(f"R_calibration_unavailable:W{w}:m{m}")
            r_map[f"W{w}_m{m}"] = max(1, math.ceil(2_500_000.0/fastest))
            predicted[(w, m)] = fastest

    total_measured_us = 0.0
    total_warmup_us = 0.0
    total_side_us = 0.0
    total_warmup_side_us = 0.0
    event_count = 0
    for condition in plan_payload()["conditions"]:
        w, m, variant, k, o = condition["W_target_kib"], condition["m"], condition["variant"], condition["K"], condition["O"]
        ref_key, ref = nearest(w, m, variant, k)
        ref_o = output_for_w(ref_key[0])
        predicted_episode_us = ref["T_X_us"] * (k/ref_key[3]) * (o/ref_o) * (m/ref_key[1])
        special_flush = variant in {"B", "C"}
        ref_special_flush = variant in {"B", "C"}
        flush_scale = (o/ref_o) * ((k/ref_key[3]) if special_flush and ref_special_flush else 1.0)
        cond_scale = (o/ref_o) * (m/ref_key[1])
        barrier_scale = k/ref_key[3]
        side_us = ref["flush_us"]*flush_scale + ref["conditioning_us"]*cond_scale + ref["barrier_us"]*barrier_scale
        repeat_count = 31*r_map[f"W{w}_m{m}"]
        warmup_count = 5*r_map[f"W{w}_m{m}"]
        total_measured_us += repeat_count*predicted_episode_us
        total_warmup_us += warmup_count*predicted_episode_us
        total_side_us += repeat_count*side_us
        total_warmup_side_us += warmup_count*side_us
        # One compact event record per sample/application aggregates its R episode timings.
        event_count += 31*k
    conditions_csv = write_full_residency_conditions(verify_plan(), r_map)
    measured_seconds = total_measured_us/1e6
    warmup_seconds = total_warmup_us/1e6
    side_seconds = (total_side_us+total_warmup_side_us)/1e6
    total_seconds = measured_seconds+warmup_seconds+side_seconds
    emon_bytes = sum((smoke_capture/name).stat().st_size for name in ("emon_stdout.txt", "emon_stderr.txt") if (smoke_capture/name).is_file())
    bytes_per_group = emon_bytes/max(1, emon_groups)
    seconds_per_group = emon_elapsed_seconds/max(1, emon_groups)
    emon_est_groups = math.ceil(total_seconds/seconds_per_group)
    emon_est_bytes = math.ceil(emon_est_groups*bytes_per_group)
    raw_bytes_per_row = (smoke_capture/"residency_raw_r11.jsonl").stat().st_size/max(1, len(rows))
    raw_est_bytes = math.ceil(raw_bytes_per_row*420*31*8)
    with (smoke_capture/"residency_events_r11.jsonl").open("rb") as event_file:
        event_rows_smoke = sum(1 for _ in event_file)
    event_bytes_per_row = (smoke_capture/"residency_events_r11.jsonl").stat().st_size/max(1, event_rows_smoke)
    event_est_bytes = math.ceil(event_bytes_per_row*event_count)
    estimate = {
        "label": "ESTIMATE_FROM_SMOKE_ONLY",
        "plan_sha256": verify_plan()["sha256"],
        "condition_count": 420,
        "N": 31,
        "warmup": 5,
        "target_sample_seconds": 2.5,
        "R_common_by_W_m": r_map,
        "conditions_csv": str(conditions_csv),
        "conditions_csv_sha256": sha(conditions_csv.read_bytes()),
        "seconds": {"measured_kernel": measured_seconds, "warmup_kernel": warmup_seconds, "flush_conditioning_barrier": side_seconds, "total": total_seconds},
        "total_hours": total_seconds/3600,
        "emon": {"smoke_groups": emon_groups, "smoke_elapsed_seconds": emon_elapsed_seconds, "groups_per_second": 1/seconds_per_group, "bytes_per_group": bytes_per_group, "estimated_groups": emon_est_groups, "estimated_bytes": emon_est_bytes},
        "disk": {"raw_jsonl_bytes": raw_est_bytes, "event_jsonl_bytes": event_est_bytes, "emon_bytes": emon_est_bytes, "total_bytes": raw_est_bytes+event_est_bytes+emon_est_bytes},
        "extrapolation": "nearest smoke point by log(O),log(m),log(K); T_X scales linearly with O,m; R calibrated to 2.5s using the fastest A/B/C K=1 estimate within each (W,m); smoke includes five cells only; resource estimates are planning estimates, not official measurements",
    }
    LOGS.mkdir(parents=True, exist_ok=True)
    json_path = LOGS/"SESSION_ESTIMATE_r11.json"
    md_path = LOGS/"SESSION_ESTIMATE_r11.md"
    json_path.write_text(json.dumps(estimate, ensure_ascii=False, indent=2, allow_nan=False)+"\n", encoding="utf-8", newline="\n")
    md_path.write_text("# Estimación de sesión oficial R11\n\n" +
        f"- Duración total estimada: **{total_seconds/3600:.2f} h** ({total_seconds/86400:.2f} días).\n" +
        f"- Captura EMON: **{emon_est_bytes/1e9:.2f} GB** estimados, {emon_est_groups:,} grupos.\n" +
        f"- JSONL crudo: **{raw_est_bytes/1e9:.2f} GB**; eventos por aplicación: **{event_est_bytes/1e9:.2f} GB**.\n" +
        f"- Espacio estimado total de captura: **{(raw_est_bytes+event_est_bytes+emon_est_bytes)/1e9:.2f} GB** (sin margen libre operativo).\n\n" +
        f"R común por W,m: `{json.dumps(r_map, sort_keys=True)}`.\n\n" +
        "La duración incluye las 420 condiciones, N=31, R episodios, cinco warmups, flush, acondicionamiento, barreras y EMON escalado desde el smoke. Excluye preflight, QA/reloj/expulsión, cualificación previa y sus controles finales, cuyos tiempos no están calibrados en esta captura; por eso el total end-to-end será mayor.\n\n" +
        "Extrapolación desde cinco celdas SMOKE_ONLY: vecino más cercano en log(O), log(m) y log(K), con escala lineal O×m. R apunta a 2,5 s para K=1 por W,m y se comparte entre variantes y K. Son cifras de planificación; no son medidas oficiales.\n",
        encoding="utf-8", newline="\n")
    return {"json": estimate, "json_path": json_path, "md_path": md_path}


def run_real_smoke(harness_path: Path | None = None) -> int:
    plan = verify_plan()
    subset = write_smoke_subset(plan)
    harness_path = harness_path or HARNESS / "harness_r11.exe"
    run_id = "residency_smoke_" + dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    cap = HARNESS / "logs_r11" / run_id
    cap.mkdir(parents=True, exist_ok=False)
    collector_cap = LOGS / run_id
    collector_cap.mkdir(parents=True, exist_ok=True)
    raw = cap / "residency_raw_r11.jsonl"
    summary = cap / "residency_summary_r11.jsonl"
    emon = Path(r"C:\Program Files (x86)\Intel\oneAPI\vtune\2026.4\bin64\emon.exe")
    edp_source = ROOT / "T0P" / "telemetry" / "reader" / "emon_joint_qualification.edp"
    emon_proc = None
    emon_state = "UNAVAILABLE"
    emon_error = ""
    emon_started_at = None
    emon_elapsed_seconds = 0.0
    if emon.is_file() and edp_source.is_file():
        edp = cap / "emon_joint_residency_smoke_r11.edp"
        text = edp_source.read_text(encoding="utf-8", errors="strict")
        if text.count("-l3600") != 1:
            emon_error = "source_EDP_missing_unique_-l3600"
        else:
            edp.write_text(text.replace("-l3600", "-l1400"), encoding="utf-8", newline="\n")
            stdout = (cap/"emon_stdout.txt").open("wb")
            stderr = (cap/"emon_stderr.txt").open("wb")
            env = dict(os.environ)
            env["PATH"] = str(emon.parent)+";"+env.get("PATH", "")
            try:
                flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
                emon_started_at = time.monotonic()
                emon_proc = subprocess.Popen([str(emon), "-collect-edp", f"edp_file={edp}"], cwd=cap, stdout=stdout, stderr=stderr, env=env, creationflags=flags)
                time.sleep(3)
                if emon_proc.poll() is not None:
                    emon_error = f"EMON_exited_before_harness:exit={emon_proc.returncode}"
                    emon_proc = None
                else:
                    emon_state = "RUNNING_CONTINUOUS"
            except OSError as exc:
                emon_error = f"EMON_start_error:{type(exc).__name__}:{exc}"
            finally:
                if emon_proc is None:
                    stdout.close(); stderr.close()
    else:
        emon_error = f"EMON_or_EDP_missing:emon={emon.is_file()} edp={edp_source.is_file()}"
    if not harness_path.is_file():
        result = {"state": "BLOCKED_HARNESS_MISSING", "command": str(harness_path), "error": "FileNotFoundError", "emon": emon_state, "emon_error": emon_error}
        write_result(result, f"{run_id}_result.json")
        print(f"SMOKE_REAL_BLOCKED command={harness_path} error=FileNotFoundError")
        return 2
    cmd = [str(harness_path), "--residency-smoke", str(cap), str(subset), "3"]
    (cap/"smoke_command.txt").write_text(subprocess.list2cmdline(cmd)+"\n", encoding="utf-8")
    with (cap/"harness.stdout.txt").open("wb") as out, (cap/"harness.stderr.txt").open("wb") as err:
        env = dict(os.environ)
        env["PATH"] = str(ROOT/"T0P"/"ref_ggml"/"build_llama"/"bin")+";"+str(Path(r"C:\Projects\OMEGA\.venv_cuda\Lib\site-packages\torch\lib"))+";"+env.get("PATH", "")
        run = subprocess.run(cmd, cwd=ROOT, stdout=out, stderr=err, env=env, check=False)
    harness_rc = run.returncode
    if emon_proc is not None:
        deadline = time.monotonic()+240
        while emon_proc.poll() is None and time.monotonic() < deadline:
            time.sleep(1)
        if emon_proc.poll() is None:
            emon_proc.terminate()
            emon_error = "EMON_1400_group_capture_timeout_240s"
            emon_state = "INCOMPLETE"
        else:
            emon_state = "COMPLETE" if emon_proc.returncode == 0 else "FAILED"
        if emon_started_at is not None:
            emon_elapsed_seconds = time.monotonic()-emon_started_at
        try:
            stdout.close(); stderr.close()
        except UnboundLocalError:
            pass
    if raw.is_file():
        rows = strict_jsonl(raw)
    else:
        rows = []
    expected_rows = 5*3*3*8
    valid_shape = len(rows) == expected_rows and {r.get("variant") for r in rows} == set(VARIANTS) and {r.get("W_target_kib") for r in rows} >= {256, 1024, 16384} and all(r.get("label") == "SMOKE_ONLY" for r in rows)
    smoke_derivation = derive_residency(rows) if rows else {"label": "SMOKE_ONLY", "state": "SMOKE_ONLY", "numeric_metrics_used": False, "residency": "NOT_ADJUDICATED", "rows": 0}
    stdout_text = (cap/"harness.stdout.txt").read_text(encoding="utf-8", errors="replace") if (cap/"harness.stdout.txt").is_file() else ""
    output_equivalence = stdout_text.count("RESIDENCY_OUTPUT_EQUIVALENCE") == len(subset.read_text(encoding="ascii").splitlines())-1 and "A=B=C PASS" in stdout_text
    timer_qualified = "TIMER_OVERHEAD" in stdout_text and "status=PASS" in stdout_text.split("TIMER_OVERHEAD", 1)[1].splitlines()[0]
    clock_qualified = "CLOCK_QUALIFICATION" in stdout_text and "invariant_TSC=PASS affinity_migration=PASS" in stdout_text
    estimate_result = None
    estimate_error = ""
    if valid_shape and emon_state == "COMPLETE":
        try:
            estimate_result = estimate_session_from_smoke(rows, cap, emon_groups=1400, emon_elapsed_seconds=emon_elapsed_seconds)
            __import__("shutil").copy2(estimate_result["json_path"], cap/"SESSION_ESTIMATE_r11.json")
            __import__("shutil").copy2(estimate_result["md_path"], cap/"SESSION_ESTIMATE_r11.md")
        except (PipelineError, OSError, ValueError) as exc:
            estimate_error = f"{type(exc).__name__}:{exc}"
    shutil_copy = __import__("shutil").copy2
    for src in cap.iterdir():
        if src.is_file():
            shutil_copy(src, collector_cap/src.name)
    result = {"label": "SMOKE_ONLY", "state": "PASS" if harness_rc == 0 and valid_shape and output_equivalence and timer_qualified and clock_qualified and smoke_derivation["numeric_metrics_used"] is False and emon_state == "COMPLETE" and estimate_result is not None else "BLOCKED_OR_FAIL", "numeric_metrics_used": False, "smoke_derivation": smoke_derivation, "output_equivalence": output_equivalence, "timer_overhead_qualified": timer_qualified, "clock_qualified": clock_qualified, "emon_elapsed_seconds": emon_elapsed_seconds, "estimate_error": estimate_error, "estimated_duration_hours": estimate_result["json"]["total_hours"] if estimate_result else None, "estimated_disk_bytes": estimate_result["json"]["disk"]["total_bytes"] if estimate_result else None, "harness_command": subprocess.list2cmdline(cmd), "harness_exit": harness_rc, "raw_rows": len(rows), "expected_raw_rows": expected_rows, "shape_pass": valid_shape, "emon_state": emon_state, "emon_error": emon_error, "harness_capture": str(cap), "collector_capture": str(collector_cap)}
    write_result(result, f"{run_id}_result.json")
    estimate_hours = f"{estimate_result['json']['total_hours']:.2f}" if estimate_result else "BLOCKED"
    print(f"SMOKE_REAL label=SMOKE_ONLY harness_exit={harness_rc} rows={len(rows)}/{expected_rows} O=912,1024,58256 variants=A/B/C N=3 output_equivalence={'PASS' if output_equivalence else 'FAIL'} timer={'PASS' if timer_qualified else 'FAIL'} clock={'PASS' if clock_qualified else 'FAIL'} EMON={emon_state} derivation=SMOKE_ONLY estimate_hours={estimate_hours} numeric_metrics_used=false")
    return 0 if result["state"] == "PASS" else (3 if harness_rc != 0 or not valid_shape else 4)


def write_synthetic_run(cut: str | None = None) -> dict[str, Any]:
    result = run_synthetic_sequence(cut)
    result.update({"label": "SYNTHETIC_ONLY", "campaign": "T0P-Q4-RESIDENCY", "numeric_metrics_used_for_science": False, "measurements_started": False})
    path = write_result(result, "dry_run_synthetic_r11.json")
    print(f"DRY_RUN_SYNTHETIC state={result['state']} stages={len(result['log'])} measurements_started=false result={path}")
    return result


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--build-plan", action="store_true")
    mode.add_argument("--plan-check", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    mode.add_argument("--auth-self-test", action="store_true")
    mode.add_argument("--dry-accept-fixture", type=Path, metavar="PATH")
    mode.add_argument("--test-orchestrator", action="store_true")
    mode.add_argument("--page-gate", action="store_true")
    mode.add_argument("--dry-run-synthetic", action="store_true")
    mode.add_argument("--official", action="store_true")
    mode.add_argument("--smoke-real", action="store_true")
    mode.add_argument("--estimate-smoke", type=Path, metavar="RESULT_JSON")
    mode.add_argument("--hash-check", action="store_true")
    mode.add_argument("--derive", nargs=2, metavar=("RAW_JSONL", "QUALIFICATION_JSON"))
    ap.add_argument("--cut", choices=("authorization", "preflight", "qa", "clock", "eviction", "qualification_hold", "qualification_fail", "residency", "sweep", "final_check"))
    ap.add_argument("--harness", type=Path)
    ns = ap.parse_args(argv)
    try:
        if ns.build_plan:
            plan = write_plan(); subset = write_smoke_subset(plan)
            print(f"PLAN_R11 PASS conditions={plan['condition_count']} arch=60 sweep=360 sha256={plan['sha256']} order_sha256={plan['plan_order_sha256']} smoke_subset={subset}")
            return 0
        if ns.plan_check:
            p = verify_plan(); gate = page_gate()
            print(f"PLAN_R11 PASS conditions={len(p['conditions'])} arch=60 sweep=360 sha256={p['sha256']} order_sha256={p['plan_order_sha256']} PAGE_GATE={gate['status']}")
            return 0
        if ns.self_test or ns.test_orchestrator:
            LOGS.mkdir(parents=True, exist_ok=True)
            lines = self_test()
            out = LOGS/"pipeline_self_test_r11.txt"
            out.write_text("PIPELINE_R11_SELF_TEST PASS\n"+"\n".join(lines)+"\n", encoding="utf-8", newline="\n")
            print(f"PIPELINE_R11_SELF_TEST PASS cases={len(lines)} evidence={out}")
            return 0
        if ns.auth_self_test:
            p = verify_plan(); lines = auth_self_test(p)
            out = LOGS/"auth_a6_self_test_r11.txt"
            LOGS.mkdir(parents=True, exist_ok=True)
            out.write_text("A6_R11_SELF_TEST PASS\n"+"\n".join(lines)+"\n", encoding="utf-8", newline="\n")
            print(f"A6_R11_SELF_TEST PASS negatives={sum('auth_negative=' in x for x in lines)} fixture=test_r11/authorized_fixture_valid.json evidence={out}")
            return 0
        if ns.dry_accept_fixture is not None:
            p = verify_plan()
            fixture = ns.dry_accept_fixture.resolve()
            allowed = (COLLECTOR/"test_r11").resolve()
            if allowed not in fixture.parents or fixture.name == "AUTHORIZED.json":
                raise PipelineError("dry_accept_fixture_must_be_nonofficial_test_path")
            validate_authorized_bytes(fixture.read_bytes(), p)
            result = {"label": "TEST_FIXTURE_ONLY", "state": "READY_TO_RUN", "measurements_started": False, "official_authorization": False, "fixture": str(fixture)}
            output = write_result(result, "dry_accept_fixture_r11.json")
            print(f"DRY_ACCEPT state=READY_TO_RUN fixture={fixture.relative_to(COLLECTOR)} measurements_started=false official_authorization=false result={output}")
            return 0
        if ns.page_gate:
            g = page_gate(); print(f"PAGE_GATE_R11 {json.dumps(g,sort_keys=True)}"); return 0
        if ns.dry_run_synthetic:
            write_synthetic_run(ns.cut); return 0
        if ns.official:
            return run_official_gate()
        if ns.smoke_real:
            return run_real_smoke(ns.harness)
        if ns.estimate_smoke is not None:
            metadata = strict_json_bytes(ns.estimate_smoke.read_bytes())
            cap = Path(metadata["harness_capture"])
            rows = strict_jsonl(cap/"residency_raw_r11.jsonl")
            estimate = estimate_session_from_smoke(rows, cap, emon_groups=1400, emon_elapsed_seconds=float(metadata["emon_elapsed_seconds"]))
            metadata["estimated_duration_hours"] = estimate["json"]["total_hours"]
            metadata["estimated_disk_bytes"] = estimate["json"]["disk"]["total_bytes"]
            metadata["estimate_error"] = ""
            write_result(metadata, ns.estimate_smoke.name)
            print(f"ESTIMATE_R11 hours={estimate['json']['total_hours']:.2f} R_common_cells={len(estimate['json']['R_common_by_W_m'])} emon_groups={estimate['json']['emon']['estimated_groups']} emon_bytes={estimate['json']['emon']['estimated_bytes']} event_bytes={estimate['json']['disk']['event_jsonl_bytes']} output={estimate['json_path']}")
            return 0
        if ns.derive:
            raw_path, qualification_path = map(Path, ns.derive)
            qualification = strict_json_bytes(qualification_path.read_bytes())
            result = derive_residency(strict_jsonl(raw_path), qualification.get("state", "HOLD_INSTRUMENTATION"))
            output = write_result(result, "residency_derivation_r11.json")
            print(f"DERIVE_R11 state={result['state']} numeric_metrics_used={result['numeric_metrics_used']} output={output}")
            return 0
        if ns.hash_check:
            status = 0
            for script in (HARNESS/"hashes_r11.ps1", COLLECTOR/"hashes_r11.ps1", COLLECTOR/"hashes_evidence_r11.ps1"):
                ps = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script), "-Verify"], cwd=ROOT, capture_output=True, text=True)
                sys.stdout.write(ps.stdout); sys.stderr.write(ps.stderr)
                status = status or ps.returncode
            return status
    except (PipelineError, OSError, ValueError) as exc:
        print(f"PIPELINE_R11_ERROR {type(exc).__name__}: {exc}", file=sys.stderr)
        return 2
    return 2


if __name__ == "__main__":
    raise SystemExit(main())



