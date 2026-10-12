#!/usr/bin/env python3
"""Strict S-20 validator for Intel EMON CLI captures.

The parser treats EMON counts as immutable raw interval deltas. It never trusts
state labels in an input file and never fills absent clocks or channels.
"""
from __future__ import annotations

import argparse
import csv
import datetime as dt
import hashlib
import json
import math
import re
import statistics
import sys
from pathlib import Path
from typing import Any, Iterable


class ValidationInputError(ValueError):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


EVENT_THREAD = "CPU_CLK_UNHALTED.THREAD"
EVENT_REF = "CPU_CLK_UNHALTED.REF_TSC"
EVENT_L2_MISS = "L2_RQSTS.DEMAND_DATA_RD_MISS"
EVENT_L2_DEMAND = "L2_RQSTS.ALL_DEMAND_DATA_RD"
EVENT_LLC_MISS = "OFFCORE_REQUESTS.L3_MISS_DEMAND_DATA_RD"
EVENT_LLC_DEMAND = "OFFCORE_REQUESTS.DEMAND_DATA_RD"
EVENT_INST = "INST_RETIRED.ANY"
EVENT_606 = "MSR_EVENT:msr=0x606:type=STATIC:scope=PACKAGE"
EVENT_611 = "MSR_EVENT:msr=0x611:type=FREERUN:scope=PACKAGE"
EVENT_19C = "MSR_EVENT:msr=0x19C:type=STATIC:scope=THREAD"
EVENT_1B1 = "MSR_EVENT:msr=0x1B1:type=STATIC:scope=PACKAGE"

DEFAULT_EVENT_GROUP = [
    EVENT_606,
    EVENT_611,
    EVENT_19C,
    EVENT_1B1,
    EVENT_THREAD,
    EVENT_REF,
    EVENT_INST,
    EVENT_L2_MISS,
    EVENT_L2_DEMAND,
    EVENT_LLC_MISS,
    EVENT_LLC_DEMAND,
]
CPU_EVENTS = {
    EVENT_THREAD,
    EVENT_REF,
    EVENT_INST,
    EVENT_L2_MISS,
    EVENT_L2_DEMAND,
    EVENT_LLC_MISS,
    EVENT_LLC_DEMAND,
}
THREAD_MSR_EVENTS = {EVENT_19C}
PACKAGE_MSR_EVENTS = {EVENT_606, EVENT_611, EVENT_1B1}
TIMESTAMP_RE = re.compile(r"^(\d{1,2}/\d{1,2}/\d{4})\s+(\d{2}:\d{2}:\d{2}\.\d{3})\s*$")
COUNT_RE = re.compile(r"^(?:\d[\d,]*)(?:\.\d+)?$")
REASONS = {
    "QPF_MISSING", "QPF_MISMATCH", "TSC_QPC_DISAGREEMENT", "CLOCK_MAP_UNCERTAINTY",
    "MULTIPLEXING_CONFIGURED", "EVENT_GROUPS_MULTIPLE", "PERIODS_LOST", "EVENT_SET_MISMATCH",
    "EVENT_MISSING", "EVENT_DUPLICATE", "WINDOW_DUPLICATE", "TIME_ORDER_INVALID",
    "LP_MAP_MISMATCH", "SMT_SCOPE_MIXED", "CROSS_CONDITION", "WINDOW_TOO_LONG",
    "WINDOW_COUNT_LOW", "COVERAGE_LOW", "GAP_TOO_LARGE", "PADDING_MARGIN_LOW", "FREQ_WINDOW_EXCURSION",
    "FREQ_CORE_DISPERSION", "FREQ_REPLICA_DRIFT", "FREQ_CONDITION_MISMATCH",
    "THREAD_REF_DENOM_ZERO", "CACHE_DENOM_ZERO", "ENERGY_VALUE_INVALID", "ENERGY_SNAPSHOT_FIRST",
    "THERMAL_READING_INVALID", "THERMAL_MARGIN_LOW", "THERMAL_CURRENT_ACTIVE",
    "PROCHOT_CURRENT", "PROCHOT_HISTORICAL_UNCLEARED", "THERMAL_HISTORY_UNCLEARED",
    "POWER_LIMIT_OBSERVED", "CHANNEL_VALUE_MISSING", "CONTROL_PEERS_MISSING",
}


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    out: dict[str, Any] = {}
    for key, value in pairs:
        if key in out:
            raise ValidationInputError("JSON_DUPLICATE_KEY", f"clave JSON duplicada: {key}")
        out[key] = value
    return out


