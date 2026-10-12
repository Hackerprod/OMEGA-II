#!/usr/bin/env python3
"""Deriva y valida la telemetría de bloques EMON para harness r9."""
from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import math
import os
import re
import statistics
import sys
from collections import defaultdict
from pathlib import Path

QPF_DEFAULT = 10_000_000
MASK32 = (1 << 32) - 1
CHANNELS = {
    "thread": "CPU_CLK_UNHALTED.THREAD",
    "ref": "CPU_CLK_UNHALTED.REF_TSC",
    "l2m": "L2_RQSTS.DEMAND_DATA_RD_MISS",
    "l2d": "L2_RQSTS.ALL_DEMAND_DATA_RD",
    "llcm": "OFFCORE_REQUESTS.L3_MISS_DEMAND_DATA_RD",
    "llcd": "OFFCORE_REQUESTS.DEMAND_DATA_RD",
    "energy_unit": "MSR_EVENT:msr=0x606:type=STATIC:scope=PACKAGE",
    "energy": "MSR_EVENT:msr=0x611:type=FREERUN:scope=PACKAGE",
    "thermal_core": "MSR_EVENT:msr=0x19C:type=STATIC:scope=THREAD",
    "thermal_pkg": "MSR_EVENT:msr=0x1B1:type=STATIC:scope=PACKAGE",
}


class DuplicateKey(ValueError):
    pass


def strict_object(pairs):
    out = {}
    for key, value in pairs:
        if key in out:
            raise DuplicateKey(f"clave JSON duplicada: {key}")
        out[key] = value
    return out


def strict_json(line: str):
    return json.loads(
        line,
        object_pairs_hook=strict_object,
        parse_constant=lambda value: (_ for _ in ()).throw(ValueError(f"número no finito: {value}")),
    )


def _number(s: str):
    s = s.strip()
    if not re.fullmatch(r"[+-]?\d[\d,]*(?:\.\d+)?", s):
        return None
    return float(s.replace(",", ""))


def _fields(line: str):
    parts = line.split("\t")
    return parts[0].strip(), [_number(v) for v in parts[1:]]


def _filetime(stamp: str):
    local = dt.datetime.strptime(stamp, "%m/%d/%Y %H:%M:%S.%f").astimezone()
    utc = local.astimezone(dt.timezone.utc)
    epoch = dt.datetime(1601, 1, 1, tzinfo=dt.timezone.utc)
    return (utc - epoch).total_seconds() * 10_000_000


def _fit_clock_map(path: Path):
    with path.open("r", newline="", encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))
    points = [(float(r["filetime_100ns"]), (float(r["qpc_pre"]) + float(r["qpc_post"])) / 2) for r in rows if r.get("filetime_100ns") not in (None, "")]
    if len(points) < 3:
        raise ValueError("clock_map requiere BEGIN, al menos un MAP y END")
    mx = sum(x for x, _ in points) / len(points)
    my = sum(y for _, y in points) / len(points)
    den = sum((x - mx) ** 2 for x, _ in points)
    if den <= 0:
        raise ValueError("clock_map no tiene intervalo temporal")
    slope = sum((x - mx) * (y - my) for x, y in points) / den
    intercept = my - slope * mx
    declared_qpf = {int(r["qpc_frequency"]) for r in rows if r.get("qpc_frequency") not in (None, "")}
    if len(declared_qpf) > 1:
        raise ValueError("QPC_frequency cambia dentro de la captura")
    qpf = declared_qpf.pop() if declared_qpf else QPF_DEFAULT
    if qpf <= 0:
        raise ValueError("QPC_frequency inválida")
    return lambda stamp: intercept + slope * _filetime(stamp), qpf


def _read_emon(path: Path, map_time, f_report_hz: float):
    raw = path.read_text(encoding="utf-8", errors="replace")
    chunks = re.split(r"(?m)^==========\s*$", raw)
    records = []
    for chunk in chunks:
        stamp_match = re.search(r"(?m)^(\d{1,2}/\d{1,2}/\d{4}\s+\d{2}:\d{2}:\d{2}\.\d{3})\s*$", chunk)
        if not stamp_match:
            continue
        vals = {}
        for line in chunk.splitlines():
            if "\t" not in line:
                continue
            name, vector = _fields(line)
            for key, channel in CHANNELS.items():
                if name == channel:
                    vals[key] = vector
        if any(k not in vals for k in CHANNELS):
            continue
        if any(v is None for v in vals["thread"][:1]) or vals["thread"][0] <= 0:
            continue
        end = map_time(stamp_match.group(1))
        span = vals["thread"][0] / f_report_hz
        vals["start"] = end - span * QPF_DEFAULT
        vals["end"] = end
        vals["span_s"] = span
        records.append(vals)
    if not records:
        raise ValueError("EMON sin grupos completos con todos los canales requeridos")
    return records, raw


