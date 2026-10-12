#!/usr/bin/env python3
"""Strict R9 JSONL validator; this is the schema boundary for future derivation."""
from __future__ import annotations
import argparse
import json
import math
import sys
from pathlib import Path


class SchemaError(ValueError):
    pass


def pairs_unique(pairs):
    out = {}
    for key, value in pairs:
        if key in out:
            raise SchemaError(f"clave duplicada: {key}")
        out[key] = value
    return out


def reject_constant(value):
    raise SchemaError(f"número no finito: {value}")


def strict_loads(line):
    try:
        value = json.loads(line, object_pairs_hook=pairs_unique, parse_constant=reject_constant)
    except (json.JSONDecodeError, SchemaError) as exc:
        raise SchemaError(str(exc)) from exc
    def finite(v):
        if isinstance(v, float) and not math.isfinite(v):
            raise SchemaError("número no finito")
        if isinstance(v, dict):
            for x in v.values(): finite(x)
        elif isinstance(v, list):
            for x in v: finite(x)
    finite(value)
    return value


SMOKE_RAW = set("label provider plan_event plan_index cell m replica worker lp N R R_common t_app_us t_pad_ini_qpc t_ini_qpc t_fin_qpc t_pad_fin_qpc tsc_start tsc_end qpc_start qpc_end large_pages affinity telemetry status".split())
DERIVED = set("execution_status telemetry_status telemetry_reason telemetry_detail telemetry".split())
DERIVED_SUMMARY = set("telemetry telemetry_status telemetry_reason telemetry_blocks".split())
GEOMETRY_RAW = set("label provider plan_event plan_index cell O m replica worker lp N R R_common t_app_us t_pad_ini_qpc t_ini_qpc t_fin_qpc t_pad_fin_qpc tsc_start tsc_end qpc_start qpc_end large_pages affinity numerical page status".split())
SMOKE_SUMMARY = set("provider cell N R_common median_us IQR_us MAD_us min_us max_us qpc_crosscheck telemetry label telemetry_status telemetry_reason telemetry_blocks".split())
GEOMETRY_SUMMARY = set("provider plan_event plan_index cell O m N R_common median_us IQR_us MAD_us min_us max_us numerical large_pages status".split())


def check_int(row, key, minimum=0):
    v = row.get(key)
    if isinstance(v, bool) or not isinstance(v, int) or v < minimum:
        raise SchemaError(f"{key}: entero inválido o ausente")


def check_number_or_null(row, key):
    v = row.get(key)
    if v is not None and (isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v)):
        raise SchemaError(f"{key}: número finito/null esperado")


def exact_keys(row, expected, line_no):
    if not isinstance(row, dict):
        raise SchemaError(f"línea {line_no}: objeto esperado")
    missing, extra = expected-row.keys(), row.keys()-expected
    if missing or extra:
        raise SchemaError(f"línea {line_no}: ausentes={sorted(missing)} extra={sorted(extra)}")


def validate_row(row, mode, line_no):
    if mode == "raw":
        label = row.get("label")
        if label == "SMOKE":
            keys = SMOKE_RAW | (row.keys() & DERIVED)
            exact_keys(row, keys, line_no)
            check_int(row, "m", 1); check_int(row, "replica"); check_int(row, "worker"); check_int(row, "lp"); check_int(row, "N", 1); check_int(row, "R", 1); check_int(row, "R_common", 1); check_int(row, "plan_index")
            for k in ("t_app_us", "t_pad_ini_qpc", "t_ini_qpc", "t_fin_qpc", "t_pad_fin_qpc", "tsc_start", "tsc_end", "qpc_start", "qpc_end"): check_number_or_null(row, k)
            if not isinstance(row["large_pages"], bool) or row["affinity"] not in ("PASS", "FAIL"): raise SchemaError(f"línea {line_no}: gate inválido")
            if "telemetry_reason" in row and not isinstance(row["telemetry_reason"], list): raise SchemaError(f"línea {line_no}: telemetry_reason debe ser array")
        elif label in ("GEOMETRY_SMOKE", "OFFICIAL"):
            exact_keys(row, GEOMETRY_RAW | (row.keys() & DERIVED), line_no)
            for k in ("O", "m", "N", "R", "R_common"): check_int(row,k,1)
            check_int(row,"plan_index")
            for k in ("replica", "worker", "lp"): check_int(row,k)
            for k in ("t_app_us", "t_pad_ini_qpc", "t_ini_qpc", "t_fin_qpc", "t_pad_fin_qpc", "tsc_start", "tsc_end", "qpc_start", "qpc_end"): check_number_or_null(row,k)
            if not isinstance(row["large_pages"], bool): raise SchemaError(f"línea {line_no}: large_pages debe ser booleano")
            for k in ("affinity", "numerical", "page", "status"):
                if row[k] not in ("PASS", "FAIL"): raise SchemaError(f"línea {line_no}: {k} inválido")
        else: raise SchemaError(f"línea {line_no}: label raw desconocido")
    else:
        label = row.get("label")
        if label == "SMOKE":
            expected = SMOKE_SUMMARY
            if row.keys() == (expected-{"telemetry_status","telemetry_reason","telemetry_blocks"}): expected = expected-{"telemetry_status","telemetry_reason","telemetry_blocks"}
            exact_keys(row, expected, line_no)
            for k in ("N", "R_common"): check_int(row,k,1)
            for k in ("median_us","IQR_us","MAD_us","min_us","max_us"): check_number_or_null(row,k)
        else:
            exact_keys(row, GEOMETRY_SUMMARY | (row.keys() & DERIVED_SUMMARY), line_no)
            for k in ("O","m","N","R_common"): check_int(row,k,1)
            check_int(row,"plan_index")
            for k in ("median_us","IQR_us","MAD_us","min_us","max_us"): check_number_or_null(row,k)


def validate(path, mode):
    rows = 0
    with Path(path).open("r", encoding="utf-8-sig") as f:
        for n, line in enumerate(f, 1):
            if not line.strip(): raise SchemaError(f"línea vacía: {n}")
            row = strict_loads(line)
            validate_row(row, mode, n)
            rows += 1
    if rows == 0: raise SchemaError("JSONL vacío")
    return rows


def self_test():
    cases = [("duplicate", '{"x":1,"x":2}'), ("nan", '{"x":NaN}'), ("infinity", '{"x":1e9999}'), ("trailing", '{"x":1} xyz')]
    for name, data in cases:
        try: strict_loads(data)
        except SchemaError: print(f"STRICT_JSON case={name} rejected=PASS")
        else: raise SchemaError(f"{name} aceptado")
    try: validate_row({"label":"GEOMETRY_SMOKE"}, "raw", 1)
    except SchemaError: print("STRICT_JSON case=missing_fields rejected=PASS")
    else: raise SchemaError("faltantes aceptados")
    print("HARNESS_SCHEMA_SELF_TEST PASS")


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--raw")
    ap.add_argument("--summary")
    ap.add_argument("--self-test",action="store_true")
    a=ap.parse_args()
    try:
        if a.self_test: self_test(); return 0
        if bool(a.raw)==bool(a.summary): ap.error("indique exactamente --raw o --summary")
        mode="raw" if a.raw else "summary"; count=validate(a.raw or a.summary,mode)
        print(f"HARNESS_SCHEMA file={Path(a.raw or a.summary).name} kind={mode} rows={count} status=PASS")
        return 0
    except (OSError,SchemaError) as exc:
        print(f"HARNESS_SCHEMA REJECT kind={('raw' if a.raw else 'summary')} reason={exc}",file=sys.stderr)
        return 3


if __name__ == "__main__": raise SystemExit(main())