def load_config(value: dict[str, Any] | str | Path) -> dict[str, Any]:
    if isinstance(value, dict):
        config = value
    else:
        try:
            config = json.loads(Path(value).read_text(encoding="utf-8-sig"), object_pairs_hook=_unique_object,
                                parse_constant=lambda v: (_ for _ in ()).throw(ValueError(f"no finito: {v}")))
        except ValidationInputError:
            raise
        except Exception as exc:
            raise ValidationInputError("CONFIG_PARSE_ERROR", f"configuración inválida: {exc}") from exc
    if not isinstance(config, dict):
        raise ValidationInputError("CONFIG_TYPE", "la configuración debe ser un objeto JSON")
    required = {
        "schema_version", "qpf_hz", "qpf_source", "f_tsc_qualified_hz", "f_tsc_qpc_hz",
        "f_tsc_configured_hz", "emon_local_utc_offset_minutes", "nominal_window_s", "lp_map",
        "event_groups", "multiplexing", "periods_lost", "energy_event", "energy_semantics",
        "energy_unit_joule", "tcc_target_c", "thermal_history_clear_at_start", "mode",
        "logical_processor_count", "profile", "event_map_source",
        "f_tsc_qualification_qpf_hz",
    }
    missing = sorted(required - config.keys())
    optional = {"collection_mode", "monitored_interval_count", "reset_count", "clock_map_path", "expected_replicas", "event_map_sha256", "marks_clock_domain", "edp_path", "edp_sha256", "n_loop"}
    unknown = sorted(config.keys() - required - optional)
    if unknown:
        raise ValidationInputError("CONFIG_UNKNOWN_FIELD", f"campos no admitidos: {', '.join(unknown)}")
    if missing:
        code = "QPF_MISSING" if any(k.startswith("qpf_") for k in missing) or "qpf_hz" in missing else "CONFIG_FIELD_MISSING"
        raise ValidationInputError(code, f"faltan campos de configuración: {', '.join(missing)}")
    if not isinstance(config.get("schema_version"), str) or config.get("schema_version") != "S20-R2-v1":
        raise ValidationInputError("CONFIG_VERSION", "schema_version debe ser S20-R2-v1")
    if not isinstance(config.get("profile"), str) or config["profile"] not in {"s20", "r1"}:
        raise ValidationInputError("CONFIG_PROFILE", "profile debe ser s20 o r1")
    if not isinstance(config.get("mode"), str) or config["mode"] not in {"temporal", "full", "r1"}:
        raise ValidationInputError("MODE_INVALID", "mode debe ser temporal, full o r1")
    if not isinstance(config["thermal_history_clear_at_start"], bool):
        raise ValidationInputError("CONFIG_FIELD_INVALID", "thermal_history_clear_at_start debe ser booleano")
    for key in ("qpf_hz", "f_tsc_qualified_hz", "f_tsc_qpc_hz", "f_tsc_configured_hz", "f_tsc_qualification_qpf_hz", "nominal_window_s", "energy_unit_joule", "tcc_target_c"):
        val = config.get(key)
        if isinstance(val, bool) or not isinstance(val, (int, float)) or not math.isfinite(float(val)) or float(val) <= 0:
            if key == "qpf_hz":
                raise ValidationInputError("QPF_MISSING", "QPF debe leerse y registrarse; no se admite valor por defecto")
            raise ValidationInputError("CONFIG_NUMBER_INVALID", f"{key} debe ser un número finito positivo")
    if not isinstance(config["qpf_source"], str) or not config["qpf_source"].strip():
        raise ValidationInputError("QPF_MISSING", "qpf_source debe identificar la lectura/registro de QPF")
    if abs(float(config["f_tsc_qualification_qpf_hz"]) - float(config["qpf_hz"])) / float(config["qpf_hz"]) > 0.001:
        raise ValidationInputError("QPF_MISMATCH", "QPF usada al cualificar f_TSC no concuerda con el QPF registrado")
    if not isinstance(config["event_map_source"], str) or not config["event_map_source"].strip():
        raise ValidationInputError("EVENT_MAP_MISSING", "se debe registrar la procedencia del mapa de eventos")
    if config["nominal_window_s"] != 0.05:
        raise ValidationInputError("NOMINAL_WINDOW_INVALID", "la ventana nominal de S-20 debe ser 50 ms")
    if config["multiplexing"] is not False:
        raise ValidationInputError("MULTIPLEXING_CONFIGURED", "la colección conjunta debe declarar multiplexing=false")
    if isinstance(config["periods_lost"], bool) or not isinstance(config["periods_lost"], int) or config["periods_lost"] != 0:
        raise ValidationInputError("PERIODS_LOST", "periods_lost debe ser el entero registrado 0")
    groups = config["event_groups"]
    if not isinstance(groups, list) or len(groups) != 1 or not isinstance(groups[0], list):
        raise ValidationInputError("EVENT_GROUPS_MULTIPLE", "S-20 exige un solo grupo efectivo de eventos")
    if any(not isinstance(event, str) or not event for event in groups[0]):
        raise ValidationInputError("EVENT_SET_INVALID", "cada evento debe ser un nombre no vacío")
    if len(groups[0]) != len(set(groups[0])):
        raise ValidationInputError("EVENT_DUPLICATE", "el grupo efectivo contiene eventos duplicados")
    if config["profile"] not in {"s20", "r1"}:
        raise ValidationInputError("CONFIG_PROFILE", "profile debe ser s20 o r1")
    if config["profile"] == "s20":
        if not {"edp_path", "edp_sha256"}.issubset(config):
            raise ValidationInputError("CONFIG_FIELD_MISSING", "S-20 requiere edp_path y edp_sha256 efectivos")
        if not isinstance(config["edp_sha256"], str) or not re.fullmatch(r"[0-9a-f]{64}", config["edp_sha256"]):
            raise ValidationInputError("CONFIG_HASH_INVALID", "edp_sha256 debe ser SHA256 hexadecimal en minúsculas")
        if not isinstance(config["edp_path"], str) or not config["edp_path"].strip():
            raise ValidationInputError("EVENT_MAP_MISSING", "se debe registrar la ruta EDP efectiva")
    expected_events = {EVENT_INST} if config["profile"] == "r1" else set(DEFAULT_EVENT_GROUP)
    if not expected_events.issubset(set(groups[0])):
        raise ValidationInputError("EVENT_SET_MISMATCH", "faltan canales obligatorios en el grupo efectivo")
    if isinstance(config["logical_processor_count"], bool) or not isinstance(config["logical_processor_count"], int) or config["logical_processor_count"] != 24:
        raise ValidationInputError("LP_MAP_INVALID", "logical_processor_count debe coincidir con los 24 LP EMON registrados")
    if config["energy_event"] != EVENT_611:
        raise ValidationInputError("ENERGY_EVENT_INVALID", "el canal de energía debe ser 0x611 INTERVAL_DELTA o snapshot modular")
    if not isinstance(config["energy_semantics"], str) or config["energy_semantics"] not in {"INTERVAL_DELTA", "SNAPSHOT32"}:
        raise ValidationInputError("ENERGY_SEMANTICS_INVALID", "0x611 debe declarar INTERVAL_DELTA o SNAPSHOT32")
    if "expected_replicas" in config and (isinstance(config["expected_replicas"], bool) or
            not isinstance(config["expected_replicas"], int) or config["expected_replicas"] < 2):
        raise ValidationInputError("CONFIG_FIELD_INVALID", "expected_replicas debe ser entero >=2")
    if "marks_clock_domain" in config and (not isinstance(config["marks_clock_domain"], str) or
            config["marks_clock_domain"] not in {"qpc_ticks", "qpc_seconds"}):
        raise ValidationInputError("CONFIG_FIELD_INVALID", "marks_clock_domain debe ser qpc_ticks o qpc_seconds")
    if "clock_map_path" in config and (not isinstance(config["clock_map_path"], str) or not config["clock_map_path"].strip()):
        raise ValidationInputError("CONFIG_FIELD_INVALID", "clock_map_path debe ser ruta de texto")
    if "event_map_sha256" in config and (not isinstance(config["event_map_sha256"], str) or
            not re.fullmatch(r"[0-9a-f]{64}", config["event_map_sha256"])):
        raise ValidationInputError("CONFIG_HASH_INVALID", "event_map_sha256 debe ser SHA256 hexadecimal")
    if not isinstance(config["lp_map"], list) or len(config["lp_map"]) != 8:
        raise ValidationInputError("LP_MAP_INVALID", "lp_map debe contener los ocho P-core seleccionados")
    seen_core: set[int] = set()
    seen_lp: set[int] = set()
    seen_core_id: set[int] = set()
    for row in config["lp_map"]:
        if not isinstance(row, dict) or not {"p_core", "core_id", "core_type", "worker_lp", "smt_siblings"}.issubset(row):
            raise ValidationInputError("LP_MAP_INVALID", "cada lp_map requiere p_core, core_id, worker_lp y smt_siblings")
        core, lp, core_id = row["p_core"], row["worker_lp"], row["core_id"]
        if (isinstance(core, bool) or not isinstance(core, int) or core in seen_core or
                isinstance(lp, bool) or not isinstance(lp, int) or lp in seen_lp or
                isinstance(core_id, bool) or not isinstance(core_id, int) or core_id in seen_core_id):
            raise ValidationInputError("LP_MAP_INVALID", "p_core y worker_lp deben ser enteros únicos")
        expected_lp = core * 2
        expected_siblings = [expected_lp, expected_lp + 1]
        if lp != expected_lp or row["smt_siblings"] != expected_siblings or core_id != core * 4:
            raise ValidationInputError("LP_MAP_INVALID", f"P{core} debe mapear a Core {core * 4}, LP {expected_lp}/{expected_lp + 1}")
        if row["core_type"] != "P":
            raise ValidationInputError("LP_MAP_INVALID", f"P{core} no está registrado como P-core")
        seen_core.add(core)
        seen_lp.add(lp)
        seen_core_id.add(core_id)
    if seen_core != set(range(8)) or seen_lp != {0, 2, 4, 6, 8, 10, 12, 14}:
        raise ValidationInputError("LP_MAP_INVALID", "el mapa gobernante es P0..P7 en LP 0,2,...,14")
    return config


def parse_count(text: str) -> int | None:
    text = text.strip()
    if text.upper() in {"N/A", "NA", "-", ""}:
        return None
    if not COUNT_RE.fullmatch(text):
        raise ValidationInputError("COUNT_PARSE_ERROR", f"contador no numérico: {text!r}")
    num = float(text.replace(",", ""))
    if not math.isfinite(num) or not num.is_integer() or num < 0:
        raise ValidationInputError("COUNT_PARSE_ERROR", f"contador debe ser entero finito no negativo: {text!r}")
    return int(num)


def _strict_csv_rows(lines: list[str], *, context: str, required: set[str], allowed: set[str]) -> list[dict[str, str]]:
    try:
        reader = csv.reader(lines, strict=True)
        header = next(reader, None)
        if not header or any(not name.strip() for name in header):
            raise ValidationInputError(f"{context}_HEADER", f"{context}: encabezado CSV vacío/inválido")
        header = [name.strip() for name in header]
        if len(header) != len(set(header)):
            raise ValidationInputError(f"{context}_HEADER", f"{context}: encabezado con columnas duplicadas")
        missing = required - set(header)
        unknown = set(header) - allowed
        if missing:
            raise ValidationInputError(f"{context}_HEADER", f"{context}: faltan columnas {', '.join(sorted(missing))}")
        if unknown:
            raise ValidationInputError(f"{context}_HEADER", f"{context}: columnas no admitidas {', '.join(sorted(unknown))}")
        rows = []
        for row_number, values in enumerate(reader, start=2):
            if not values or all(value == "" for value in values):
                continue
            if len(values) != len(header):
                raise ValidationInputError(f"{context}_ROW", f"{context}: fila {row_number} tiene {len(values)} columnas; esperaba {len(header)}")
            rows.append(dict(zip(header, values)))
        return rows
    except csv.Error as exc:
        raise ValidationInputError(f"{context}_CSV", f"{context}: CSV mal formado: {exc}") from exc