def _check(metrics: dict, reported_hz: float):
    reasons = []
    observed = metrics.get("f_tsc_hz")
    if not _finite_positive(observed) or not _finite_positive(reported_hz):
        reasons.append("F_TSC_QPC_UNAVAILABLE")
    elif abs(observed / reported_hz - 1.0) > 0.03:
        reasons.append("F_TSC_QPC_DEVIATION_GT_3_PERCENT")
    if metrics.get("thermal_current"):
        reasons.append("THERMAL_CURRENT_ACTIVE")
    if not metrics.get("thermal_core_seen") or not metrics.get("thermal_pkg_seen"):
        reasons.append("THERMAL_STATUS_MISSING")
    if metrics.get("tcc_margin_c") is None or metrics["tcc_margin_c"] < 5:
        reasons.append("TCC_MARGIN_LT_5C_OR_MISSING")
    if not _finite_positive(metrics.get("pkg_power_w")):
        reasons.append("PKG_POWER_NOT_ESTIMABLE")
    if not _finite_positive(metrics.get("f_active_hz")):
        reasons.append("F_ACTIVE_NOT_ESTIMABLE")
    if metrics.get("l2_den", 0) <= 0:
        reasons.append("L2_NOT_ESTIMABLE_DENOMINATOR_ZERO")
    if metrics.get("llc_den", 0) <= 0:
        reasons.append("LLC_NOT_ESTIMABLE_DENOMINATOR_ZERO")
    return reasons


def _finite_positive(x):
    return isinstance(x, (int, float)) and math.isfinite(x) and x > 0


def _derive_block(rows, telemetry, qpf, f_report_hz):
    starts = [int(r["t_ini_qpc"]) for r in rows]
    ends = [int(r["t_fin_qpc"]) for r in rows]
    a, b = max(starts), min(ends)
    if b <= a:
        raise ValueError("los 8 workers no comparten intervalo de medición")
    selected = [r for r in telemetry if r["start"] >= a and r["end"] <= b]
    if not selected:
        raise ValueError("no hay grupos EMON completos dentro del bloque")
    qpc_s = (b - a) / qpf
    tsc_rates = []
    for r in rows:
        qpc_dt = (int(r["qpc_end"]) - int(r["qpc_start"])) / qpf
        if qpc_dt <= 0:
            continue
        tsc_rates.append((int(r["tsc_end"]) - int(r["tsc_start"])) / qpc_dt)
    if len(tsc_rates) != len(rows):
        raise ValueError("f_TSC no estimable para todos los workers")
    f_tsc = statistics.median(tsc_rates)
    energy_unit_raw = selected[0]["energy_unit"]
    if not energy_unit_raw or len(energy_unit_raw) < 2 or energy_unit_raw[1] is None:
        raise ValueError("0x606 sin unidad de energía")
    energy_unit_reg = int(energy_unit_raw[1])
    exponent = (energy_unit_reg >> 8) & 0x1F
    u_e = 2.0 ** (-exponent)
    energy_counts = sum(int(r["energy"][1]) for r in selected if len(r["energy"]) > 1 and r["energy"][1] is not None)
    span_s = sum(r["span_s"] for r in selected)
    pkg_power = (u_e * energy_counts / span_s) if span_s > 0 else None

    thread_sums = [0] * 8
    ref_sums = [0] * 8
    l2m = l2d = llcm = llcd = 0
    thermal_current = False
    thermal_history = False
    thermal_core_seen = thermal_pkg_seen = False
    margins = []
    for rec in selected:
        for core in range(8):
            pair = (2 * core, 2 * core + 1)
            for lp in pair:
                ix = lp + 1
                thread_sums[core] += int(rec["thread"][ix] or 0)
                ref_sums[core] += int(rec["ref"][ix] or 0)
                l2m += int(rec["l2m"][ix] or 0)
                l2d += int(rec["l2d"][ix] or 0)
                llcm += int(rec["llcm"][ix] or 0)
                llcd += int(rec["llcd"][ix] or 0)
                therm = rec["thermal_core"][ix] if ix < len(rec["thermal_core"]) else None
                if therm is not None:
                    raw = int(therm)
                    thermal_core_seen = True
                    thermal_current |= bool(raw & 1)
                    thermal_history |= bool(raw & 2)
                    margins.append((raw >> 16) & 0x7F)
            pkg = rec["thermal_pkg"][1] if len(rec["thermal_pkg"]) > 1 else None
            if pkg is not None:
                raw_pkg = int(pkg)
                thermal_pkg_seen = True
                thermal_current |= bool(raw_pkg & 1)
                thermal_history |= bool(raw_pkg & 2)
                margins.append((raw_pkg >> 16) & 0x7F)
    active_hz = [f_tsc * t / ref if ref > 0 else None for t, ref in zip(thread_sums, ref_sums)]
    wall_hz = [t / qpc_s if qpc_s > 0 else None for t in thread_sums]
    activity = [ref / (f_tsc * qpc_s) if f_tsc > 0 and qpc_s > 0 else None for ref in ref_sums]
    metrics = {
        "f_tsc_hz": f_tsc,
        "f_tsc_reported_hz": f_report_hz,
        "pkg_power_w": pkg_power,
        "f_active_hz": min((v for v in active_hz if v is not None), default=None),
        "f_active_hz_by_pcore": active_hz,
        "activity_by_pcore": activity,
        "f_wall_hz_by_pcore": wall_hz,
        "l2_num": l2m,
        "l2_den": l2d,
        "l2_fraction": l2m / l2d if l2d else None,
        "llc_num": llcm,
        "llc_den": llcd,
        "llc_fraction": llcm / llcd if llcd else None,
        "thermal_current": thermal_current,
        "thermal_history": thermal_history,
        "thermal_core_seen": thermal_core_seen,
        "thermal_pkg_seen": thermal_pkg_seen,
        "tcc_margin_c": min(margins) if margins else None,
        "u_e_j": u_e,
        "energy_delta_counts": energy_counts,
        "emon_groups": len(selected),
        "block_span_s": qpc_s,
    }
    reasons = _check(metrics, f_report_hz)
    for i, hz in enumerate(active_hz):
        if not _finite_positive(hz):
            reasons.append(f"F_ACTIVE_PCORE_{i}_NOT_ESTIMABLE")
    metrics["valid"] = not reasons
    metrics["reasons"] = sorted(set(reasons))
    return metrics