def parse_clock_map(path: str | Path, config: dict[str, Any]) -> dict[str, Any]:
    lines = Path(path).read_text(encoding="utf-8-sig").splitlines()
    header_index = next((i for i, line in enumerate(lines) if line.strip().startswith("sample,")), None)
    if header_index is None:
        raise ValidationInputError("CLOCK_MAP_HEADER", "clock map no contiene encabezado CSV")
    rows = _strict_csv_rows(lines[header_index:], context="CLOCK_MAP",
                            required={"sample", "qpc_pre", "qpc_post", "filetime_100ns"},
                            allowed={"sample", "qpc_pre", "qpc_post", "filetime_100ns", "sample_period_ms"})
    fit_rows = []
    named: dict[str, dict[str, float]] = {}
    for row_number, row in enumerate(rows, start=2):
        try:
            q0 = float(row["qpc_pre"])
            q1 = float(row["qpc_post"])
            filetime = float(row["filetime_100ns"])
        except (KeyError, TypeError, ValueError) as exc:
            raise ValidationInputError("CLOCK_MAP_INVALID", f"fila {row_number}: QPC/UTC no numéricos") from exc
        if (not all(math.isfinite(v) and v.is_integer() for v in (q0, q1, filetime)) or
                q0 < 0 or q1 < q0 or filetime <= 0):
            raise ValidationInputError("CLOCK_MAP_INVALID", f"fila {row_number}: valores QPC/UTC inválidos")
        sample = str(row.get("sample", ""))
        if not sample or sample in named:
            raise ValidationInputError("CLOCK_MAP_INVALID", f"fila {row_number}: sample vacío/duplicado")
        if not (sample.isdigit() or sample in {"BEGIN", "END"}):
            raise ValidationInputError("CLOCK_MAP_INVALID", f"fila {row_number}: sample debe ser contador, BEGIN o END")
        named[sample] = {"qpc_pre": q0, "qpc_post": q1, "filetime_100ns": filetime}
        if sample.isdigit():
            point = (filetime / 10_000_000.0, (q0 + q1) / 2.0, q1 - q0)
            if fit_rows and (point[0] <= fit_rows[-1][0] or point[1] <= fit_rows[-1][1]):
                raise ValidationInputError("CLOCK_MAP_ORDER", f"fila {row_number}: QPC/UTC no es estrictamente creciente")
            fit_rows.append(point)
    if len(fit_rows) < 20:
        raise ValidationInputError("CLOCK_MAP_TOO_SHORT", f"clock map tiene {len(fit_rows)} puntos numéricos; requiere ≥20")
    xs = [r[0] for r in fit_rows]
    ys = [r[1] for r in fit_rows]
    xm, ym = statistics.fmean(xs), statistics.fmean(ys)
    denominator = math.fsum((x - xm) ** 2 for x in xs)
    if denominator <= 0:
        raise ValidationInputError("CLOCK_MAP_SINGULAR", "clock map no tiene extensión temporal")
    slope = math.fsum((x - xm) * (y - ym) for x, y in zip(xs, ys)) / denominator
    intercept = ym - slope * xm
    qpf_recorded = float(config["qpf_hz"])
    if abs(slope - qpf_recorded) / qpf_recorded > 0.001:
        raise ValidationInputError("QPF_MISMATCH", f"QPF registrado {qpf_recorded:.6f} no concuerda con clock map {slope:.6f}")
    residual_qpc = max(abs(y - (intercept + slope * x)) for x, y, _ in fit_rows)
    max_bracket_qpc = max(r[2] for r in fit_rows)
    uncertainty_s = residual_qpc / qpf_recorded + max_bracket_qpc / (2.0 * qpf_recorded) + 0.0005
    return {
        "qpf_hz": qpf_recorded, "slope_qpf_hz": slope, "intercept_qpc": intercept,
        "uncertainty_s": uncertainty_s, "rows": len(fit_rows), "max_residual_qpc": residual_qpc,
        "max_bracket_qpc": max_bracket_qpc, "named": named,
    }


def verify_effective_edp(config: dict[str, Any]) -> None:
    if config["profile"] != "s20":
        return
    path = Path(config["edp_path"])
    if not path.is_file():
        raise ValidationInputError("EDP_MISSING", f"no existe EDP efectivo registrado: {path}")
    observed = hashlib.sha256(path.read_bytes()).hexdigest()
    if observed != config["edp_sha256"]:
        raise ValidationInputError("EDP_HASH_MISMATCH", "SHA256 del EDP efectivo no coincide")
    lines = path.read_text(encoding="utf-8-sig").splitlines()
    starts = [i for i, line in enumerate(lines) if re.match(r"^\s*-C\s*\(\s*$", line)]
    if len(starts) != 1:
        raise ValidationInputError("EDP_GROUP_COUNT", f"EDP debe declarar exactamente un grupo -C (...); hallados {len(starts)}")
    event_lines: list[str] = []
    for line in lines[starts[0] + 1:]:
        clean = line.strip()
        if clean == ")":
            break
        if not clean or clean.startswith("#"):
            continue
        event_lines.extend(part.strip() for part in clean.split(",") if part.strip())
    else:
        raise ValidationInputError("EDP_GROUP_UNCLOSED", "grupo -C del EDP no está cerrado")
    if event_lines != config["event_groups"][0]:
        raise ValidationInputError("EVENT_SET_MISMATCH", "event_groups no coincide, en orden, con el grupo de eventos EDP efectivo")


def _local_filetime(date_part: str, time_part: str, offset_minutes: int) -> float:
    local = dt.datetime.strptime(f"{date_part} {time_part}", "%m/%d/%Y %H:%M:%S.%f")
    utc = local - dt.timedelta(minutes=offset_minutes)
    epoch = dt.datetime(1601, 1, 1)
    delta = utc - epoch
    ticks = (delta.days * 86400 + delta.seconds) * 10_000_000 + delta.microseconds * 10
    return float(ticks)


def _parse_event_row(fields: list[str], event: str, lp_count: int) -> tuple[int, list[int | None] | int | None]:
    # EMON standard output columns: elapsed-TSC field, then per-LP counters;
    # package MSR rows carry one package value after elapsed-TSC.
    if len(fields) < 3:
        raise ValidationInputError("EVENT_ROW_SHORT", f"fila incompleta: {event}")
    elapsed = parse_count(fields[1])
    if elapsed is None:
        raise ValidationInputError("EVENT_ELAPSED_INVALID", f"elapsed-TSC inválido en {event}")
    values = fields[2:]
    if event in CPU_EVENTS or event in THREAD_MSR_EVENTS:
        if len(values) != lp_count:
            raise ValidationInputError("LP_VECTOR_LENGTH", f"{event}: {len(values)} valores LP, se esperaban {lp_count}")
        return elapsed, [parse_count(v) for v in values]
    if event in PACKAGE_MSR_EVENTS:
        if len(values) != 1:
            raise ValidationInputError("PACKAGE_VECTOR_LENGTH", f"{event}: se esperaba una lectura de paquete")
        return elapsed, parse_count(values[0])
    raise ValidationInputError("EVENT_UNKNOWN", f"evento no admitido: {event}")


def parse_emon_raw(path: str | Path, config: dict[str, Any], clock: dict[str, Any]) -> list[dict[str, Any]]:
    raw = Path(path).read_text(encoding="utf-8-sig", errors="strict")
    lp_count = int(config["logical_processor_count"])
    expected = set(config["event_groups"][0])
    pieces = re.split(r"(?m)^={10,}\s*$", raw)
    records: list[dict[str, Any]] = []
    last_qpc = -math.inf
    seen_intervals: set[tuple[int, int, int]] = set()
    offset = config["emon_local_utc_offset_minutes"]
    if isinstance(offset, bool) or not isinstance(offset, int) or abs(offset) > 14 * 60:
        raise ValidationInputError("TIMEZONE_INVALID", "EMON local UTC offset must be an explicit integer minute offset")
    for piece in pieces:
        lines = piece.splitlines()
        timestamp = next((TIMESTAMP_RE.match(line.strip()) for line in lines if TIMESTAMP_RE.match(line.strip())), None)
        if timestamp is None:
            continue
        filetime = _local_filetime(timestamp.group(1), timestamp.group(2), offset)
        qpc_end = (clock["intercept_qpc"] + clock["slope_qpf_hz"] * (filetime / 10_000_000.0)) / clock["qpf_hz"]
        if qpc_end <= last_qpc:
            raise ValidationInputError("TIME_ORDER_INVALID", "marcas EMON repetidas o fuera de orden")
        events: dict[str, Any] = {}
        elapsed_by_event: dict[str, int] = {}
        for line in lines:
            if "\t" not in line:
                continue
            fields = line.split("\t")
            while fields and fields[-1].strip() == "":
                fields.pop()
            event = fields[0].strip()
            if event not in expected:
                if event.startswith(("MSR_EVENT:", "CPU_CLK_UNHALTED.", "INST_RETIRED.", "L2_RQSTS.", "OFFCORE_REQUESTS.")):
                    raise ValidationInputError("EVENT_SET_MISMATCH", f"evento EMON no configurado dentro del grupo: {event}")
                continue
            if event in events:
                raise ValidationInputError("EVENT_DUPLICATE", f"evento duplicado en intervalo EMON: {event}")
            elapsed, values = _parse_event_row(fields, event, lp_count)
            elapsed_by_event[event] = elapsed
            events[event] = values
        if not events:
            continue
        if set(events) != expected:
            missing = sorted(expected - events.keys())
            raise ValidationInputError("EVENT_MISSING", f"grupo EMON incompleto: faltan {', '.join(missing)}")
        if len(set(elapsed_by_event.values())) != 1:
            raise ValidationInputError("EVENT_INTERVAL_MISMATCH", "eventos del mismo grupo tienen distinta marca elapsed-TSC")
        # EMON's elapsed-TSC field is the actual collector interval.  Per-LP
        # REF_TSC is a CPU-unhalted event and can stop during halt/modulation;
        # it is used only in THREAD/REF frequency ratios, never as wall time.
        duration = next(iter(elapsed_by_event.values())) / float(config["f_tsc_qpc_hz"])
        windows: dict[int, dict[str, Any]] = {}
        for lp in range(lp_count):
            if not math.isfinite(duration) or duration <= 0:
                windows[lp] = {"valid": False, "reason": "EMON_INTERVAL_INVALID"}
            else:
                start = qpc_end - duration
                key = (lp, round(start * 1e6), round(qpc_end * 1e6))
                if key in seen_intervals:
                    raise ValidationInputError("WINDOW_DUPLICATE", f"intervalo EMON duplicado en LP{lp}")
                windows[lp] = {"valid": True, "start_qpc": start, "end_qpc": qpc_end, "duration_s": duration}
        records.append({"index": len(records), "end_qpc": qpc_end, "events": events, "windows": windows,
                        "elapsed_tsc": next(iter(elapsed_by_event.values()))})
        last_qpc = qpc_end
        seen_intervals.update((lp, round(w["start_qpc"] * 1e6), round(w["end_qpc"] * 1e6)) for lp, w in windows.items() if w["valid"])
    if not records:
        raise ValidationInputError("EMON_NO_INTERVALS", "EMON raw no contiene intervalos completos")
    return records


def _mark_rows(path: str | Path, config: dict[str, Any], qpf: float) -> list[dict[str, Any]]:
    with Path(path).open("r", encoding="utf-8-sig", newline="") as f:
        rows = _strict_csv_rows(f.readlines(), context="MARKS", required=set(), allowed={
            "sample_id", "p_core", "Pcore", "P_core", "replica", "replica_id", "condition", "condicion",
            "t_pad_ini", "qpc_pad_start", "t_ini", "qpc_start", "t_fin", "qpc_end", "t_pad_fin",
            "qpc_pad_end", "worker_lp", "comparison_set", "clock_domain", "utc_pad_start", "utc_start",
            "utc_end", "utc_pad_end"})
    if not rows:
        raise ValidationInputError("MARKS_EMPTY", "el archivo de marcas no contiene muestras")
    aliases = {
        "p_core": ("p_core", "Pcore", "P_core"), "replica": ("replica", "replica_id"),
        "condition": ("condition", "condicion"), "t_pad_ini": ("t_pad_ini", "qpc_pad_start"),
        "t_ini": ("t_ini", "qpc_start"), "t_fin": ("t_fin", "qpc_end"),
        "t_pad_fin": ("t_pad_fin", "qpc_pad_end"),
    }
    alias_groups = [{"p_core", "Pcore", "P_core"}, {"replica", "replica_id"}, {"condition", "condicion"},
                    {"t_pad_ini", "qpc_pad_start"}, {"t_ini", "qpc_start"}, {"t_fin", "qpc_end"},
                    {"t_pad_fin", "qpc_pad_end"}]
    header_fields = set(rows[0])
    if any(len(header_fields & aliases_for_field) != 1 for aliases_for_field in alias_groups):
        raise ValidationInputError("MARKS_HEADER", "marcas deben declarar exactamente una columna por campo requerido")
    normalized = []
    for idx, row in enumerate(rows):
        item: dict[str, Any] = {"sample_id": row.get("sample_id") or f"row-{idx}"}
        for key, names in aliases.items():
            found = next((row[name] for name in names if name in row and row[name] != ""), None)
            if found is None:
                raise ValidationInputError("MARK_FIELD_MISSING", f"marca {idx} carece de {key}")
            try:
                if key.startswith("t_"):
                    item[key] = float(found)
                    if not math.isfinite(item[key]):
                        raise ValueError("non-finite timestamp")
                elif key in {"p_core", "replica"}:
                    numeric = float(found)
                    if not math.isfinite(numeric) or not numeric.is_integer():
                        raise ValueError("non-integer field")
                    item[key] = int(numeric)
                else:
                    item[key] = str(found).strip()
                    if not item[key]:
                        raise ValueError("empty text")
            except ValueError as exc:
                raise ValidationInputError("MARK_VALUE_INVALID", f"marca {idx}: {key} inválido") from exc
        # Source marks use QPC ticks unless explicitly declared as seconds.
        item["clock_domain"] = row.get("clock_domain") or config.get("marks_clock_domain", "qpc_ticks")
        if item["clock_domain"] == "qpc_ticks":
            for key in ("t_pad_ini", "t_ini", "t_fin", "t_pad_fin"):
                item[key] /= qpf
        elif item["clock_domain"] != "qpc_seconds":
            raise ValidationInputError("MARK_TIMEBASE_INVALID", "clock_domain debe ser qpc_ticks o qpc_seconds")
        if not (item["t_pad_ini"] <= item["t_ini"] < item["t_fin"] <= item["t_pad_fin"]):
            raise ValidationInputError("MARK_ORDER_INVALID", f"orden de marcas inválido para {item['sample_id']}")
        core_entry = next((x for x in config["lp_map"] if x["p_core"] == item["p_core"]), None)
        if core_entry is None:
            raise ValidationInputError("LP_MAP_MISMATCH", f"marca referencia P-core desconocido: {item['p_core']}")
        try:
            worker_text = row.get("worker_lp", "")
            worker_value = float(worker_text) if worker_text else float(core_entry["worker_lp"])
            if not math.isfinite(worker_value) or not worker_value.is_integer():
                raise ValueError("non-integer LP")
            item["worker_lp"] = int(worker_value)
        except ValueError as exc:
            raise ValidationInputError("MARK_VALUE_INVALID", f"marca {idx}: worker_lp inválido") from exc
        item["core_id"] = core_entry["core_id"]
        item["smt_siblings"] = list(core_entry["smt_siblings"])
        item["comparison_set"] = (row.get("comparison_set") or "").strip()
        normalized.append(item)
    keys = [(m["sample_id"], m["p_core"]) for m in normalized]
    if len(keys) != len(set(keys)):
        raise ValidationInputError("MARK_DUPLICATE", "marca duplicada para sample_id/P-core")
    return normalized


def _union_coverage(a: float, b: float, intervals: Iterable[tuple[float, float]], u: float) -> tuple[float, float, list[tuple[float, float]]]:
    segments: list[tuple[float, float]] = []
    for start, end in intervals:
        left, right = max(a, start + u), min(b, end - u)
        if right > left:
            segments.append((left, right))
    segments.sort()
    merged: list[list[float]] = []
    for start, end in segments:
        if not merged or start > merged[-1][1]:
            merged.append([start, end])
        else:
            merged[-1][1] = max(merged[-1][1], end)
    span = b - a
    covered = math.fsum(end - start for start, end in merged)
    cursor, max_gap = a, 0.0
    for start, end in merged:
        max_gap = max(max_gap, start - cursor)
        cursor = max(cursor, end)
    max_gap = max(max_gap, b - cursor)
    return (covered / span if span > 0 else 0.0), max_gap, [(x[0], x[1]) for x in merged]


def coverage_fraction(a: float, b: float, windows: Iterable[tuple[float, float]], uncertainty_s: float = 0.0) -> tuple[float, float]:
    """Coverage in seconds; each window is contracted before union."""
    coverage, gap, _ = _union_coverage(a, b, windows, uncertainty_s)
    return coverage, gap


def modular_delta_32(before: int, after: int) -> int:
    if any(isinstance(v, bool) or not isinstance(v, int) or not 0 <= v <= 0xFFFFFFFF for v in (before, after)):
        raise ValidationInputError("ENERGY_VALUE_INVALID", "snapshot 0x611 debe ser uint32")
    return (after - before) & 0xFFFFFFFF


def ratio_or_not_estimable(numerator: int, denominator: int) -> tuple[str, float | None]:
    if denominator == 0:
        return "NOT_ESTIMABLE", None
    return "ESTIMABLE", numerator / denominator


def active_frequency_hz(thread_counts: Iterable[int], ref_counts: Iterable[int], f_tsc_hz: float) -> float | None:
    thread_total = sum(thread_counts)
    ref_total = sum(ref_counts)
    return None if ref_total == 0 else f_tsc_hz * thread_total / ref_total