def derive(capture: Path):
    raw_path = capture / "raw.jsonl"
    summary_path = capture / "summary.jsonl"
    if not summary_path.is_file():
        summary_path = capture / "summary.json"  # Compatibility for earlier R9 captures.
    emon_path = capture / "emon_stdout.txt"
    if not all(p.is_file() for p in (raw_path, summary_path, emon_path, capture / "clock_map.csv")):
        raise ValueError("captura requiere raw.jsonl, summary.jsonl, emon_stdout.txt y clock_map.csv")
    map_time, qpf = _fit_clock_map(capture / "clock_map.csv")
    emon_raw = emon_path.read_text(encoding="utf-8", errors="replace")
    freq_match = re.search(r"TSC Freq[^\r\n]*?(\d+(?:\.\d+)?)\s+MHz", emon_raw)
    if not freq_match:
        raise ValueError("EMON no informa TSC Freq; no se cualifica contra QPC")
    f_report_hz = float(freq_match.group(1)) * 1_000_000
    records, _ = _read_emon(emon_path, map_time, f_report_hz)
    with raw_path.open("r", encoding="utf-8-sig") as f:
        samples = [strict_json(line) for line in f if line.strip()]
    grouped = defaultdict(list)
    for sample in samples:
        grouped[(sample["provider"], sample["cell"], int(sample["replica"]))].append(sample)
    block_metrics = {}
    for key, rows in grouped.items():
        rows.sort(key=lambda r: int(r["worker"]))
        try:
            metric = _derive_block(rows, records, qpf, f_report_hz)
        except (KeyError, TypeError, ValueError, IndexError) as exc:
            metric = {"valid": False, "reasons": ["DERIVATION_ERROR:" + str(exc)]}
        block_metrics[key] = metric
        for row in rows:
            pcore = int(row["lp"]) // 2
            row["execution_status"] = row.get("status", "UNKNOWN")
            row["telemetry_status"] = "VALID" if metric["valid"] else "INVALID_TELEMETRY"
            row["telemetry_reason"] = metric["reasons"]
            row["telemetry"] = row["telemetry_status"]
            row["status"] = row["telemetry_status"] if row.get("execution_status") in ("SMOKE_TIMED", "PASS") else row.get("execution_status")
            row["telemetry_detail"] = {k: v for k, v in metric.items() if k not in ("valid", "reasons")}
            if "f_active_hz_by_pcore" in metric:
                row["telemetry_detail"]["pcore"] = pcore
                row["telemetry_detail"]["f_active_hz"] = metric["f_active_hz_by_pcore"][pcore]
                row["telemetry_detail"]["activity_fraction"] = metric["activity_by_pcore"][pcore]
                row["telemetry_detail"]["f_wall_hz"] = metric["f_wall_hz_by_pcore"][pcore]
    tmp = raw_path.with_suffix(".jsonl.tmp")
    with tmp.open("w", encoding="utf-8", newline="\n") as f:
        for row in samples:
            f.write(json.dumps(row, allow_nan=False, separators=(",", ":")) + "\n")
    os.replace(tmp, raw_path)
    summaries = []
    for line in summary_path.read_text(encoding="utf-8-sig").splitlines():
        if line.strip():
            try:
                summaries.append(strict_json(line))
            except json.JSONDecodeError:
                # R8 escribía tokens PASS/FAIL sin comillas; se admiten solo como entrada histórica.
                summaries.append(strict_json(re.sub(r'(?<=[\:,])\s*(PASS|FAIL)(?=\s*[,}])', r'"\1"', line)))
    for summary in summaries:
        key = summary.get("provider")
        summary_cell = summary.get("cell")
        matching = [v for (provider, cell, _rep), v in block_metrics.items()
                    if provider == key and (summary_cell is None or cell == summary_cell)]
        summary["telemetry_status"] = "VALID" if matching and all(m["valid"] for m in matching) else "INVALID_TELEMETRY"
        summary["telemetry"] = summary["telemetry_status"]
        summary["telemetry_reason"] = sorted({reason for m in matching for reason in m.get("reasons", [])})
        summary["telemetry_blocks"] = len(matching)
    with summary_path.open("w", encoding="utf-8", newline="\n") as f:
        for summary in summaries:
            f.write(json.dumps(summary, allow_nan=False, separators=(",", ":")) + "\n")
    report = capture / "telemetry_r9.txt"
    with report.open("w", encoding="utf-8", newline="\n") as f:
        for key, metric in block_metrics.items():
            provider, cell, replica = key
            f.write(f"BLOCK provider={provider} cell={cell} replica={replica} status={'VALID' if metric['valid'] else 'INVALID_TELEMETRY'} reasons={','.join(metric.get('reasons', []))}\n")
            if "f_active_hz_by_pcore" in metric:
                for i, hz in enumerate(metric["f_active_hz_by_pcore"]):
                    f.write(f"PCORE={i} f_active_mhz={hz / 1e6:.3f} a={metric['activity_by_pcore'][i]:.6f} f_wall_mhz={metric['f_wall_hz_by_pcore'][i] / 1e6:.3f}\n")
                f.write(f"P_pkg_w={metric['pkg_power_w']:.3f} u_E_j={metric['u_e_j']:.12g} dE_counts={metric['energy_delta_counts']} TCC_margin_c={metric['tcc_margin_c']} thermal_current={metric['thermal_current']} thermal_history={metric['thermal_history']} L2={metric['l2_fraction']} LLC={metric['llc_fraction']} EMON_groups={metric['emon_groups']}\n")
            else:
                f.write("METRICS_UNAVAILABLE\n")
    for line in report.read_text(encoding="utf-8").splitlines():
        print(line)
    valid = sum(bool(m["valid"]) for m in block_metrics.values())
    print(f"TELEMETRY_R9 blocks={len(block_metrics)} valid={valid} invalid={len(block_metrics)-valid} f_TSC_QPC_vs_EMON=3pct")
    return 0 if valid == len(block_metrics) else 4