def _thermal_state(raw19c: int | None, raw1b1: int | None, config: dict[str, Any]) -> tuple[list[str], dict[str, Any]]:
    reasons: list[str] = []
    out: dict[str, Any] = {"raw_19c": raw19c, "raw_1b1": raw1b1}
    if raw19c is None:
        reasons.append("CHANNEL_VALUE_MISSING")
        return reasons, out
    # IA32_THERM_STATUS (0x19C): bit 31 alone validates its digital reading.
    valid = bool(raw19c & (1 << 31))
    out["digital_reading_valid_19c_bit31"] = valid
    if not valid:
        reasons.append("THERMAL_READING_INVALID")
    readout = (raw19c >> 16) & 0x7F
    out["digital_readout_c"] = readout if valid else None
    out["tcc_margin_c"] = readout if valid else None
    out["temperature_estimate_c"] = float(config["tcc_target_c"]) - readout if valid else None
    if valid and readout < 5:
        reasons.append("THERMAL_MARGIN_LOW")
    current = {"thermal": bool(raw19c & (1 << 0)), "prochot": bool(raw19c & (1 << 2))}
    history = {"thermal_log": bool(raw19c & (1 << 1)), "prochot_log": bool(raw19c & (1 << 3))}
    power = {"current": bool(raw19c & (1 << 10)), "historical_log": bool(raw19c & (1 << 11))}
    out.update({"current_19c": current, "historical_19c": history, "power_limit_19c": power})
    if current["thermal"]:
        reasons.append("THERMAL_CURRENT_ACTIVE")
    if current["prochot"]:
        reasons.append("PROCHOT_CURRENT")
    if (history["thermal_log"] or history["prochot_log"]) and not config["thermal_history_clear_at_start"]:
        reasons.append("PROCHOT_HISTORICAL_UNCLEARED" if history["prochot_log"] else "THERMAL_HISTORY_UNCLEARED")
    out["power_limit_observed"] = power["current"] or power["historical_log"]
    if raw1b1 is not None:
        # Package status is kept separate.  Its bit 31 is never used as a
        # validity bit; only documented status masks are read here.
        out["package_1b1_current"] = {"thermal": bool(raw1b1 & (1 << 0)), "prochot": bool(raw1b1 & (1 << 2))}
        out["package_1b1_historical"] = {"thermal_log": bool(raw1b1 & (1 << 1)), "prochot_log": bool(raw1b1 & (1 << 3))}
        out["package_1b1_power_limit"] = {"current": bool(raw1b1 & (1 << 10)), "historical_log": bool(raw1b1 & (1 << 11))}
        if out["package_1b1_current"]["thermal"]:
            reasons.append("THERMAL_CURRENT_ACTIVE")
        if out["package_1b1_current"]["prochot"]:
            reasons.append("PROCHOT_CURRENT")
        if (out["package_1b1_historical"]["thermal_log"] or out["package_1b1_historical"]["prochot_log"]) and not config["thermal_history_clear_at_start"]:
            reasons.append("PROCHOT_HISTORICAL_UNCLEARED" if out["package_1b1_historical"]["prochot_log"] else "THERMAL_HISTORY_UNCLEARED")
        out["power_limit_observed"] = out["power_limit_observed"] or out["package_1b1_power_limit"]["current"] or out["package_1b1_power_limit"]["historical_log"]
    if out["power_limit_observed"]:
        out["observations"] = ["POWER_LIMIT_OBSERVED"]
    return sorted(set(reasons)), out


def _channel_reasons(
    records: list[dict[str, Any]], mark: dict[str, Any], lp: int, config: dict[str, Any],
    clock: dict[str, Any], mode: str, all_marks: list[dict[str, Any]],
) -> tuple[list[str], dict[str, Any], list[dict[str, Any]]]:
    u = clock["uncertainty_s"]
    a, b = mark["t_ini"], mark["t_fin"]
    pa, pb = mark["t_pad_ini"], mark["t_pad_fin"]
    selected: list[dict[str, Any]] = []
    window_statuses: list[dict[str, Any]] = []
    excluded_cross = 0
    too_long = 0
    for rec in records:
        win = rec["windows"].get(lp)
        if not win or not win.get("valid"):
            continue
        s, e = win["start_qpc"], win["end_qpc"]
        if e <= a or s >= b:
            continue
        ext_s, ext_e = s - u, e + u
        other_condition_overlap = any(
            other is not mark and other["p_core"] == mark["p_core"] and other["condition"] != mark["condition"]
            and other["replica"] == mark["replica"] and (
                (ext_s < other["t_fin"] and ext_e > other["t_ini"])
                or (ext_s >= other["t_pad_ini"] and ext_e <= other["t_pad_fin"])
            ) for other in all_marks
        )
        if s - u < pa or e + u > pb or other_condition_overlap:
            excluded_cross += 1
            window_statuses.append({"interval": rec["index"], "state": "EXCLUDED", "reason": "CROSS_CONDITION", "s_qpc": s, "e_qpc": e})
            continue
        if win["duration_s"] > 0.100:
            too_long += 1
            window_statuses.append({"interval": rec["index"], "state": "INVALID", "reason": "WINDOW_TOO_LONG", "duration_s": win["duration_s"]})
            continue
        selected.append({"record": rec, "s": s, "e": e, "duration_s": win["duration_s"]})
        window_statuses.append({"interval": rec["index"], "state": "CANDIDATE", "reason": None, "s_qpc": s, "e_qpc": e, "duration_s": win["duration_s"]})
    coverage, gap, covered_segments = _union_coverage(a, b, [(w["s"], w["e"]) for w in selected], u)
    reasons: list[str] = []
    if u > 0.001:
        reasons.append("CLOCK_MAP_UNCERTAINTY")
    if a - pa < 0.200 or pb - b < 0.200:
        reasons.append("PADDING_MARGIN_LOW")
    if len(selected) < 20:
        reasons.append("WINDOW_COUNT_LOW")
    if coverage < 0.95:
        reasons.append("COVERAGE_LOW")
    if gap > 0.020:
        reasons.append("GAP_TOO_LARGE")
    if too_long:
        reasons.append("WINDOW_TOO_LONG")

    # Verify each required event for each accepted LP/window; no channel can be
    # credited because another event or core happened to be present.
    win_freq: list[tuple[int, float]] = []
    thread_sum: list[int] = []
    ref_sum: list[int] = []
    l2_num = l2_den = llc_num = llc_den = 0
    energy_sum = 0
    energy_elapsed = 0.0
    thermal_rows: list[dict[str, Any]] = []
    per_window_metrics: list[dict[str, Any]] = []
    previous_energy: int | None = None
    valid_count = 0
    for w in selected:
        ev = w["record"]["events"]
        try:
            thread = ev[EVENT_THREAD][lp]
            ref = ev[EVENT_REF][lp]
            l2n, l2d = ev[EVENT_L2_MISS][lp], ev[EVENT_L2_DEMAND][lp]
            llcn, llcd = ev[EVENT_LLC_MISS][lp], ev[EVENT_LLC_DEMAND][lp]
        except (KeyError, IndexError, TypeError):
            reasons.append("CHANNEL_VALUE_MISSING")
            continue
        if any(v is None for v in (thread, ref, l2n, l2d, llcn, llcd)):
            reasons.append("CHANNEL_VALUE_MISSING")
            continue
        if ref <= 0:
            reasons.append("THREAD_REF_DENOM_ZERO")
            continue
        active = float(config["f_tsc_qpc_hz"]) * thread / ref
        win_freq.append((w["record"]["index"], active))
        thread_sum.append(thread)
        ref_sum.append(ref)
        l2_num += l2n
        l2_den += l2d
        llc_num += llcn
        llc_den += llcd
        energy_delta_window: int | None = None
        if config["energy_semantics"] == "INTERVAL_DELTA":
            energy_value = w["record"]["events"].get(EVENT_611)
            if not isinstance(energy_value, int) or energy_value > 0xFFFFFFFF:
                reasons.append("ENERGY_VALUE_INVALID")
            else:
                energy_delta_window = energy_value
                energy_sum += energy_value
                energy_elapsed += w["duration_s"]
        else:
            snap = w["record"]["events"].get(EVENT_611)
            if not isinstance(snap, int) or snap > 0xFFFFFFFF:
                reasons.append("ENERGY_VALUE_INVALID")
            elif previous_energy is None:
                previous_energy = snap
            else:
                energy_delta_window = modular_delta_32(previous_energy, snap)
                energy_sum += energy_delta_window
                energy_elapsed += w["duration_s"]
                previous_energy = snap
        raw19 = ev.get(EVENT_19C)
        raw19 = raw19[lp] if isinstance(raw19, list) and lp < len(raw19) else None
        raw1b1 = ev.get(EVENT_1B1)
        raw1b1 = raw1b1 if isinstance(raw1b1, int) else None
        tr, thermal = _thermal_state(raw19, raw1b1, config)
        reasons.extend(tr)
        thermal_rows.append(thermal)
        l2_window_state, l2_window_fraction = ratio_or_not_estimable(l2n, l2d)
        llc_window_state, llc_window_fraction = ratio_or_not_estimable(llcn, llcd)
        per_window_metrics.append({
            "interval": w["record"]["index"], "f_active_hz": active,
            "l2_numerator": l2n, "l2_denominator": l2d, "l2_state": l2_window_state, "l2_fraction": l2_window_fraction,
            "llc_numerator": llcn, "llc_denominator": llcd, "llc_state": llc_window_state, "llc_fraction": llc_window_fraction,
            "energy_delta": energy_delta_window,
            "package_power_w": (float(config["energy_unit_joule"]) * energy_delta_window / w["duration_s"]) if energy_delta_window is not None else None,
            "thermal": thermal, "thermal_reasons": tr,
        })
        valid_count += 1
    active_mean = active_frequency_hz(thread_sum, ref_sum, float(config["f_tsc_qpc_hz"]))
    excursions = []
    if active_mean and active_mean > 0:
        for window_id, frequency in win_freq:
            delta = abs(frequency / active_mean - 1.0)
            if delta > 0.03:
                excursions.append({"interval": window_id, "fraction": delta})
        if excursions:
            reasons.append("FREQ_WINDOW_EXCURSION")
    l2_state, l2_ratio = ratio_or_not_estimable(l2_num, l2_den)
    llc_state, llc_ratio = ratio_or_not_estimable(llc_num, llc_den)
    if l2_state == "NOT_ESTIMABLE" or llc_state == "NOT_ESTIMABLE":
        reasons.append("CACHE_DENOM_ZERO")
    pkg_power = None
    if energy_elapsed > 0:
        pkg_power = float(config["energy_unit_joule"]) * energy_sum / energy_elapsed
    if mode not in {"temporal", "full"}:
        raise ValidationInputError("MODE_INVALID", "mode debe ser temporal o full")
    stats = {
        "lp": lp, "window_count": len(selected), "excluded_cross_condition": excluded_cross,
        "coverage_lower": coverage, "coverage_pct": coverage * 100.0, "gap_upper_s": gap,
        "uncertainty_s": u, "max_window_s": max((w["duration_s"] for w in selected), default=0.0),
        "f_active_hz": active_mean, "freq_excursions": excursions,
        "l2_numerator": l2_num, "l2_denominator": l2_den, "l2_fraction_state": l2_state, "l2_fraction": l2_ratio,
        "llc_numerator": llc_num, "llc_denominator": llc_den, "llc_fraction_state": llc_state, "llc_fraction": llc_ratio,
        "energy_delta": energy_sum, "energy_elapsed_s": energy_elapsed, "package_power_w": pkg_power,
        "thermal_windows": thermal_rows, "window_metrics": per_window_metrics,
    }
    return sorted(set(reasons)), stats, window_statuses


def _add_frequency_controls(rows: list[dict[str, Any]], mode: str) -> None:
    if mode != "full":
        return
    # A/B/C comparisons and replica drift are evaluated using each sample's
    # ratio-of-sums active frequency, keyed by explicit comparison_set.
    sets: dict[str, list[dict[str, Any]]] = {}
    for row in rows:
        cs = row["sample"].get("comparison_set", "")
        if cs and row["channel"] == "THREAD_REF_TSC":
            sets.setdefault(cs, []).append(row)
    for comparison_id, members in sets.items():
        by_core_replica: dict[tuple[int, int], list[dict[str, Any]]] = {}
        by_condition_replica: dict[tuple[str, int], list[dict[str, Any]]] = {}
        for member in members:
            by_core_replica.setdefault((member["sample"]["p_core"], member["sample"]["replica"]), []).append(member)
            by_condition_replica.setdefault((member["sample"]["condition"], member["sample"]["replica"]), []).append(member)
        for (_core, _replica), peers in by_core_replica.items():
            fs = [p["metrics"].get("f_active_hz") for p in peers if p["metrics"].get("f_active_hz")]
            variants = {p["sample"]["condition"] for p in peers}
            if len(fs) != 3 or variants != {"A", "B", "C"}:
                for p in peers:
                    p["_control_reasons"].append("CONTROL_PEERS_MISSING")
            if len(fs) >= 2:
                for i in range(len(fs)):
                    for j in range(i + 1, len(fs)):
                        if abs(fs[i] - fs[j]) / ((fs[i] + fs[j]) / 2.0) > 0.03:
                            for p in peers:
                                p["_control_reasons"].append("FREQ_CONDITION_MISMATCH")
        for (_condition, _replica), peers in by_condition_replica.items():
            fs = [p["metrics"].get("f_active_hz") for p in peers if p["metrics"].get("f_active_hz")]
            if len(fs) != 8:
                for p in peers:
                    p["_control_reasons"].append("CONTROL_PEERS_MISSING")
            if len(fs) == 8:
                mean = statistics.fmean(fs)
                if (max(fs) - min(fs)) / mean > 0.05:
                    for p in peers:
                        p["_control_reasons"].append("FREQ_CORE_DISPERSION")
        by_core_condition: dict[tuple[int, str], list[dict[str, Any]]] = {}
        for member in members:
            by_core_condition.setdefault((member["sample"]["p_core"], member["sample"]["condition"]), []).append(member)
        for peers in by_core_condition.values():
            fs = [p["metrics"].get("f_active_hz") for p in peers if p["metrics"].get("f_active_hz")]
            if len(fs) < 2:
                for p in peers:
                    p["_control_reasons"].append("CONTROL_PEERS_MISSING")
            if len(fs) >= 2:
                med = statistics.median(fs)
                for p in peers:
                    f = p["metrics"].get("f_active_hz")
                    if f and abs(f / med - 1.0) > 0.03:
                        p["reasons"].append("FREQ_REPLICA_DRIFT")


def _propagate_frequency_controls(rows: list[dict[str, Any]]) -> None:
    """Apply block/core frequency qualification to each channel for that sample."""
    codes_of_interest = {"FREQ_WINDOW_EXCURSION", "FREQ_CORE_DISPERSION", "FREQ_REPLICA_DRIFT",
                         "FREQ_CONDITION_MISMATCH", "CONTROL_PEERS_MISSING"}
    by_sample: dict[tuple[str, int], tuple[set[str], set[int]]] = {}
    for row in rows:
        if row["channel"] != "THREAD_REF_TSC":
            continue
        key = (str(row["sample"]["sample_id"]), int(row["core"]))
        reasons = {item["code"] if isinstance(item, dict) else item for item in row["reasons"]}
        reasons.update(row.get("_control_reasons", []))
        reasons &= codes_of_interest
        intervals = {int(item["interval"]) for item in row["metrics"].get("freq_excursions", [])}
        by_sample[key] = (reasons, intervals)
    for row in rows:
        key = (str(row["sample"]["sample_id"]), int(row["core"]))
        if key not in by_sample:
            continue
        reasons, intervals = by_sample[key]
        row.setdefault("_control_reasons", []).extend(sorted(reasons))
        for window in row.get("window_statuses", []):
            if window.get("state") in {"VALID", "INVALID"} and int(window["interval"]) in intervals:
                current = set(window.get("reasons", []))
                current.add("FREQ_WINDOW_EXCURSION")
                window["reasons"] = sorted(current)
                window["state"] = "INVALID"