def self_test():
    base = {
        "f_tsc_hz": 2.112e9,
        "pkg_power_w": 90.0,
        "f_active_hz": 2.3e9,
        "thermal_current": False,
        "thermal_core_seen": True,
        "thermal_pkg_seen": True,
        "tcc_margin_c": 8,
        "l2_den": 100,
        "llc_den": 100,
    }
    assert not _check(base, 2.112e9), "fixture base debe ser VALID"
    print("TELEMETRY_QA valid_fixture=PASS")
    cases = [
        ("thermal_current", {"thermal_current": True}),
        ("f_tsc_outside_3pct", {"f_tsc_hz": 2.20e9}),
        ("tcc_margin_below_5c", {"tcc_margin_c": 4}),
        ("l2_denominator_zero", {"l2_den": 0}),
        ("llc_denominator_zero", {"llc_den": 0}),
        ("pkg_power_missing", {"pkg_power_w": None}),
    ]
    for name, change in cases:
        metrics = dict(base)
        metrics.update(change)
        reasons = _check(metrics, 2.112e9)
        assert reasons, f"{name} no se rechazó"
        print(f"NEGATIVE telemetry={name} rejected=PASS reason={reasons[0]}")
    print("TELEMETRY_QA result=PASS")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--derive", action="store_true")
    ap.add_argument("--capture", type=Path)
    ns = ap.parse_args()
    if ns.self_test:
        self_test()
        return 0
    if ns.derive and ns.capture:
        return derive(ns.capture.resolve())
    ap.error("use --self-test o --derive --capture DIR")


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"TELEMETRY_R9_ERROR {type(exc).__name__}: {exc}", file=sys.stderr)
        sys.exit(2)