def validate_capture(
    emon_raw: str | Path, clock_map: str | Path, block_marks: str | Path,
    effective_config: dict[str, Any] | str | Path, *, mode: str | None = None,
) -> dict[str, Any]:
    config = load_config(effective_config)
    verify_effective_edp(config)
    clock = parse_clock_map(clock_map, config)
    qpf = float(config["qpf_hz"])
    tsc_disagreement = abs(float(config["f_tsc_qpc_hz"]) - float(config["f_tsc_qualified_hz"])) / float(config["f_tsc_qualified_hz"])
    tsc_config_disagreement = abs(float(config["f_tsc_qualified_hz"]) - float(config["f_tsc_configured_hz"])) / float(config["f_tsc_configured_hz"])
    records = parse_emon_raw(emon_raw, config, clock)
    marks = _mark_rows(block_marks, config, qpf)
    run_mode = mode or config["mode"]
    rows: list[dict[str, Any]] = []
    for mark in marks:
        lp = mark["worker_lp"]
        core_entry = next(x for x in config["lp_map"] if x["p_core"] == mark["p_core"])
        sample_reasons: list[str] = []
        if lp != core_entry["worker_lp"] or lp not in core_entry["smt_siblings"]:
            sample_reasons.append("SMT_SCOPE_MIXED")
        if tsc_disagreement > 0.001:
            sample_reasons.append("TSC_QPC_DISAGREEMENT")
        if tsc_config_disagreement > 0.001:
            sample_reasons.append("TSC_QPC_DISAGREEMENT")
        if clock["uncertainty_s"] > 0.001:
            sample_reasons.append("CLOCK_MAP_UNCERTAINTY")
        for channel in ("THREAD_REF_TSC", "L2_DEMAND_DATA_READ", "LLC_DEMAND_DATA_READ", "ENERGY_PKG", "THERMAL_19C", "THERMAL_1B1"):
            reasons, metrics, windows = _channel_reasons(records, mark, lp, config, clock, run_mode, marks)
            reasons.extend(sample_reasons)
            if channel == "ENERGY_PKG" and metrics["package_power_w"] is None:
                reasons.append("ENERGY_VALUE_INVALID")
            if channel == "L2_DEMAND_DATA_READ" and metrics["l2_fraction_state"] == "NOT_ESTIMABLE":
                reasons.append("CACHE_DENOM_ZERO")
            if channel == "LLC_DEMAND_DATA_READ" and metrics["llc_fraction_state"] == "NOT_ESTIMABLE":
                reasons.append("CACHE_DENOM_ZERO")
            if channel == "THERMAL_19C" and not metrics["thermal_windows"]:
                reasons.append("THERMAL_READING_INVALID")
            if channel == "THERMAL_1B1" and not any(x.get("raw_1b1") is not None for x in metrics["thermal_windows"]):
                reasons.append("CHANNEL_VALUE_MISSING")
            common = {"CLOCK_MAP_UNCERTAINTY", "WINDOW_COUNT_LOW", "COVERAGE_LOW", "GAP_TOO_LARGE", "PADDING_MARGIN_LOW", "WINDOW_TOO_LONG",
                      "SMT_SCOPE_MIXED", "TSC_QPC_DISAGREEMENT", "CHANNEL_VALUE_MISSING"}
            scoped = {
                "THREAD_REF_TSC": common | {"THREAD_REF_DENOM_ZERO", "FREQ_WINDOW_EXCURSION"},
                "L2_DEMAND_DATA_READ": common | {"THREAD_REF_DENOM_ZERO", "CACHE_DENOM_ZERO"},
                "LLC_DEMAND_DATA_READ": common | {"THREAD_REF_DENOM_ZERO", "CACHE_DENOM_ZERO"},
                "ENERGY_PKG": common | {"THREAD_REF_DENOM_ZERO", "ENERGY_VALUE_INVALID"},
                "THERMAL_19C": common | {"THERMAL_READING_INVALID", "THERMAL_MARGIN_LOW", "THERMAL_CURRENT_ACTIVE",
                                           "PROCHOT_CURRENT", "PROCHOT_HISTORICAL_UNCLEARED", "THERMAL_HISTORY_UNCLEARED"},
                "THERMAL_1B1": common | {"THERMAL_CURRENT_ACTIVE", "PROCHOT_CURRENT", "PROCHOT_HISTORICAL_UNCLEARED", "THERMAL_HISTORY_UNCLEARED"},
            }
            reasons = sorted(set(reasons) & scoped[channel])
            if run_mode == "full" and not mark.get("comparison_set"):
                reasons.append("CONTROL_PEERS_MISSING")
            cache_not_estimable = (channel == "L2_DEMAND_DATA_READ" and metrics["l2_fraction_state"] == "NOT_ESTIMABLE") or (
                channel == "LLC_DEMAND_DATA_READ" and metrics["llc_fraction_state"] == "NOT_ESTIMABLE")
            state = "NOT_ESTIMABLE" if cache_not_estimable and set(reasons) <= {"CACHE_DENOM_ZERO"} else "INVALID" if reasons else "VALID"
            window_metrics = {x["interval"]: x for x in metrics["window_metrics"]}
            channel_windows = []
            for win in windows:
                current = dict(win)
                current["channel"] = channel
                current["core"] = mark["p_core"]
                current["sample_id"] = mark["sample_id"]
                if win["state"] == "CANDIDATE":
                    detail = window_metrics.get(win["interval"])
                    current["metrics"] = detail
                    window_reasons: list[str] = []
                    if detail is None:
                        window_reasons.append("CHANNEL_VALUE_MISSING")
                    elif channel == "THREAD_REF_TSC":
                        if any(e["interval"] == win["interval"] for e in metrics["freq_excursions"]):
                            window_reasons.append("FREQ_WINDOW_EXCURSION")
                    elif channel == "L2_DEMAND_DATA_READ" and detail["l2_state"] == "NOT_ESTIMABLE":
                        window_reasons.append("CACHE_DENOM_ZERO")
                    elif channel == "LLC_DEMAND_DATA_READ" and detail["llc_state"] == "NOT_ESTIMABLE":
                        window_reasons.append("CACHE_DENOM_ZERO")
                    elif channel == "ENERGY_PKG" and detail["energy_delta"] is None:
                        window_reasons.append("ENERGY_VALUE_INVALID")
                    elif channel == "THERMAL_19C":
                        window_reasons.extend(r for r in detail["thermal_reasons"] if r != "POWER_LIMIT_OBSERVED")
                    elif channel == "THERMAL_1B1":
                        window_reasons.extend(r for r in detail["thermal_reasons"] if r in {"THERMAL_CURRENT_ACTIVE", "PROCHOT_CURRENT", "PROCHOT_HISTORICAL_UNCLEARED", "THERMAL_HISTORY_UNCLEARED"})
                        if detail["thermal"].get("raw_1b1") is None:
                            window_reasons.append("CHANNEL_VALUE_MISSING")
                    window_reasons.extend(sample_reasons)
                    current["reasons"] = sorted(set(window_reasons))
                    current["state"] = "INVALID" if current["reasons"] else "VALID"
                    if cache_not_estimable and channel in {"L2_DEMAND_DATA_READ", "LLC_DEMAND_DATA_READ"}:
                        current["state"] = "NOT_ESTIMABLE"
                        current["reasons"] = ["CACHE_DENOM_ZERO"]
                channel_windows.append(current)
            rows.append({"sample": mark, "channel": channel, "core": mark["p_core"], "state": state,
                         "reasons": [{"code": code, "message": code.replace("_", " ").lower()} for code in reasons],
                         "_control_reasons": [], "metrics": metrics, "window_statuses": channel_windows})
    # Full mode controls are based on actual sample-frequency fields.
    _add_frequency_controls(rows, run_mode)
    _propagate_frequency_controls(rows)
    for row in rows:
        row["reasons"] = [{"code": code, "message": code.replace("_", " ").lower()} for code in sorted({r["code"] for r in row["reasons"]} | set(row.get("_control_reasons", [])))]
        del row["_control_reasons"]
        reason_codes = {r["code"] for r in row["reasons"]}
        if reason_codes and not (row["state"] == "NOT_ESTIMABLE" and reason_codes <= {"CACHE_DENOM_ZERO"}):
            row["state"] = "INVALID"
    overall = "INVALID" if not rows or any(r["state"] == "INVALID" for r in rows) else "NOT_ESTIMABLE" if any(r["state"] == "NOT_ESTIMABLE" for r in rows) else "VALID"
    return {
        "schema_version": "S20-RESULT-v1", "state": overall, "mode": run_mode,
        "qpf_hz": qpf, "qpf_source": config["qpf_source"], "f_tsc_qpc_disagreement": tsc_disagreement,
        "clock_uncertainty_s": clock["uncertainty_s"], "clock_map_rows": clock["rows"],
        "emon_interval_groups": len(records), "samples": len(marks), "results": rows,
    }


def read_r1_helper_report(helper_report: str | Path, config: dict[str, Any] | str | Path) -> tuple[dict[str, Any], dict[int, dict[str, Any]]]:
    """Read and verify the helper's write-once metadata and eight worker rows."""
    cfg = load_config(config)
    lines = Path(helper_report).read_text(encoding="utf-8-sig").splitlines()
    metadata: dict[str, str] = {}
    for line in lines:
        if line.startswith("lp,"):
            break
        if "=" not in line:
            raise ValidationInputError("R1_HELPER_FORMAT", "metadatos del helper deben ser pares clave=valor")
        key, value = line.split("=", 1)
        if key in metadata:
            raise ValidationInputError("R1_HELPER_FORMAT", f"metadato duplicado del helper: {key}")
        metadata[key] = value
    else:
        raise ValidationInputError("R1_HELPER_FORMAT", "falta encabezado CSV de workers")
    expected_meta = {"DUMMY_NATIVE", "N_loop", "I0", "QPF", "run_qpc_start", "run_qpc_end"}
    if set(metadata) != expected_meta or metadata["DUMMY_NATIVE"] != "TRUE":
        raise ValidationInputError("R1_HELPER_FORMAT", "metadatos DUMMY del helper incompletos o desconocidos")
    iterations = cfg.get("n_loop")
    try:
        metadata_iterations = int(metadata["N_loop"])
        metadata_i0 = int(metadata["I0"])
    except ValueError as exc:
        raise ValidationInputError("R1_HELPER_FORMAT", "N_loop/I0 del helper no son enteros") from exc
    if iterations != 200_000_000_000 or metadata_iterations != iterations:
        raise ValidationInputError("R1_N_LOOP", "N_loop debe ser exactamente 200.000.000.000")
    expected_count = 3 * iterations + 4
    if metadata_i0 != expected_count:
        raise ValidationInputError("R1_I0_FORMULA", "I0 del helper no concuerda con 3N+4")
    try:
        helper_qpf = int(metadata["QPF"])
        run_start = int(metadata["run_qpc_start"])
        run_end = int(metadata["run_qpc_end"])
    except ValueError as exc:
        raise ValidationInputError("R1_HELPER_FORMAT", "QPF/QPC del helper no son enteros") from exc
    if helper_qpf <= 0 or run_start <= 0 or run_end <= run_start:
        raise ValidationInputError("R1_HELPER_CLOCK", "QPF y límites QPC del helper no son válidos")
    if abs(helper_qpf - float(cfg["qpf_hz"])) / float(cfg["qpf_hz"]) > 0.001:
        raise ValidationInputError("QPF_MISMATCH", "QPF del helper no concuerda con QPF registrada")
    header_index = next(i for i, line in enumerate(lines) if line.startswith("lp,"))
    report = list(csv.DictReader(lines[header_index:]))
    expected_lps = [0, 2, 4, 6, 8, 10, 12, 14]
    if len(report) != 8:
        raise ValidationInputError("R1_WORKER_COUNT", "helper_report debe contener exactamente ocho workers")
    by_lp: dict[int, dict[str, Any]] = {}
    required_columns = {"lp", "group_begin", "lp_begin", "group_end", "lp_end", "result", "expected_result",
                        "expected_retired", "qpc_start", "qpc_end", "duration_s", "affinity"}
    if set(report[0]) != required_columns:
        raise ValidationInputError("R1_HELPER_FORMAT", "columnas del helper no coinciden con el contrato r6")
    for row in report:
        try:
            lp = int(row["lp"])
            parsed = {key: int(row[key]) for key in ("group_begin", "lp_begin", "group_end", "lp_end", "result",
                                                       "expected_result", "expected_retired", "qpc_start", "qpc_end")}
            duration = float(row["duration_s"])
        except (KeyError, TypeError, ValueError) as exc:
            raise ValidationInputError("R1_HELPER_FORMAT", "valor de worker inválido en helper_report") from exc
        if lp in by_lp:
            raise ValidationInputError("R1_LP_SET", f"worker duplicado LP{lp}")
        if lp not in expected_lps:
            raise ValidationInputError("R1_LP_SET", f"LP no seleccionado en helper_report: {lp}")
        if (parsed["result"] != iterations or parsed["expected_result"] != iterations or
                parsed["expected_retired"] != expected_count or row["affinity"] != "PASS" or
                parsed["group_begin"] != 0 or parsed["group_end"] != 0 or
                parsed["lp_begin"] != lp or parsed["lp_end"] != lp):
            raise ValidationInputError("R1_HELPER_RESULT", f"worker LP{lp} no verificó resultado/afinidad")
        if parsed["qpc_start"] < run_start or parsed["qpc_end"] > run_end or parsed["qpc_end"] <= parsed["qpc_start"]:
            raise ValidationInputError("R1_HELPER_CLOCK", f"worker LP{lp} QPC fuera del intervalo informado")
        qpc_duration = (parsed["qpc_end"] - parsed["qpc_start"]) / helper_qpf
        if abs(duration - qpc_duration) > 1.0 / helper_qpf:
            raise ValidationInputError("R1_HELPER_CLOCK", f"worker LP{lp} duration_s no coincide con QPC/QPF")
        if not math.isfinite(duration) or duration < 20.0:
            raise ValidationInputError("R1_REGION_SHORT", f"worker LP{lp} región {row['duration_s']} s <20 s")
        by_lp[lp] = {**row, **parsed, "duration_s": duration}
    if set(by_lp) != set(expected_lps):
        raise ValidationInputError("R1_LP_SET", "helper LP debe ser 0,2,...,14")
    metadata.update({"N_loop": iterations, "I0": expected_count, "QPF": helper_qpf,
                     "run_qpc_start": run_start, "run_qpc_end": run_end})
    return metadata, by_lp


def validate_r1_capture(emon_raw: str | Path, clock_map: str | Path, config: dict[str, Any] | str | Path,
                        helper_report: str | Path) -> dict[str, Any]:
    """Audit one continuous -t 0 INST_RETIRED capture against 3N+4 per LP."""
    cfg = load_config(config)
    if cfg["profile"] != "r1" or cfg["mode"] != "r1" or cfg["event_groups"] != [[EVENT_INST]] or cfg.get("collection_mode") != "application_until_exit":
        raise ValidationInputError("R1_INTERVAL_CONFIG", "R1 exige un grupo INST_RETIRED.ANY y -t 0 hasta salida de app")
    for field, expected in (("multiplexing", False), ("periods_lost", 0), ("reset_count", 0), ("monitored_interval_count", 1)):
        value = cfg.get(field)
        if (field != "multiplexing" and (isinstance(value, bool) or not isinstance(value, int))) or value != expected:
            raise ValidationInputError("R1_CONTINUITY_CONFIG", f"{field} debe ser {expected}")
    metadata, by_lp = read_r1_helper_report(helper_report, cfg)
    expected_lps = [0, 2, 4, 6, 8, 10, 12, 14]
    iterations = cfg.get("n_loop")
    if iterations != 200_000_000_000:
        raise ValidationInputError("R1_N_LOOP", "N_loop debe ser exactamente 200.000.000.000")
    expected_count = 3 * iterations + 4
    for lp, row in by_lp.items():
        if row["expected_retired"] != expected_count:
            raise ValidationInputError("R1_I0_FORMULA", f"I0 inválido para LP{lp}")
    # R1's one application-bounded collection can comprise consecutive sample
    # rows, but all groups must be one uninterrupted set with no reset/loss.
    # R1 equality uses interval deltas and the helper's QPC report; timestamps
    # order groups but are not used to prorate counter values.
    clock = parse_clock_map(clock_map, cfg)
    records = parse_emon_raw(emon_raw, cfg, clock)
    if not records:
        raise ValidationInputError("EMON_NO_INTERVALS", "EMON raw no contiene intervalos R1")
    first_start = min(rec["windows"][expected_lps[0]]["start_qpc"] for rec in records)
    last_end = max(rec["windows"][expected_lps[0]]["end_qpc"] for rec in records)
    start_required = metadata["run_qpc_start"] / metadata["QPF"]
    end_required = metadata["run_qpc_end"] / metadata["QPF"]
    uncertainty = clock["uncertainty_s"]
    if first_start + uncertainty > start_required or last_end - uncertainty < end_required:
        raise ValidationInputError("R1_INTERVAL_NOT_COVERING_WORK", "el intervalo EMON no cubre de forma conservadora toda la región de workers")
    continuity_tolerance = uncertainty + 0.001
    ordered = [rec["windows"][expected_lps[0]] for rec in records]
    for prev, current in zip(ordered, ordered[1:]):
        gap = current["start_qpc"] - prev["end_qpc"]
        if gap > continuity_tolerance or gap < -continuity_tolerance:
            raise ValidationInputError("R1_INTERVAL_GAP", f"intervalo EMON no es continuo; delta={gap:.9f}s")
    counts = {lp: 0 for lp in expected_lps}
    for rec in records:
        vec = rec["events"].get(EVENT_INST)
        if not isinstance(vec, list):
            raise ValidationInputError("R1_EVENT_MISSING", "INST_RETIRED.ANY ausente")
        for lp in expected_lps:
            if vec[lp] is None:
                raise ValidationInputError("R1_LP_COUNT_MISSING", f"INST_RETIRED.ANY sin valor LP{lp}")
            counts[lp] += vec[lp]
    rows = []
    for lp in expected_lps:
        error = abs(counts[lp] - expected_count) / expected_count
        rows.append({"lp": lp, "i_emon": counts[lp], "i0": expected_count, "error_fraction": error,
                     "error_pct": error * 100.0, "state": "VALID" if error <= 0.01 else "HOLD_INSTRUMENTATION"})
    return {"state": "PASS" if all(r["state"] == "VALID" for r in rows) else "HOLD_INSTRUMENTATION",
            "interval_groups": len(records), "interval_count": 1, "continuous": True,
            "qpf_hz": clock["qpf_hz"], "clock_uncertainty_s": uncertainty, "rows": rows}


def _main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validador S-20 único (R2, CLI EMON crudo)")
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("validate", help="valida EMON + mapa QPC/UTC + marcas + configuración")
    p.add_argument("--emon-raw", required=True)
    p.add_argument("--clock-map", required=True)
    p.add_argument("--block-marks", required=True)
    p.add_argument("--config", required=True)
    p.add_argument("--mode", choices=("temporal", "full"))
    r1 = sub.add_parser("validate-r1", help="contrasta una captura continua R1 por LP contra 3N+4")
    r1.add_argument("--emon-raw", required=True)
    r1.add_argument("--clock-map", required=True)
    r1.add_argument("--helper-report", required=True)
    r1.add_argument("--config", required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == "validate-r1":
            result = validate_r1_capture(args.emon_raw, args.clock_map, args.config, args.helper_report)
        else:
            result = validate_capture(args.emon_raw, args.clock_map, args.block_marks, args.config, mode=args.mode)
    except ValidationInputError as exc:
        print(json.dumps({"state": "INVALID_INPUT", "reason": {"code": exc.code, "message": str(exc)}}, ensure_ascii=False))
        return 2
    except (OSError, UnicodeError) as exc:
        print(json.dumps({"state": "INVALID_INPUT", "reason": {"code": "INPUT_IO", "message": str(exc)}}, ensure_ascii=False))
        return 2
    serialized = json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False)
    print(serialized)
    return 0 if result["state"] in {"VALID", "PASS"} else 1


if __name__ == "__main__":
    raise SystemExit(_main())
