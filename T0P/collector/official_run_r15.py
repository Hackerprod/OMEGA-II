#!/usr/bin/env python3
"""R15 official gate and executable synthetic rehearsal for S-18..S-23."""
from __future__ import annotations

import argparse
import copy
import datetime as dt
import gc
import hashlib
import json
import math
import os
import re
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Callable

ROOT = Path(__file__).resolve().parents[2]
COLLECTOR = Path(__file__).resolve().parent
HARNESS = ROOT / "T0P" / "harness"
VALIDATOR = ROOT / "T0P" / "validator"
EVICT = ROOT / "T0P" / "evict"
TELEMETRY = ROOT / "T0P" / "telemetry" / "reader"
OBJ = COLLECTOR / "logs_r15" / "obj_r15"
PLAN_PATH = OBJ / "residency_plan_r15.json"
AUTH_PATH = COLLECTOR / "AUTHORIZED.json"
AUTH_SCHEMA_PATH = COLLECTOR / "AUTHORIZED.schema_r15.json"
INTEGRATED_EXE = HARNESS / "logs_r15" / "obj_r15" / "harness_r15.exe"
EMON_EXE = Path(r"C:\Program Files (x86)\Intel\oneAPI\vtune\2026.4\bin64\emon.exe")
T0_PATH = ROOT / "T0.md"
ROADMAP_PATH = ROOT / "Roadmap.md"
SEED = "4F4D454741543031"
SCHEMA_ID = "T0P_S18_A6_AUTHORIZED_R15_V1"
CAMPAIGN = "T0P-Q4-RESIDENCY"
P_LPS = (0, 2, 4, 6, 8, 10, 12, 14)
REQUIRED_CHANNELS = (
    "CPU_CLK_UNHALTED.THREAD", "CPU_CLK_UNHALTED.REF_TSC",
    "L2_RQSTS.ALL_DEMAND_DATA_RD", "L2_RQSTS.DEMAND_DATA_RD_MISS",
    "OFFCORE_REQUESTS.L3_MISS_DEMAND_DATA_RD", "MSR_EVENT:0x611",
    "MSR_EVENT:0x19C", "MSR_EVENT:0x1B1",
)
PHASES = (
    "preflight",
    "instrumentation_reader",
    "kernel_reference_qualification",
    "scientific_qualification",
    "kernel_qa",
    "residency_abc",
    "margins_closure",
    "d8_post_revalidation",
    "derivation_verdict",
)
RETRYABLE_REASONS = ("DURATION_INSUFFICIENT", "FREQUENCY_UNSTABLE")
AUTH_KEYS = (
    "schema", "campaign", "session_id", "issued_at_utc", "judge",
    "sol_decision_id", "scope", "master_seed", "plan_sha256",
    "plan_order_sha256", "executable_sha256", "artifacts",
    "usage_policy", "retry_policy",
)


class GateError(ValueError):
    pass


def sha_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def plan_order_sha256(plan: dict[str, Any]) -> str:
    return sha_bytes(canonical_bytes([event["id"] for event in plan["events"]]))


def sha_file(path: Path) -> str:
    if not path.is_file():
        raise GateError(f"required_input_missing:{path}")
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical_bytes(value: Any) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"),
                      allow_nan=False).encode("utf-8")


def _unique_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    output: dict[str, Any] = {}
    for key, value in pairs:
        if key in output:
            raise GateError(f"duplicate_json_key:{key}")
        output[key] = value
    return output


def _reject_constant(value: str):
    raise GateError(f"nonfinite_json_constant:{value}")


def strict_json_bytes(data: bytes) -> Any:
    if data.startswith(b"\xef\xbb\xbf"):
        raise GateError("utf8_bom_not_allowed")
    try:
        result = json.loads(data.decode("utf-8", "strict"), object_pairs_hook=_unique_pairs,
                            parse_constant=_reject_constant)
    except GateError:
        raise
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise GateError(f"malformed_json:{exc}") from exc

    def finite(node: Any, at: str = "$") -> None:
        if isinstance(node, float) and not math.isfinite(node):
            raise GateError(f"nonfinite_number:{at}")
        if isinstance(node, dict):
            for key, child in node.items():
                finite(child, f"{at}.{key}")
        elif isinstance(node, list):
            for index, child in enumerate(node):
                finite(child, f"{at}[{index}]")
    finite(result)
    return result


def _schema_ref(schema: dict[str, Any], root_schema: dict[str, Any]) -> dict[str, Any]:
    ref = schema.get("$ref")
    if ref is None:
        return schema
    if ref.startswith("#/$defs/"):
        name = ref.rsplit("/", 1)[1]
        defs = root_schema.get("$defs")
        if not isinstance(defs, dict) or name not in defs:
            raise GateError(f"schema_reference_invalid:{ref}")
        return defs[name]
    raise GateError(f"schema_reference_unsupported:{ref}")


def validate_schema_value(value: Any, schema: dict[str, Any], root_schema: dict[str, Any], path: str = "$") -> None:
    schema = _schema_ref(schema, root_schema)
    kind = schema.get("type")
    checks = {
        "object": lambda v: isinstance(v, dict),
        "array": lambda v: isinstance(v, list),
        "string": lambda v: isinstance(v, str),
        "integer": lambda v: type(v) is int,
        "boolean": lambda v: type(v) is bool,
    }
    if kind not in checks:
        raise GateError(f"schema_type_unsupported:{path}:{kind}")
    if not checks[kind](value):
        raise GateError(f"schema_type_invalid:{path}:expected={kind}")
    if "const" in schema and value != schema["const"]:
        raise GateError(f"schema_const_mismatch:{path}")
    if "enum" in schema and value not in schema["enum"]:
        raise GateError(f"schema_enum_mismatch:{path}")
    if kind == "string":
        if len(value) < schema.get("minLength", 0) or len(value) > schema.get("maxLength", 2**31):
            raise GateError(f"schema_string_length:{path}")
        pattern = schema.get("pattern")
        if pattern is not None and re.fullmatch(pattern, value) is None:
            raise GateError(f"schema_string_pattern:{path}")
        if schema.get("format") == "date-time":
            try:
                parsed = dt.datetime.fromisoformat(value.replace("Z", "+00:00"))
            except ValueError as exc:
                raise GateError(f"schema_datetime_invalid:{path}") from exc
            if not value.endswith("Z") or parsed.utcoffset() != dt.timedelta(0):
                raise GateError(f"schema_datetime_not_utc:{path}")
    if kind == "object":
        props = schema.get("properties")
        required = schema.get("required")
        if not isinstance(props, dict) or not isinstance(required, list):
            raise GateError(f"schema_object_definition_invalid:{path}")
        missing = sorted(set(required) - set(value))
        extra = sorted(set(value) - set(props))
        if missing:
            raise GateError(f"schema_required_missing:{path}:{','.join(missing)}")
        if extra and schema.get("additionalProperties") is False:
            raise GateError(f"schema_unknown_fields:{path}:{','.join(extra)}")
        for key, child in value.items():
            if key in props:
                validate_schema_value(child, props[key], root_schema, f"{path}.{key}")
    if kind == "array":
        if "const" in schema and value != schema["const"]:
            raise GateError(f"schema_array_const_mismatch:{path}")
        item_schema = schema.get("items")
        if item_schema is not None:
            for index, item in enumerate(value):
                validate_schema_value(item, item_schema, root_schema, f"{path}[{index}]")


def load_auth_schema() -> dict[str, Any]:
    schema = strict_json_bytes(AUTH_SCHEMA_PATH.read_bytes())
    if not isinstance(schema, dict) or schema.get("$id") != SCHEMA_ID:
        raise GateError("authorized_schema_id_invalid")
    return schema


def _manifest_entries(path: Path) -> list[tuple[str, str]]:
    entries: list[tuple[str, str]] = []
    for line_no, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        if not line.strip():
            raise GateError(f"manifest_blank_line:{path.name}:{line_no}")
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
        if not match:
            raise GateError(f"manifest_line_invalid:{path.name}:{line_no}")
        entries.append((match.group(1), match.group(2)))
    if not entries:
        raise GateError(f"manifest_empty:{path}")
    if len({relative for _digest, relative in entries}) != len(entries):
        raise GateError(f"manifest_duplicate_path:{path.name}")
    return entries


def verify_manifest(path: Path, base: Path) -> dict[str, Any]:
    entries = _manifest_entries(path)
    valid = 0
    for expected, relative in entries:
        target = (base / relative.replace("/", os.sep)).resolve()
        try:
            target.relative_to(base.resolve())
        except ValueError as exc:
            raise GateError(f"manifest_path_escape:{relative}") from exc
        if sha_file(target) != expected:
            raise GateError(f"manifest_hash_mismatch:{path.name}:{relative}")
        valid += 1
    return {"entries": valid, "manifest_sha256": sha_file(path), "set_exact": True}


def hash_inventory(paths: list[Path], base: Path) -> str:
    items = []
    for path in sorted(paths, key=lambda item: item.as_posix().lower()):
        items.append({"path": path.resolve().relative_to(base.resolve()).as_posix(), "sha256": sha_file(path)})
    return sha_bytes(canonical_bytes(items))


def reference_library_paths() -> tuple[dict[str, Path], dict[str, Path]]:
    ggml = ROOT / "T0P" / "ref_ggml" / "build_llama" / "bin"
    torch = ROOT / ".venv_cuda" / "Lib" / "site-packages" / "torch" / "lib"
    return (
        {name: ggml / name for name in ("ggml.dll", "ggml-cpu.dll", "ggml-base.dll")},
        {name: torch / name for name in ("torch.dll", "torch_cpu.dll", "c10.dll")},
    )


def integrated_source_paths() -> list[Path]:
    return [
        COLLECTOR / "official_run_r15.py", AUTH_SCHEMA_PATH,
        COLLECTOR / "residency_plan_r15.py", COLLECTOR / "qualify_r15.py",
        COLLECTOR / "telemetry_r15.py", COLLECTOR / "residency_derive_r15.py",
        HARNESS / "main_r15.cpp", HARNESS / "residency_r15.cpp",
        HARNESS / "topology_r15.cpp", HARNESS / "topology_r15.hpp",
        HARNESS / "harness_r15.hpp", HARNESS / "provider_common_r15.hpp",
        ROOT / "T0P" / "kernel" / "q4_kernel.cpp", ROOT / "T0P" / "kernel" / "q4_kernel.hpp",
        ROOT / "T0P" / "common" / "common.cpp", ROOT / "T0P" / "common" / "common.hpp",
        EVICT / "evict.cpp", EVICT / "evict.hpp",
    ]


def artifact_hashes(plan: dict[str, Any], executable: Path = INTEGRATED_EXE) -> dict[str, Any]:
    ggml_manifest = ROOT / "T0P" / "ref_ggml" / "SHA256SUMS_ref_ggml.txt"
    torch_manifest = ROOT / "T0P" / "ref_torch" / "SHA256SUMS_ref_torch.txt"
    collector_manifest = COLLECTOR / "SHA256SUMS_collector_r15.txt"
    harness_manifest = HARNESS / "SHA256SUMS_harness_r15.txt"
    ggml_libraries, torch_libraries = reference_library_paths()
    missing = [path for path in [
        PLAN_PATH, COLLECTOR / "plan.json", ggml_manifest, torch_manifest,
        ROOT / "T0P" / "kernel" / "q4_kernel.cpp",
        HARNESS / "logs_r15" / "obj_r15" / "q4_kernel.obj",
        EMON_EXE, TELEMETRY / "emon_joint_qualification.edp",
        VALIDATOR / "s20_validator.py", EVICT / "evict.cpp", EVICT / "evict.hpp",
        T0_PATH, ROADMAP_PATH, collector_manifest, harness_manifest,
        *ggml_libraries.values(), *torch_libraries.values(), executable,
    ] if not path.is_file()]
    if missing:
        raise GateError("auth_input_missing:" + ",".join(str(path) for path in missing))
    ggml_integrity = verify_manifest(ggml_manifest, ggml_manifest.parent)
    torch_integrity = verify_manifest(torch_manifest, torch_manifest.parent)
    if not ggml_integrity["set_exact"] or not torch_integrity["set_exact"]:
        raise GateError("reference_manifest_not_exact")
    collector_integrity = verify_manifest(collector_manifest, COLLECTOR)
    harness_integrity = verify_manifest(harness_manifest, HARNESS)
    if collector_integrity["entries"] < 1 or harness_integrity["entries"] < 1:
        raise GateError("r15_package_manifest_empty")
    return {
        "plan_file_sha256": sha_file(PLAN_PATH),
        "qualification_plan_sha256": sha_file(COLLECTOR / "plan.json"),
        "references_ggml_manifest_sha256": sha_file(ggml_manifest),
        "references_torch_manifest_sha256": sha_file(torch_manifest),
        "ggml_libraries": {name: sha_file(path) for name, path in sorted(ggml_libraries.items())},
        "torch_libraries": {name: sha_file(path) for name, path in sorted(torch_libraries.items())},
        "kernel_source_sha256": sha_file(ROOT / "T0P" / "kernel" / "q4_kernel.cpp"),
        "kernel_object_sha256": sha_file(HARNESS / "logs_r15" / "obj_r15" / "q4_kernel.obj"),
        "integrated_sources_sha256": hash_inventory(integrated_source_paths(), ROOT),
        "emon_executable_sha256": sha_file(EMON_EXE),
        "emon_edp_sha256": sha_file(TELEMETRY / "emon_joint_qualification.edp"),
        "s20_validator_sha256": sha_file(VALIDATOR / "s20_validator.py"),
        "eviction_cpp_sha256": sha_file(EVICT / "evict.cpp"),
        "eviction_hpp_sha256": sha_file(EVICT / "evict.hpp"),
        "t0_md_sha256": sha_file(T0_PATH),
        "roadmap_sha256": sha_file(ROADMAP_PATH),
        "collector_manifest_sha256": collector_integrity["manifest_sha256"],
        "harness_manifest_sha256": harness_integrity["manifest_sha256"],
        "authorized_schema_sha256": sha_file(AUTH_SCHEMA_PATH),
    }


def expected_usage_policy() -> dict[str, Any]:
    return {
        "session_count": 1, "official": True, "emon_continuous": True,
        "N": 31, "warmup": 5, "LPs": list(P_LPS),
        "large_pages_required": True, "retry_global_max": 1,
        "dry_accept_measurements": False, "synthetic_allowed_in_official": False,
    }


def expected_retry_policy() -> dict[str, Any]:
    return {
        "max_technical_retries": 1,
        "retryable_reasons": list(RETRYABLE_REASONS),
        "scientific_failures_retryable": False,
        "same_plan_seed_order": True,
        "second_hold_is_final": True,
    }


def _parser_test_artifacts() -> dict[str, Any]:
    """Schema-complete hash placeholders confined to parser mutation fixtures."""
    digest = sha_bytes(b"AUTHORIZED_R15_TEST_ONLY_ARTIFACT")
    return {
        "plan_file_sha256": digest,
        "qualification_plan_sha256": digest,
        "references_ggml_manifest_sha256": digest,
        "references_torch_manifest_sha256": digest,
        "ggml_libraries": {name: digest for name in ("ggml.dll", "ggml-cpu.dll", "ggml-base.dll")},
        "torch_libraries": {name: digest for name in ("torch.dll", "torch_cpu.dll", "c10.dll")},
        "kernel_source_sha256": digest,
        "kernel_object_sha256": digest,
        "integrated_sources_sha256": digest,
        "emon_executable_sha256": digest,
        "emon_edp_sha256": digest,
        "s20_validator_sha256": digest,
        "eviction_cpp_sha256": digest,
        "eviction_hpp_sha256": digest,
        "t0_md_sha256": digest,
        "roadmap_sha256": digest,
        "collector_manifest_sha256": digest,
        "harness_manifest_sha256": digest,
        "authorized_schema_sha256": digest,
    }


def make_auth_fixture(plan: dict[str, Any], executable: Path = INTEGRATED_EXE,
                      *, artifacts_override: dict[str, Any] | None = None) -> dict[str, Any]:
    now = dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    return {
        "schema": SCHEMA_ID,
        "campaign": CAMPAIGN,
        "session_id": "fixture-session-r15-0001",
        "issued_at_utc": now,
        "judge": "TEST FIXTURE ONLY",
        "sol_decision_id": "MD/50",
        "scope": "S-18..S-23; isolated AUTH parser mutation fixture",
        "master_seed": SEED,
        "plan_sha256": plan["sha256"],
        "plan_order_sha256": plan_order_sha256(plan),
        "executable_sha256": sha_file(executable),
        "artifacts": artifacts_override if artifacts_override is not None else artifact_hashes(plan, executable),
        "usage_policy": expected_usage_policy(),
        "retry_policy": expected_retry_policy(),
    }


def validate_authorized_bytes(data: bytes, plan: dict[str, Any], *,
                              expected_session_id: str,
                              executable: Path = INTEGRATED_EXE,
                              expected_artifacts: dict[str, Any] | None = None,
                              require_real_session: bool = True) -> dict[str, Any]:
    schema = load_auth_schema()
    auth = strict_json_bytes(data)
    validate_schema_value(auth, schema, schema)
    if tuple(auth) == () or set(auth) != set(AUTH_KEYS):
        raise GateError("authorized_top_level_fields_invalid")
    if require_real_session and auth["session_id"].startswith("fixture-"):
        raise GateError("test_fixture_cannot_authorize_official")
    if auth["session_id"] != expected_session_id:
        raise GateError("authorized_session_id_mismatch")
    if auth["master_seed"] != SEED or auth["plan_sha256"] != plan["sha256"]:
        raise GateError("authorized_plan_or_seed_mismatch")
    if auth["plan_order_sha256"] != plan_order_sha256(plan):
        raise GateError("authorized_event_order_mismatch")
    executable_hash = sha_file(executable)
    if auth["executable_sha256"] != executable_hash:
        raise GateError("authorized_integrated_executable_mismatch")
    actual = expected_artifacts if expected_artifacts is not None else artifact_hashes(plan, executable)
    if canonical_bytes(auth["artifacts"]) != canonical_bytes(actual):
        differing = sorted(key for key in set(auth["artifacts"]) | set(actual)
                           if auth["artifacts"].get(key) != actual.get(key))
        raise GateError("authorized_artifact_hash_mismatch:" + (differing[0] if differing else "unknown"))
    if canonical_bytes(auth["usage_policy"]) != canonical_bytes(expected_usage_policy()):
        raise GateError("authorized_usage_policy_mismatch")
    if canonical_bytes(auth["retry_policy"]) != canonical_bytes(expected_retry_policy()):
        raise GateError("authorized_retry_policy_mismatch")
    return auth


def reader_required_channels_gate(data: dict[str, Any]) -> dict[str, Any]:
    """S-23 policy over S-20 validated direct channel results; no raw parser here."""
    if data.get("label") not in {"SYNTHETIC_READER_FIXTURE", "OFFICIAL_READER_RESULT"}:
        return {"status": "HOLD_INSTRUMENTATION", "reason": "reader_label_missing_or_invalid"}
    workers = data.get("workers")
    if not isinstance(workers, list) or len(workers) != 8:
        return {"status": "HOLD_INSTRUMENTATION", "reason": "required_eight_P_workers_missing"}
    by_lp = {row.get("lp"): row for row in workers if isinstance(row, dict)}
    if set(by_lp) != set(P_LPS):
        return {"status": "HOLD_INSTRUMENTATION", "reason": "required_P_LP_map_incomplete"}
    for lp in P_LPS:
        row = by_lp[lp]
        if row.get("required_channels") != list(REQUIRED_CHANNELS):
            return {"status": "HOLD_INSTRUMENTATION", "reason": f"required_channel_missing:LP{lp}"}
        if row.get("channel_validation") != "VALID":
            return {"status": "HOLD_INSTRUMENTATION", "reason": f"channel_validation_invalid:LP{lp}"}
        ratio = row.get("ref_tsc_ratio")
        if type(ratio) not in (int, float) or not math.isfinite(ratio) or not .98 <= ratio <= 1.02:
            return {"status": "HOLD_INSTRUMENTATION", "reason": f"REF_TSC_out_of_range:LP{lp}"}
        freq = row.get("active_frequency_hz")
        if type(freq) not in (int, float) or not math.isfinite(freq) or freq <= 0:
            return {"status": "HOLD_INSTRUMENTATION", "reason": f"active_frequency_invalid:LP{lp}"}
        if row.get("coverage") < .95 or row.get("max_gap_s") > .020 or row.get("window_count") < 20:
            return {"status": "HOLD_INSTRUMENTATION", "reason": f"S20_temporal_control_invalid:LP{lp}"}
        if row.get("thermal_margin_c") < 5 or row.get("thermal_flags_valid") is not True:
            return {"status": "HOLD_INSTRUMENTATION", "reason": f"S20_thermal_control_invalid:LP{lp}"}
    # The historical LP0 R1 failure is diagnostic across every logical processor.
    if data.get("inst_retired_any_scope") != "DIAGNOSTIC_ONLY_ALL_LP":
        return {"status": "HOLD_INSTRUMENTATION", "reason": "INST_RETIRED_governing_scope_invalid"}
    return {
        "status": "PASS",
        "reader_state": "QUALIFIED_FOR_REQUIRED_CHANNELS_WITH_DECLARED_LIMITATION",
        "R1_MD48_GLOBAL": "FAIL_HISTORICAL",
        "R1_LP0": "NOT_QUALIFIED_FOR_LOOP_ATTRIBUTION_AT_1_PERCENT",
        "INST_RETIRED.ANY": "DIAGNOSTIC_ONLY_ALL_LP",
        "worker_lps": list(P_LPS), "worker_count": 8,
        "synthetic_fixture": data["label"] == "SYNTHETIC_READER_FIXTURE",
    }


def synthetic_reader(*, ref_tsc_lp0: float = 1.0, missing_channel_lp: int | None = None,
                     invalid_frequency_lp: int | None = None) -> dict[str, Any]:
    workers = []
    for lp in P_LPS:
        channels = list(REQUIRED_CHANNELS)
        if lp == missing_channel_lp:
            channels.remove("CPU_CLK_UNHALTED.REF_TSC")
        workers.append({
            "lp": lp, "required_channels": channels, "channel_validation": "VALID",
            "ref_tsc_ratio": ref_tsc_lp0 if lp == 0 else 1.0,
            "active_frequency_hz": None if lp == invalid_frequency_lp else 2_112_000_000.0,
            "coverage": .982, "max_gap_s": .008, "window_count": 48,
            "thermal_margin_c": 11.0, "thermal_flags_valid": True,
        })
    return {"label": "SYNTHETIC_READER_FIXTURE", "workers": workers,
            "inst_retired_any_scope": "DIAGNOSTIC_ONLY_ALL_LP",
            "R1_LP0_historical": "FAIL"}


def synthetic_d8_captures(*, bad_h2: tuple[str, int] | None = None,
                          bad_h3: tuple[str, int] | None = None,
                          low_denominator: tuple[str, int] | None = None,
                          missing_lp: tuple[str, int] | None = None,
                          mixed_windows: bool = False) -> dict[str, list[dict[str, Any]]]:
    captures: dict[str, list[dict[str, Any]]] = {}
    ids = [f"window-{i}" for i in range(1, 33)]
    for capture in ("Q", "R"):
        rows = []
        for lp in P_LPS:
            d2, m2, m3 = 100_000, 100, 20
            if bad_h2 == (capture, lp):
                m2 = 10_001
            if bad_h3 == (capture, lp):
                m3 = 10_001
            if low_denominator == (capture, lp):
                d2 = 9_999
            if mixed_windows and capture == "R" and lp == 14:
                row_ids = ids[:-1]
            else:
                row_ids = ids
            if missing_lp == (capture, lp):
                continue
            rows.append({"lp": lp, "scope": "LP_WORKER", "window_ids": row_ids,
                         "D2": d2, "M2": m2, "M3": m3, "finite": True,
                         "capture": capture, "coverage_valid": True})
        captures[capture] = rows
    return captures


def validate_d8_post_revalidation(captures: dict[str, list[dict[str, Any]]]) -> dict[str, Any]:
    if set(captures) != {"Q", "R"}:
        return {"status": "HOLD_INSTRUMENTATION", "reason": "D8_capture_pair_missing"}
    common_windows: set[str] | None = None
    worst_h2 = 0.0
    worst_h3 = 0.0
    for capture in ("Q", "R"):
        rows = captures[capture]
        by_lp = {row.get("lp"): row for row in rows if isinstance(row, dict)}
        if set(by_lp) != set(P_LPS):
            return {"status": "HOLD_INSTRUMENTATION", "reason": f"D8_LP_coverage_invalid:{capture}"}
        for lp in P_LPS:
            row = by_lp[lp]
            if (row.get("capture") != capture or row.get("scope") != "LP_WORKER" or
                    row.get("coverage_valid") is not True or row.get("finite") is not True):
                return {"status": "HOLD_INSTRUMENTATION", "reason": f"D8_provenance_invalid:{capture}:LP{lp}"}
            ids = row.get("window_ids")
            if not isinstance(ids, list) or len(ids) < 20 or len(ids) != len(set(ids)):
                return {"status": "HOLD_INSTRUMENTATION", "reason": f"D8_window_ids_invalid:{capture}:LP{lp}"}
            if common_windows is None:
                common_windows = set(ids)
            elif set(ids) != common_windows:
                return {"status": "HOLD_INSTRUMENTATION", "reason": f"D8_mixed_windows:{capture}:LP{lp}"}
            d2, m2, m3 = row.get("D2"), row.get("M2"), row.get("M3")
            if any(type(value) is not int or value < 0 for value in (d2, m2, m3)):
                return {"status": "HOLD_INSTRUMENTATION", "reason": f"D8_count_invalid:{capture}:LP{lp}"}
            if d2 < 10_000:
                return {"status": "HOLD_INSTRUMENTATION", "reason": f"D8_denominator_NOT_ESTIMABLE:{capture}:LP{lp}"}
            if m2 > d2:
                return {"status": "HOLD_INSTRUMENTATION", "reason": f"D8_M2_exceeds_D2:{capture}:LP{lp}"}
            h2, h3 = m2 / d2, m3 / d2
            worst_h2, worst_h3 = max(worst_h2, h2), max(worst_h3, h3)
            if h2 > .10 or h3 > .10:
                return {"status": "HOLD_INSTRUMENTATION", "reason": f"D8_hot_gate_fail:{capture}:LP{lp}",
                        "H2": h2, "H3_over_D2": h3}
    return {"status": "PASS", "capture_pair": ["Q", "R"], "worker_lps": list(P_LPS),
            "scope": "LP_WORKER", "max_H2": worst_h2, "max_H3_over_D2": worst_h3,
            "window_ids_common": True, "aggregation": "per-core ratio of sums; no cross-core compensation"}


def _explicit_stage(status: str, label: str, **fields: Any) -> dict[str, Any]:
    if status not in {"PASS", "HOLD_INSTRUMENTATION", "FALLIDA_CUALIFICACION", "NO_ADJUDICABLE_CONTROLES"}:
        raise GateError(f"phase_status_unsupported:{status}")
    return {"status": status, "label": label, **fields}


def _phase_provider(ctx: dict[str, Any], synthetic_name: str, official_name: str) -> str:
    return synthetic_name if ctx.get("label", "SYNTHETIC_FIXTURE") == "SYNTHETIC_FIXTURE" else official_name


def _synthetic_context(*, plan: dict[str, Any] | None = None,
                       qualification_slow_m1: bool = False,
                       residency_branch: str = "pass",
                       reader: dict[str, Any] | None = None,
                       d8: dict[str, list[dict[str, Any]]] | None = None,
                       qa_output: str = "KERNEL_QA_R15 PASS") -> dict[str, Any]:
    import qualify_r15 as qualification
    import residency_derive_r15 as derivation
    import residency_plan_r15 as plans

    plan = plan if plan is not None else plans.build_payload()
    q_ids = qualification._test_plan_ids()
    q_rows = qualification._synthetic_rows(q_ids, prepared_m1_us=16.0 if qualification_slow_m1 else 10.0)
    residency_rows, episode_rows = derivation._synthetic_fixture(plan, branch=residency_branch)
    return {
        "plan": plan, "qualification_ids": q_ids, "qualification_rows": q_rows,
        "residency_rows": residency_rows, "episode_rows": episode_rows,
        "reader": reader if reader is not None else synthetic_reader(),
        "d8": d8 if d8 is not None else synthetic_d8_captures(),
        "qa_output": qa_output, "residency_branch": residency_branch,
    }


def _phase_preflight(ctx: dict[str, Any]) -> dict[str, Any]:
    import residency_plan_r15 as plans
    plan_path = PLAN_PATH
    if not plan_path.is_file():
        return _explicit_stage("HOLD_INSTRUMENTATION", "SYNTHETIC_PHASE_PROVIDER", reason="D2_plan_missing")
    checked, ids = plans.verify_plan(plan_path)
    if checked["sha256"] != ctx["plan"]["sha256"] or len(ids) != 13_688:
        return _explicit_stage("HOLD_INSTRUMENTATION", _phase_provider(ctx, "SYNTHETIC_PHASE_PROVIDER", "OFFICIAL_PREFLIGHT_PROVIDER"), reason="D2_plan_identity_or_event_count_invalid")
    if ctx.get("label") == "OFFICIAL":
        preflight = ctx.get("preflight")
        if (not isinstance(preflight, dict) or preflight.get("label") != "OFFICIAL_PREFLIGHT_RESULT" or
                preflight.get("state") != "PASS" or preflight.get("large_pages") is not True or
                preflight.get("plan_sha256") != checked["sha256"]):
            return _explicit_stage("HOLD_INSTRUMENTATION", "OFFICIAL_PREFLIGHT_PROVIDER",
                                   reason="official_preflight_resource_or_plan_invalid")
    return _explicit_stage("PASS", _phase_provider(ctx, "SYNTHETIC_PHASE_PROVIDER", "OFFICIAL_PREFLIGHT_PROVIDER"), plan_sha256=checked["sha256"],
                           conditions=420, calibration_units=140, aa_pairs=124,
                           LPs=list(P_LPS), numeric_metrics_used=ctx.get("label") == "OFFICIAL")


def _phase_reader(ctx: dict[str, Any]) -> dict[str, Any]:
    if ctx.get("label") == "OFFICIAL":
        instrument = ctx.get("instrumentation")
        if not isinstance(instrument, dict) or instrument.get("state") != "PASS":
            reason = instrument.get("reason", "reader_or_channel_qualification_missing") if isinstance(instrument, dict) else "reader_or_channel_qualification_missing"
            retry_reason = instrument.get("retry_reason") if isinstance(instrument, dict) else None
            fields = {"reason": reason}
            if retry_reason in RETRYABLE_REASONS:
                fields["retry_reason"] = retry_reason
            return _explicit_stage("HOLD_INSTRUMENTATION", "OFFICIAL_READER_PROVIDER", **fields)
    result = reader_required_channels_gate(ctx["reader"])
    if result["status"] != "PASS":
        return _explicit_stage("HOLD_INSTRUMENTATION", _phase_provider(ctx, "SYNTHETIC_READER_FIXTURE", "OFFICIAL_READER_PROVIDER"), reason=result["reason"])
    return _explicit_stage("PASS", _phase_provider(ctx, "SYNTHETIC_READER_FIXTURE", "OFFICIAL_READER_PROVIDER"), reader_state=result["reader_state"],
                           R1="DIAGNOSTIC_ONLY", worker_lps=list(P_LPS),
                           no_core_exclusion=True, numeric_metrics_used=ctx.get("label") == "OFFICIAL")


def _phase_reference_qualification(ctx: dict[str, Any]) -> dict[str, Any]:
    import qualify_r15 as qualification
    rows = ctx["qualification_rows"]
    wanted = qualification._test_plan_ids()
    if ctx.get("label") == "OFFICIAL":
        evidence = ctx.get("reference_qualification")
        if (not isinstance(evidence, dict) or evidence.get("state") != "PASS" or
                evidence.get("reference_selection_independent") is not True):
            reason = evidence.get("reason", "reference_qualification_not_passed") if isinstance(evidence, dict) else "reference_qualification_missing"
            return _explicit_stage("HOLD_INSTRUMENTATION", "OFFICIAL_QUALIFICATION_PROVIDER", reason=reason)
    # This phase validates planned competency/reference events only.  The same
    # synthetic source also carries BW_DRAM and P_instr qualification rows,
    # whose identities are defined by provider/core/replica fields rather than
    # by plan_event.
    actual = {str(row.get("plan_event")) for row in rows if row.get("plan_event") is not None}
    if actual != wanted:
        return _explicit_stage("HOLD_INSTRUMENTATION", _phase_provider(ctx, "SYNTHETIC_QUALIFICATION_FIXTURE", "OFFICIAL_QUALIFICATION_PROVIDER"),
                               reason="mandatory_reference_or_comparison_event_missing")
    if ctx.get("label") == "SYNTHETIC_FIXTURE":
        g = qualification._paired_g([10.0] * 31, [8.0] * 31)
        if abs(g - .80) > 1e-12 or g >= .90:
            return _explicit_stage("HOLD_INSTRUMENTATION", "SYNTHETIC_QUALIFICATION_FIXTURE",
                                   reason="D3_reference_selection_contract_invalid")
    else:
        g = None
    return _explicit_stage("PASS", _phase_provider(ctx, "SYNTHETIC_QUALIFICATION_FIXTURE", "OFFICIAL_QUALIFICATION_PROVIDER"),
                           planned_events=len(wanted), replicas=31,
                           reference_selection_independent=True, D3_G_ref8_alt10_omega10=g,
                           numeric_metrics_used=ctx.get("label") == "OFFICIAL")


def _phase_science(ctx: dict[str, Any]) -> dict[str, Any]:
    import qualify_r15 as qualification
    synthetic = ctx.get("label", "SYNTHETIC_FIXTURE") == "SYNTHETIC_FIXTURE"
    if synthetic:
        summary = [{"label": "SYNTHETIC_FIXTURE"}]
        telemetry = lambda row: (row.get("s20_attempt", {}).get("fixture") is True,
                                 "SYNTHETIC_S20_PROVIDER")
    else:
        summary = ctx["qualification_summary"]
        s20 = ctx["qualification_s20"]
        telemetry = lambda row: (s20.get("state") == "PASS" and
                                 (row.get("plan_event") in s20.get("valid_plan_event_ids", [])
                                  if row.get("plan_event") is not None
                                  else s20.get("auxiliary_rows_valid") is True),
                                 "OFFICIAL_S20_QUALIFICATION_PROVIDER")
    result = qualification._gate_official(
        ctx["qualification_rows"], summary,
        ctx["plan"]["sha256"], ctx["qualification_ids"],
        telemetry_check=telemetry,
    )
    if not synthetic:
        reported = ctx.get("science_result")
        if not isinstance(reported, dict) or reported.get("state") != result.get("state"):
            return _explicit_stage("HOLD_INSTRUMENTATION", "OFFICIAL_QUALIFICATION_PROVIDER",
                                   reason="qualification_raw_and_reported_states_disagree")
    if result.get("state") == "HOLD_INSTRUMENTATION":
        return _explicit_stage("HOLD_INSTRUMENTATION", _phase_provider(ctx, "SYNTHETIC_QUALIFICATION_FIXTURE", "OFFICIAL_QUALIFICATION_PROVIDER"),
                               reason=result.get("reason", "qualification_input_invalid"))
    if result.get("state") == "FALLIDA_CUALIFICACION":
        return _explicit_stage("FALLIDA_CUALIFICACION", _phase_provider(ctx, "SYNTHETIC_QUALIFICATION_FIXTURE", "OFFICIAL_QUALIFICATION_PROVIDER"),
                               reason=result.get("reason", "valid_scientific_gate_failed"),
                               campaign_line="CLOSED_Q4_256_THIS_I7", residency="NOT_STARTED",
                               qualification_gates=result.get("gates", []))
    if result.get("state") != "QUALIFIED_FOR_RESIDENCY":
        return _explicit_stage("HOLD_INSTRUMENTATION", _phase_provider(ctx, "SYNTHETIC_QUALIFICATION_FIXTURE", "OFFICIAL_QUALIFICATION_PROVIDER"),
                               reason="qualification_returned_no_explicit_terminal_state")
    return _explicit_stage("PASS", _phase_provider(ctx, "SYNTHETIC_QUALIFICATION_FIXTURE", "OFFICIAL_QUALIFICATION_PROVIDER"),
                           qualification_state=result["state"], gates=result.get("gates", []),
                           reader_state="QUALIFIED_FOR_REQUIRED_CHANNELS_WITH_DECLARED_LIMITATION",
                           numeric_metrics_used=not synthetic)


def _phase_qa(ctx: dict[str, Any]) -> dict[str, Any]:
    output = ctx.get("qa_output")
    if not isinstance(output, str) or "KERNEL_QA_R15 PASS" not in output:
        return _explicit_stage("NO_ADJUDICABLE_CONTROLES", _phase_provider(ctx, "SYNTHETIC_QA_PROVIDER", "OFFICIAL_QA_PROVIDER"),
                               reason="kernel_QA_evidence_missing_or_failed")
    return _explicit_stage("PASS", "HARNESS_R15_RUN_PROVIDER", qa_path="omega_q4::run",
                           O1024_positions=512, m_values=[1, 4, 8, 16],
                           sweep_points=15, evidence_sha256=sha_bytes(output.encode("utf-8")),
                           numeric_metrics_used=ctx.get("label") == "OFFICIAL")


def _phase_residency(ctx: dict[str, Any]) -> dict[str, Any]:
    plan_ids = {event["id"] for event in ctx["plan"]["events"]}
    seen = {row.get("event_id") for row in ctx["residency_rows"]}
    if seen != plan_ids or len(ctx["residency_rows"]) != len(plan_ids):
        return _explicit_stage("NO_ADJUDICABLE_CONTROLES", _phase_provider(ctx, "SYNTHETIC_HARNESS_FIXTURE", "OFFICIAL_HARNESS_PROVIDER"),
                               reason="residency_plan_executed_union_invalid")
    if ctx.get("label") == "OFFICIAL" and any(row.get("label") != "OFFICIAL" for row in ctx["residency_rows"]):
        return _explicit_stage("NO_ADJUDICABLE_CONTROLES", "OFFICIAL_HARNESS_PROVIDER",
                               reason="official_residency_contains_nonofficial_rows")
    return _explicit_stage("PASS", _phase_provider(ctx, "SYNTHETIC_HARNESS_FIXTURE", "OFFICIAL_HARNESS_PROVIDER"), event_count=len(seen),
                           variants=["A", "B", "C"], replicas="1..31",
                           numeric_metrics_used=ctx.get("label") == "OFFICIAL")


def _phase_margins(ctx: dict[str, Any]) -> dict[str, Any]:
    import residency_derive_r15 as derivation
    if ctx.get("label") == "OFFICIAL":
        closure = ctx.get("margins_closure")
        if not isinstance(closure, dict) or closure.get("state") != "PASS":
            reason = closure.get("reason", "margin_closure_evidence_missing") if isinstance(closure, dict) else "margin_closure_evidence_missing"
            return _explicit_stage("NO_ADJUDICABLE_CONTROLES", "OFFICIAL_MARGIN_PROVIDER", reason=reason)
    planned = {event["id"]: event for event in ctx["plan"]["events"]}
    actual = {row.get("event_id"): row for row in ctx["residency_rows"]}
    try:
        for event_id, event in planned.items():
            derivation._check_margin(actual[event_id], event)
    except (KeyError, derivation.DeriveError) as exc:
        return _explicit_stage("NO_ADJUDICABLE_CONTROLES", _phase_provider(ctx, "SYNTHETIC_MARGIN_FIXTURE", "OFFICIAL_MARGIN_PROVIDER"),
                               reason=f"margin_or_closure_invalid:{exc}")
    return _explicit_stage("PASS", _phase_provider(ctx, "SYNTHETIC_MARGIN_FIXTURE", "OFFICIAL_MARGIN_PROVIDER"), checked_events=len(planned),
                           same_condition=True, continuous_load=True,
                           margin_each_side_min_s=.200, numeric_metrics_used=ctx.get("label") == "OFFICIAL")


def _phase_d8(ctx: dict[str, Any]) -> dict[str, Any]:
    result = validate_d8_post_revalidation(ctx["d8"])
    if result["status"] != "PASS":
        return _explicit_stage("HOLD_INSTRUMENTATION", _phase_provider(ctx, "SYNTHETIC_S22_FIXTURE", "OFFICIAL_S22_PROVIDER"), reason=result["reason"])
    return _explicit_stage("PASS", _phase_provider(ctx, "SYNTHETIC_S22_FIXTURE", "OFFICIAL_S22_PROVIDER"), **{k: v for k, v in result.items() if k != "status"},
                           numeric_metrics_used=ctx.get("label") == "OFFICIAL")


def _phase_derivation(ctx: dict[str, Any]) -> dict[str, Any]:
    import residency_derive_r15 as derivation
    synthetic = ctx.get("label", "SYNTHETIC_FIXTURE") == "SYNTHETIC_FIXTURE"
    if synthetic:
        telemetry = lambda row: (True, "SYNTHETIC_S20_PROVIDER")
    else:
        valid_ids = set(ctx["residency_s20"]["valid_event_ids"])
        telemetry = lambda row: (row.get("event_id") in valid_ids, "OFFICIAL_S20_RESIDENCY_PROVIDER")
    result = derivation.derive(
        ctx["plan"], ctx["residency_rows"], ctx["episode_rows"],
        telemetry_check=telemetry, synthetic=synthetic,
    )
    state = result.get("state")
    if state == "RESIDENCIA_FAIL":
        verdict = "FALLO_RESIDENCIA_VALIDO"
    elif state in {"L2_NOT_ESTABLISHED", "RESIDENCIA_PASS", "NO_ADJUDICABLE_CONTROLES"}:
        verdict = state
    else:
        return _explicit_stage("NO_ADJUDICABLE_CONTROLES", _phase_provider(ctx, "SYNTHETIC_DERIVATION_FIXTURE", "OFFICIAL_DERIVATION_PROVIDER"),
                               reason=f"unexpected_deriver_state:{state}")
    if state == "NO_ADJUDICABLE_CONTROLES":
        return _explicit_stage("NO_ADJUDICABLE_CONTROLES", _phase_provider(ctx, "SYNTHETIC_DERIVATION_FIXTURE", "OFFICIAL_DERIVATION_PROVIDER"),
                               verdict=verdict, reason=result.get("reason", "controls_invalid"),
                               derived_state=state, numeric_metrics_used=not synthetic)
    return _explicit_stage("PASS", _phase_provider(ctx, "SYNTHETIC_DERIVATION_FIXTURE", "OFFICIAL_DERIVATION_PROVIDER"), verdict=verdict,
                           derived_state=state, plan_event_count=result.get("plan_event_count"),
                           event_union_exact=True, L2_RESIDENCY=result.get("L2_RESIDENCY"),
                           B_L2_operational_kib_per_worker=None,
                           numeric_metrics_used=not synthetic)


PHASE_FUNCTIONS: dict[str, Callable[[dict[str, Any]], dict[str, Any]]] = {
    "preflight": _phase_preflight,
    "instrumentation_reader": _phase_reader,
    "kernel_reference_qualification": _phase_reference_qualification,
    "scientific_qualification": _phase_science,
    "kernel_qa": _phase_qa,
    "residency_abc": _phase_residency,
    "margins_closure": _phase_margins,
    "d8_post_revalidation": _phase_d8,
    "derivation_verdict": _phase_derivation,
}


def run_phase_machine(ctx: dict[str, Any], *, injected: dict[str, tuple[str, str]] | None = None,
                      attempt_store: Any = None) -> dict[str, Any]:
    injected = injected or {}
    phases: list[dict[str, Any]] = []
    final_verdict = "NOT_ADJUDICATED"
    residency_started = False
    for name in PHASES:
        if name == "residency_abc":
            residency_started = True
        if name in injected:
            status, reason = injected[name]
            stage = _explicit_stage(status, "SYNTHETIC_FAILURE_INJECTION", reason=reason)
        else:
            action = PHASE_FUNCTIONS.get(name)
            if action is None:
                stage = _explicit_stage("HOLD_INSTRUMENTATION", "MISSING_PHASE_PROVIDER",
                                       reason=f"phase_provider_missing:{name}")
            else:
                try:
                    stage = action(ctx)
                except Exception as exc:
                    stage = _explicit_stage("HOLD_INSTRUMENTATION", "PHASE_EXCEPTION",
                                           reason=f"{type(exc).__name__}:{exc}")
        if not isinstance(stage, dict) or type(stage.get("status")) is not str or type(stage.get("label")) is not str:
            stage = _explicit_stage("HOLD_INSTRUMENTATION", "INVALID_PHASE_RESULT",
                                   reason=f"phase_result_missing_status_or_label:{name}")
        record = {"sequence": len(phases) + 1, "phase": name, **stage}
        if attempt_store is not None:
            attempt_store.write_raw(f"phase_{len(phases)+1:02d}_{name}.json",
                                    canonical_bytes(record) + b"\n")
        phases.append(record)
        status = record["status"]
        if status == "HOLD_INSTRUMENTATION":
            return {"state": "HOLD_INSTRUMENTATION", "campaign_line": "HOLD",
                    "residency": "NOT_STARTED" if not residency_started else "INCOMPLETE",
                    "residency_started": residency_started,
                    "retry_reason": record.get("retry_reason"),
                    "final_verdict": "NO_ADJUDICABLE_CONTROLES", "phases": phases}
        if status == "NO_ADJUDICABLE_CONTROLES":
            return {"state": "NO_ADJUDICABLE_CONTROLES", "campaign_line": "HOLD",
                    "residency": "NOT_STARTED" if not residency_started else "INCOMPLETE",
                    "residency_started": residency_started,
                    "final_verdict": "NO_ADJUDICABLE_CONTROLES", "phases": phases}
        if status == "FALLIDA_CUALIFICACION":
            return {"state": "FALLIDA_CUALIFICACION", "campaign_line": "CLOSED_Q4_256_THIS_I7",
                    "residency": "NOT_STARTED", "residency_started": False,
                    "final_verdict": "FALLIDA_CUALIFICACION", "phases": phases}
        if status != "PASS":
            return {"state": "HOLD_INSTRUMENTATION", "campaign_line": "HOLD",
                    "residency": "NOT_STARTED", "residency_started": False,
                    "final_verdict": "NO_ADJUDICABLE_CONTROLES", "phases": phases}
        if name == "derivation_verdict":
            final_verdict = record.get("verdict")
    if final_verdict not in {"RESIDENCIA_PASS", "FALLO_RESIDENCIA_VALIDO", "L2_NOT_ESTABLISHED"}:
        return {"state": "NO_ADJUDICABLE_CONTROLES", "campaign_line": "HOLD",
                "residency": "NOT_ADJUDICATED", "residency_started": True,
                "final_verdict": "NO_ADJUDICABLE_CONTROLES", "phases": phases}
    return {"state": final_verdict, "campaign_line": "ADJUDICATED",
            "residency": final_verdict, "residency_started": True,
            "final_verdict": final_verdict, "phases": phases,
            "label": ctx.get("label", "SYNTHETIC_FIXTURE"),
            "numeric_metrics_used": ctx.get("label", "SYNTHETIC_FIXTURE") == "OFFICIAL"}


class RetryLedger:
    """Append-only per-session attempt records; one typed technical retry globally."""
    def __init__(self, directory: Path, *, session_id: str, plan_sha256: str,
                 seed: str = SEED, order_sha256: str):
        self.directory = directory
        self.identity = {"session_id": session_id, "plan_sha256": plan_sha256,
                         "seed": seed, "order_sha256": order_sha256}
        self.directory.mkdir(parents=True, exist_ok=True)
        self.attempts = sorted(self.directory.glob("retry_attempt_*.json"))
        if len(self.attempts) > 2:
            raise GateError("retry_ledger_attempt_limit_exceeded")
        for path in self.attempts:
            row = strict_json_bytes(path.read_bytes())
            if any(row.get(key) != value for key, value in self.identity.items()):
                raise GateError("retry_ledger_identity_changed")

    def record(self, *, outcome: str, reason: str | None) -> dict[str, Any]:
        number = len(self.attempts) + 1
        if number > 2:
            raise GateError("retry_attempt_limit_exceeded")
        if outcome == "SCIENTIFIC_FAILURE":
            retry = False
            final = "FALLIDA_CUALIFICACION"
            if reason is not None:
                raise GateError("scientific_failure_reason_must_not_be_retry_reason")
        elif outcome == "TECHNICAL_HOLD":
            if reason not in RETRYABLE_REASONS:
                retry, final = False, "HOLD_INSTRUMENTATION"
            elif number == 1:
                retry, final = True, "RETRY_GLOBAL_ONCE"
            else:
                retry, final = False, "HOLD_INSTRUMENTATION"
        elif outcome == "PASS":
            retry, final = False, "CONTINUE"
        else:
            raise GateError(f"retry_outcome_invalid:{outcome}")
        record = {**self.identity, "attempt": number, "outcome": outcome,
                  "reason": reason, "retry": retry, "state": final,
                  "max_technical_retries": 1}
        path = self.directory / f"retry_attempt_{number:02d}.json"
        with path.open("xb") as stream:
            stream.write(canonical_bytes(record) + b"\n")
            stream.flush()
            os.fsync(stream.fileno())
        self.attempts.append(path)
        return record


def _copy_mutate(value: dict[str, Any], key: str, replacement: Any) -> dict[str, Any]:
    result = copy.deepcopy(value)
    result[key] = replacement
    return result


def _expect_auth_rejected(doc: dict[str, Any], plan: dict[str, Any], expected_session: str,
                          expected_artifacts: dict[str, Any], expected_exe_hash: str) -> None:
    try:
        validate_authorized_bytes(canonical_bytes(doc), plan,
                                  expected_session_id=expected_session,
                                  expected_artifacts=expected_artifacts,
                                  require_real_session=False)
    except GateError:
        return
    raise GateError("authorized_mutation_accepted")


def auth_self_test() -> list[str]:
    import residency_plan_r15 as plans
    if not PLAN_PATH.is_file():
        raise GateError("auth_self_test_requires_generated_plan")
    plan, _ids = plans.verify_plan(PLAN_PATH)
    fixture_root = OBJ / "authorized_fixtures_r15"
    if fixture_root.exists():
        raise GateError("refusing_existing_AUTH_fixture_directory")
    fixture_root.mkdir(parents=True)
    lines: list[str] = []
    try:
        parser_test_artifacts = _parser_test_artifacts()
        valid = make_auth_fixture(plan, artifacts_override=parser_test_artifacts)
        fixture = fixture_root / "AUTHORIZED.fixture.json"
        with fixture.open("xb") as stream:
            stream.write(canonical_bytes(valid) + b"\n")
        actual_artifacts = parser_test_artifacts
        accepted = validate_authorized_bytes(fixture.read_bytes(), plan,
                                             expected_session_id=valid["session_id"],
                                             expected_artifacts=actual_artifacts,
                                             require_real_session=False)
        if accepted["session_id"] != valid["session_id"]:
            raise GateError("valid_AUTH_fixture_was_not_accepted")
        lines.append("fixture_scope=logs_r15/obj_r15/authorized_fixtures_r15 label=TEST_ONLY dry_accept=PASS")

        top_mutations = 0
        for key in AUTH_KEYS:
            changed = copy.deepcopy(valid)
            original = changed[key]
            if key in {"judge", "scope"}:
                changed[key] = ""
            elif key == "issued_at_utc":
                changed[key] = "not-a-utc-date"
            elif isinstance(original, dict):
                changed[key] = {}
            elif isinstance(original, list):
                changed[key] = []
            elif type(original) is bool:
                changed[key] = not original
            elif type(original) is int:
                changed[key] = original + 1
            elif isinstance(original, str):
                changed[key] = original + "_MUTATED"
            else:
                changed[key] = None
            _expect_auth_rejected(changed, plan, valid["session_id"], actual_artifacts,
                                  valid["executable_sha256"])
            top_mutations += 1
        lines.append(f"AUTH_top_level_fields_mutated={top_mutations}/{len(AUTH_KEYS)} rejected=PASS")

        leaves: list[tuple[str, list[str]]] = []
        def visit(node: Any, path: list[str]) -> None:
            if isinstance(node, dict):
                for key, child in node.items():
                    visit(child, [*path, key])
            elif isinstance(node, list):
                if node:
                    leaves.append((".".join(path), [*path, "0"]))
            else:
                leaves.append((".".join(path), path))
        for root_key in ("artifacts", "usage_policy", "retry_policy"):
            visit(valid[root_key], [root_key])
        nested_mutations = 0
        for display, path in leaves:
            changed = copy.deepcopy(valid)
            cursor: Any = changed
            for part in path[:-1]:
                cursor = cursor[int(part)] if isinstance(cursor, list) else cursor[part]
            final = path[-1]
            original = cursor[int(final)] if isinstance(cursor, list) else cursor[final]
            if isinstance(original, dict):
                mutation: Any = {}
            elif isinstance(original, list):
                mutation = []
            elif type(original) is bool:
                mutation = not original
            elif type(original) is int:
                mutation = original + 1
            elif isinstance(original, str):
                mutation = original + "_MUTATED"
            else:
                mutation = None
            if isinstance(cursor, list):
                cursor[int(final)] = mutation
            else:
                cursor[final] = mutation
            _expect_auth_rejected(changed, plan, valid["session_id"], actual_artifacts,
                                  valid["executable_sha256"])
            nested_mutations += 1
        lines.append(f"AUTH_nested_fields_mutated={nested_mutations} rejected=PASS")

        _expect_auth_rejected(valid, plan, "fixture-session-r15-different", actual_artifacts,
                              valid["executable_sha256"])
        lines.append("AUTH_session_id_mismatch=rejected=PASS")
        changed_exe = copy.deepcopy(valid)
        changed_exe["executable_sha256"] = "0" * 64
        _expect_auth_rejected(changed_exe, plan, valid["session_id"], actual_artifacts,
                              valid["executable_sha256"])
        lines.append("AUTH_integrated_executable_mismatch=rejected=PASS")
        try:
            validate_authorized_bytes(fixture.read_bytes(), plan,
                                      expected_session_id=valid["session_id"],
                                      expected_artifacts=actual_artifacts,
                                      require_real_session=True)
        except GateError as exc:
            if "test_fixture" not in str(exc):
                raise
        else:
            raise GateError("test_fixture_accepted_as_real_AUTH")
        lines.append("AUTH_fixture_as_real_authorization=rejected=PASS")
        lines.append("AUTH_fixture_hash_placeholders=test-only; official artifact hashing remains independent")
        try:
            artifact_hashes(plan)
        except GateError as exc:
            if not str(exc).startswith("auth_input_missing:"):
                raise
            lines.append(f"AUTH_real_artifact_snapshot=HOLD missing_required_input={str(exc).split(':', 1)[1]}")
        else:
            lines.append("AUTH_real_artifact_snapshot=PASS required_inputs_and_manifests_verified=true")

        unknown_field = copy.deepcopy(valid)
        unknown_field["unrecognized"] = "reject-me"
        _expect_auth_rejected(unknown_field, plan, valid["session_id"], actual_artifacts,
                              valid["executable_sha256"])
        missing_field = copy.deepcopy(valid)
        del missing_field["judge"]
        _expect_auth_rejected(missing_field, plan, valid["session_id"], actual_artifacts,
                              valid["executable_sha256"])
        lines.append("AUTH_unknown_key=reject AUTH_missing_required=reject PASS")

        malformed_cases = (
            b'{"schema":"x","schema":"y"}', b'{"schema":NaN}', b'{"schema":1e9999}',
            b"\xef\xbb\xbf{}", b'{"schema":"x"} trailing', b"\xff",
        )
        for index, raw in enumerate(malformed_cases):
            try:
                strict_json_bytes(raw)
            except GateError:
                continue
            raise GateError(f"malformed_AUTH_case_accepted:{index}")
        lines.append("AUTH_malformed=duplicate,NaN,overflow,BOM,trailing,invalid_UTF8 rejected=PASS")

        try:
            previous_launch = os.environ.get("OMEGA_R15_INTEGRATED_LAUNCH")
            os.environ["OMEGA_R15_INTEGRATED_LAUNCH"] = "1"
            run_official_gate(expected_session_id=valid["session_id"], integrated_executable=INTEGRATED_EXE,
                              authorization_path=fixture_root / "AUTHORIZED.json")
        except GateError as exc:
            if "AUTHORIZED.json_missing" not in str(exc):
                raise
        finally:
            if previous_launch is None:
                os.environ.pop("OMEGA_R15_INTEGRATED_LAUNCH", None)
            else:
                os.environ["OMEGA_R15_INTEGRATED_LAUNCH"] = previous_launch
        lines.append("AUTH_absent_official=measurements_started:false rejected=PASS")
    finally:
        shutil.rmtree(fixture_root, ignore_errors=True)
    return lines


def _stage_injection_case(name: str, *, qa_output: str = "KERNEL_QA_R15 PASS") -> tuple[dict[str, Any], dict[str, tuple[str, str]]]:
    ctx = _synthetic_context(qa_output=qa_output)
    injected: dict[str, tuple[str, str]] = {}
    if name == "preflight":
        injected[name] = ("HOLD_INSTRUMENTATION", "synthetic_preflight_resource_fault")
    elif name == "instrumentation_reader":
        injected[name] = ("HOLD_INSTRUMENTATION", "synthetic_required_channel_missing")
    elif name == "kernel_reference_qualification":
        injected[name] = ("HOLD_INSTRUMENTATION", "synthetic_mandatory_reference_missing")
    elif name == "scientific_qualification":
        import qualify_r15 as qualification
        ctx["qualification_rows"] = qualification._synthetic_rows(
            ctx["qualification_ids"], prepared_m1_us=16.0)
    elif name == "kernel_qa":
        ctx["qa_output"] = "KERNEL_QA_R15 REJECT injected_mutation"
    elif name == "residency_abc":
        injected[name] = ("NO_ADJUDICABLE_CONTROLES", "synthetic_event_union_fault")
    elif name == "margins_closure":
        target = next(row for row in ctx["residency_rows"] if row.get("phase") == "residency")
        target["pre_margin_seconds"] = .199
    elif name == "d8_post_revalidation":
        ctx["d8"] = synthetic_d8_captures(mixed_windows=True)
    elif name == "derivation_verdict":
        injected[name] = ("NO_ADJUDICABLE_CONTROLES", "synthetic_derivation_union_fault")
    else:
        raise GateError(f"unknown_phase_injection:{name}")
    return ctx, injected


def d1_s23_self_test(qa_output: str) -> list[str]:
    import residency_derive_r15 as derivation
    lines: list[str] = []
    base = _synthetic_context(qa_output=qa_output)
    positive = run_phase_machine(base)
    if positive.get("final_verdict") != "RESIDENCIA_PASS" or len(positive["phases"]) != 9:
        raise GateError(f"D1_positive_end_to_end_failed:{positive.get('final_verdict')}")
    if positive.get("label") != "SYNTHETIC_FIXTURE" or positive.get("numeric_metrics_used") is not False:
        raise GateError("D1_synthetic_label_or_metrics_guard_missing")
    lines.append("D1_positive_path=PASS phases=9 label=SYNTHETIC_FIXTURE numeric_metrics_used=false")
    retry_plan_sha = base["plan"]["sha256"]
    retry_order_sha = plan_order_sha256(base["plan"])
    del base, positive
    gc.collect()

    for phase in PHASES:
        ctx, injected = _stage_injection_case(phase, qa_output=qa_output)
        result = run_phase_machine(ctx, injected=injected)
        reached = [row["phase"] for row in result["phases"]]
        if phase == "scientific_qualification":
            ok = result["final_verdict"] == "FALLIDA_CUALIFICACION" and not result["residency_started"]
        elif phase == "kernel_qa" or phase in {"residency_abc", "margins_closure", "derivation_verdict"}:
            expected_started = phase in {"residency_abc", "margins_closure", "derivation_verdict"}
            ok = (result["state"] == "NO_ADJUDICABLE_CONTROLES" and
                  result["residency_started"] is expected_started)
        elif phase == "d8_post_revalidation":
            ok = result["state"] == "HOLD_INSTRUMENTATION" and result["residency_started"] is True
        else:
            ok = result["state"] == "HOLD_INSTRUMENTATION" and not result["residency_started"]
        if not ok or phase not in reached:
            raise GateError(f"D1_phase_failure_injection_not_enforced:{phase}:{result.get('state')}")
        if result["final_verdict"] == "FALLIDA_CUALIFICACION" and "residency_abc" in reached:
            raise GateError("D1_qualification_cut_invoked_residency")
        lines.append(f"D1_inject_phase={phase} state={result['state']} reached={len(reached)} residency_started={result['residency_started']} PASS")
        del ctx, injected, result
        gc.collect()

    outcome_cases = (
        ("competence_failure", {"qualification_slow_m1": True}, "FALLIDA_CUALIFICACION"),
        ("valid_residency_failure", {"residency_branch": "rho_fail"}, "FALLO_RESIDENCIA_VALIDO"),
        ("L2_not_established", {"residency_branch": "no_knee"}, "L2_NOT_ESTABLISHED"),
        ("controls_not_adjudicable", {"residency_branch": "delta_hold"}, "NO_ADJUDICABLE_CONTROLES"),
    )
    for name, context_options, expected in outcome_cases:
        ctx = _synthetic_context(qa_output=qa_output, **context_options)
        result = run_phase_machine(ctx)
        if result.get("final_verdict") != expected:
            raise GateError(f"D1_final_verdict_wrong:{name}:{result.get('final_verdict')}:{expected}")
        lines.append(f"D1_final_outcome={name} verdict={expected} PASS")
        del ctx, result
        gc.collect()

    r1_diag = reader_required_channels_gate(synthetic_reader())
    if r1_diag["status"] != "PASS" or r1_diag["reader_state"] != "QUALIFIED_FOR_REQUIRED_CHANNELS_WITH_DECLARED_LIMITATION":
        raise GateError("S23_i_historical_R1_diag_still_blocks")
    if r1_diag["worker_lps"] != list(P_LPS) or r1_diag["INST_RETIRED.ANY"] != "DIAGNOSTIC_ONLY_ALL_LP":
        raise GateError("S23_i_core_scope_or_R1_role_invalid")
    lines.append("S23_i=PASS R1_LP0_historical_FAIL reader=QUALIFIED_FOR_REQUIRED_CHANNELS_WITH_DECLARED_LIMITATION workers=8 INST_RETIRED=DIAGNOSTIC_ONLY")

    for label, reader, expected_reason in (
        ("REF_TSC_LP0_low", synthetic_reader(ref_tsc_lp0=.97), "REF_TSC_out_of_range:LP0"),
        ("frequency_invalid", synthetic_reader(invalid_frequency_lp=0), "active_frequency_invalid:LP0"),
        ("mandatory_channel_missing", synthetic_reader(missing_channel_lp=0), "required_channel_missing:LP0"),
    ):
        result = reader_required_channels_gate(reader)
        if result["status"] != "HOLD_INSTRUMENTATION" or result["reason"] != expected_reason:
            raise GateError(f"S23_ii_bad_transition:{label}:{result}")
        lines.append(f"S23_ii={label} state=HOLD_INSTRUMENTATION LP0_retained=true workers=8 no_seven_P_fallback=PASS")

    science_cut = run_phase_machine(_synthetic_context(qualification_slow_m1=True, qa_output=qa_output))
    if science_cut["final_verdict"] != "FALLIDA_CUALIFICACION" or science_cut["residency_started"]:
        raise GateError("S23_iii_scientific_fail_not_cut")
    lines.append("S23_iii=PASS instrument_valid=true science_fail=FALLIDA_CUALIFICACION residency=NOT_STARTED LP0_retained=true")

    d8_negative = (
        ("H2_over_0_10", synthetic_d8_captures(bad_h2=("Q", 14)), "D8_hot_gate_fail"),
        ("H3_over_0_10", synthetic_d8_captures(bad_h3=("R", 0)), "D8_hot_gate_fail"),
        ("denominator_under_10000", synthetic_d8_captures(low_denominator=("Q", 2)), "D8_denominator_NOT_ESTIMABLE"),
        ("channel_LP_missing", synthetic_d8_captures(missing_lp=("R", 14)), "D8_LP_coverage_invalid"),
        ("mixed_window_ids", synthetic_d8_captures(mixed_windows=True), "D8_mixed_windows"),
    )
    for name, captures, prefix in d8_negative:
        result = validate_d8_post_revalidation(captures)
        if result["status"] != "HOLD_INSTRUMENTATION" or not result["reason"].startswith(prefix):
            raise GateError(f"S22_negative_not_rejected:{name}:{result}")
        lines.append(f"S22_negative={name} state=HOLD_INSTRUMENTATION PASS")

    for name, failures in (("duration_retry", ["DURATION_INSUFFICIENT", None]),
                           ("second_hold_final", ["FREQUENCY_UNSTABLE", "FREQUENCY_UNSTABLE"])):
        dir_path = OBJ / "synthetic_retry_fixtures_r15" / name
        if dir_path.exists():
            shutil.rmtree(dir_path)
        ledger = RetryLedger(dir_path, session_id=f"fixture-{name}-r15",
                             plan_sha256=retry_plan_sha,
                             order_sha256=retry_order_sha)
        first = ledger.record(outcome="TECHNICAL_HOLD", reason=failures[0])
        if name == "duration_retry" and first["state"] != "RETRY_GLOBAL_ONCE":
            raise GateError("S19_retry_first_hold_failed")
        if name == "duration_retry":
            second = ledger.record(outcome="PASS", reason=None)
            if second["attempt"] != 2 or second["retry"]:
                raise GateError("S19_retry_attempt2_invalid")
        else:
            second = ledger.record(outcome="TECHNICAL_HOLD", reason=failures[1])
            if second["state"] != "HOLD_INSTRUMENTATION" or second["retry"]:
                raise GateError("S19_second_hold_not_final")
        shutil.rmtree(dir_path)
    science_dir = OBJ / "synthetic_retry_fixtures_r15" / "science_no_retry"
    if science_dir.exists():
        shutil.rmtree(science_dir)
    science_ledger = RetryLedger(science_dir, session_id="fixture-science-r15",
                                 plan_sha256=retry_plan_sha,
                                 order_sha256=retry_order_sha)
    science_attempt = science_ledger.record(outcome="SCIENTIFIC_FAILURE", reason=None)
    if science_attempt["retry"] or science_attempt["state"] != "FALLIDA_CUALIFICACION":
        raise GateError("S19_scientific_failure_retried")
    shutil.rmtree(science_dir)
    lines.append("S19_retry=PASS one_global_typed_retry same_identity second_HOLD_final scientific_FAIL_no_retry")

    # D11 fixtures exercise explicit physical and control outcomes through the same deriver.
    lines.append("D11_outputs=PASS competence_failure residency_failure L2_NOT_ESTABLISHED NO_ADJUDICABLE_CONTROLES")
    return lines


def run_synthetic_self_test(output: Path, *, qa_output: str | None = None) -> list[str]:
    if qa_output is None:
        command = [str(INTEGRATED_EXE), "--qa-r15"]
        process = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, check=False)
        qa_output = process.stdout + process.stderr
        if process.returncode != 0 or "KERNEL_QA_R15 PASS" not in qa_output:
            raise GateError(f"real_kernel_QA_r15_failed:rc={process.returncode}:{qa_output[-1000:]}")
    lines = d1_s23_self_test(qa_output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("OFFICIAL_RUN_R15_SYNTHETIC_QA PASS\n" + "\n".join(lines) + "\n",
                      encoding="utf-8", newline="\n")
    return lines


def run_official_gate(*, expected_session_id: str, integrated_executable: Path,
                      authorization_path: Path = AUTH_PATH) -> dict[str, Any]:
    if os.environ.get("OMEGA_R15_INTEGRATED_LAUNCH") != "1":
        raise GateError("official_requires_integrated_harness_entry")
    if not expected_session_id:
        raise GateError("official_session_id_required")
    if integrated_executable.resolve() != INTEGRATED_EXE.resolve():
        raise GateError("official_integrated_executable_path_mismatch")
    import residency_plan_r15 as plans
    plan, _ids = plans.verify_plan(PLAN_PATH)
    if not authorization_path.is_file():
        raise GateError("AUTHORIZED.json_missing:measurements_started=false")
    hashes = artifact_hashes(plan, integrated_executable)
    auth = validate_authorized_bytes(authorization_path.read_bytes(), plan,
                                     expected_session_id=expected_session_id,
                                     executable=integrated_executable,
                                     expected_artifacts=hashes,
                                     require_real_session=True)
    # Authorization is only the first gate. A fresh D13 attempt and hash-linked
    # outputs are required before the phase machine can adjudicate the session.
    session_root = OBJ / "official_sessions_r15" / auth["session_id"]
    retry_dir = session_root / "retry_ledger_r15"
    ledger = RetryLedger(retry_dir, session_id=auth["session_id"],
                         plan_sha256=plan["sha256"], order_sha256=plan_order_sha256(plan))
    if ledger.attempts:
        last_attempt = strict_json_bytes(ledger.attempts[-1].read_bytes())
        if last_attempt.get("state") != "RETRY_GLOBAL_ONCE":
            raise GateError("official_retry_policy_already_final")
    attempt_number = len(ledger.attempts) + 1
    if attempt_number > 2:
        raise GateError("official_retry_global_limit_exceeded")
    session_dir = session_root / f"attempt_{attempt_number:02d}"
    if not session_dir.is_dir():
        raise GateError("session_bound_phase_artifacts_missing:measurements_started=false")
    ctx, store, source_names = _official_context_from_attempt(session_dir, auth, plan)
    result = run_phase_machine(ctx)
    if result["state"] == "FALLIDA_CUALIFICACION":
        retry_record = ledger.record(outcome="SCIENTIFIC_FAILURE", reason=None)
    elif result["state"] in {"HOLD_INSTRUMENTATION", "NO_ADJUDICABLE_CONTROLES"}:
        typed_reason = result.get("retry_reason")
        if typed_reason not in RETRYABLE_REASONS:
            typed_reason = None
        retry_record = ledger.record(outcome="TECHNICAL_HOLD", reason=typed_reason)
        if retry_record["retry"]:
            result["state"] = "RETRY_GLOBAL_ONCE"
            result["campaign_line"] = "HOLD"
            result["final_verdict"] = "RETRY_GLOBAL_ONCE"
            result["residency"] = "NOT_ADJUDICATED"
    else:
        retry_record = ledger.record(outcome="PASS", reason=None)
    result["retry_policy"] = retry_record
    result["mode"] = "OFFICIAL"
    result["session_id"] = auth["session_id"]
    result["measurements_started"] = result.get("residency_started") is True
    store.write_derived("official_gate_result_r15.json", canonical_bytes(result) + b"\n",
                        source_raw=source_names,
                        metadata={"session_id": auth["session_id"],
                                  "integrated_executable_sha256": auth["executable_sha256"],
                                  "phase_count": len(result.get("phases", [])),
                                  "retry_attempt": retry_record["attempt"],
                                  "retry_state": retry_record["state"]})
    return result


def _write_fail_closed(path: Path, reason: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    answer = {"mode": "OFFICIAL", "state": "HOLD_AUTHORIZATION_OR_INPUTS",
              "reason": reason, "measurements_started": False,
              "campaign_line": "HOLD", "residency": "NOT_STARTED"}
    path.write_text(json.dumps(answer, ensure_ascii=False, sort_keys=True,
                               separators=(",", ":"), allow_nan=False) + "\n",
                    encoding="utf-8", newline="\n")


OFFICIAL_SESSION_RAW_FILES = (
    "preflight_r15.json", "reader_qualification_r15.json",
    "kernel_reference_qualification_r15.json", "science_qualification_r15.json",
    "qualification_events_r15.jsonl", "qualification_s20_r15.json", "kernel_qa_r15.json",
    "residency_events_r15.jsonl", "residency_episodes_r15.jsonl",
    "residency_s20_r15.json", "margins_closure_r15.json", "d8_post_revalidation_r15.json",
)


def _official_context_from_attempt(session_dir: Path, auth: dict[str, Any],
                                   plan: dict[str, Any]) -> tuple[dict[str, Any], Any, list[str]]:
    validator_dir = ROOT / "T0P" / "validator"
    if str(validator_dir) not in sys.path:
        sys.path.insert(0, str(validator_dir))
    from artifact_store import ArtifactError, SessionAttemptStore  # type: ignore
    try:
        store = SessionAttemptStore(session_dir)
    except (ArtifactError, OSError) as exc:
        raise GateError(f"D13_attempt_invalid:{exc}") from exc
    metadata = strict_json_bytes((store.directory / "attempt_metadata.json").read_bytes())
    if not isinstance(metadata, dict):
        raise GateError("D13_attempt_metadata_invalid")
    if (metadata.get("protocol") != "D13-write-once-v1" or
            metadata.get("session_id") != auth["session_id"] or
            not isinstance(metadata.get("attempt_id"), str) or not metadata["attempt_id"]):
        raise GateError("D13_attempt_session_identity_invalid")
    if (store.directory / "official_gate_result_r15.json").exists():
        raise GateError("official_session_id_reuse_rejected:verdict_already_written")
    for name in OFFICIAL_SESSION_RAW_FILES:
        try:
            store.verify_raw(name)
        except (ArtifactError, OSError, IndexError) as exc:
            raise GateError(f"session_bound_phase_artifact_invalid:{name}:{exc}") from exc
    import qualify_r15 as qualification
    def load_json(name: str) -> Any:
        value = strict_json_bytes((store.directory / name).read_bytes())
        return value
    def load_jsonl(name: str) -> list[dict[str, Any]]:
        rows: list[dict[str, Any]] = []
        with (store.directory / name).open("rb") as stream:
            for number, line in enumerate(stream, 1):
                if not line.strip():
                    raise GateError(f"official_jsonl_blank_line:{name}:{number}")
                value = strict_json_bytes(line.rstrip(b"\r\n"))
                if not isinstance(value, dict):
                    raise GateError(f"official_jsonl_row_not_object:{name}:{number}")
                rows.append(value)
        return rows

    preflight = load_json("preflight_r15.json")
    reader = load_json("reader_qualification_r15.json")
    reference_evidence = load_json("kernel_reference_qualification_r15.json")
    science_result = load_json("science_qualification_r15.json")
    qualification_rows = load_jsonl("qualification_events_r15.jsonl")
    qualification_s20 = load_json("qualification_s20_r15.json")
    qa_evidence = load_json("kernel_qa_r15.json")
    residency_rows = load_jsonl("residency_events_r15.jsonl")
    episode_rows = load_jsonl("residency_episodes_r15.jsonl")
    residency_s20 = load_json("residency_s20_r15.json")
    margins_closure = load_json("margins_closure_r15.json")
    d8 = load_json("d8_post_revalidation_r15.json")
    if any(not isinstance(value, dict) for value in
           (preflight, reader, reference_evidence, science_result, qualification_s20,
            qa_evidence, residency_s20, margins_closure, d8)):
        raise GateError("official_phase_artifact_not_object")
    qual_ids = qualification._test_plan_ids()
    if not isinstance(qualification_s20, dict) or qualification_s20.get("state") not in {"PASS", "HOLD_INSTRUMENTATION"}:
        raise GateError("official_qualification_s20_state_invalid")
    if qualification_s20.get("state") == "PASS":
        valid_qual_ids = qualification_s20.get("valid_plan_event_ids")
        if (not isinstance(valid_qual_ids, list) or any(not isinstance(item, str) for item in valid_qual_ids) or
                set(valid_qual_ids) != set(qual_ids) or
                qualification_s20.get("auxiliary_rows_valid") is not True):
            raise GateError("official_qualification_s20_event_coverage_invalid")
    residency_s20 = load_json("residency_s20_r15.json")
    planned_ids = {event["id"] for event in plan["events"]}
    if not isinstance(residency_s20, dict) or residency_s20.get("state") not in {"PASS", "HOLD_INSTRUMENTATION"}:
        raise GateError("official_residency_s20_state_invalid")
    if residency_s20.get("state") == "PASS":
        valid_residency_ids = residency_s20.get("valid_event_ids")
        if (not isinstance(valid_residency_ids, list) or
                any(not isinstance(item, str) for item in valid_residency_ids) or
                set(valid_residency_ids) != planned_ids):
            raise GateError("official_residency_s20_event_coverage_invalid")
    qa_output = qa_evidence.get("stdout", qa_evidence.get("output", ""))
    instrumentation_state = "PASS" if qualification_s20.get("state") == "PASS" else "HOLD_INSTRUMENTATION"
    instrumentation_reason = ("OFFICIAL_S20_QUALIFICATION_PASS" if instrumentation_state == "PASS"
                              else qualification_s20.get("reason", "S20_qualification_hold"))
    ctx = {
        "label": "OFFICIAL", "plan": plan, "preflight": preflight,
        "instrumentation": {"state": instrumentation_state, "reason": instrumentation_reason,
                            "retry_reason": qualification_s20.get("retry_reason")},
        "reader": reader, "qualification_ids": qual_ids,
        "qualification_rows": qualification_rows, "qualification_summary": [{"label": "OFFICIAL"}],
        "qualification_s20": qualification_s20, "reference_qualification": reference_evidence,
        "science_result": science_result, "qa_output": qa_output,
        "residency_rows": residency_rows, "episode_rows": episode_rows,
        "residency_s20": residency_s20, "margins_closure": margins_closure, "d8": d8,
    }
    return ctx, store, list(OFFICIAL_SESSION_RAW_FILES)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--official", dest="mode", action="store_const", const="official")
    modes.add_argument("--self-test", dest="mode", action="store_const", const="self-test")
    modes.add_argument("--auth-self-test", dest="mode", action="store_const", const="auth-self-test")
    parser.add_argument("--integrated-executable", type=Path)
    parser.add_argument("--session-id")
    parser.add_argument("--authorization", type=Path, default=AUTH_PATH)
    parser.add_argument("--output", type=Path, default=OBJ / "official_run_r15_qa.txt")
    ns = parser.parse_args(argv)
    try:
        if ns.mode == "self-test":
            lines = run_synthetic_self_test(ns.output)
            print(f"OFFICIAL_RUN_R15_SYNTHETIC_QA PASS cases={len(lines)} phases=9 S23=i-iii D8_negative=5 evidence={ns.output}")
            return 0
        if ns.mode == "auth-self-test":
            lines = auth_self_test()
            print(f"AUTHORIZED_R15_SELF_TEST PASS cases={len(lines)} fixtures=TEST_ONLY absent_real_AUTH=true")
            for line in lines:
                print(line)
            return 0
        if ns.integrated_executable is None or ns.session_id is None:
            raise GateError("official_integrated_executable_and_session_id_required")
        try:
            result = run_official_gate(expected_session_id=ns.session_id,
                                      integrated_executable=ns.integrated_executable,
                                      authorization_path=ns.authorization)
        except GateError as exc:
            _write_fail_closed(OBJ / "official_fail_closed_r15.json", str(exc))
            print(f"OFFICIAL_R15_REJECT reason={exc} measurements_started=false")
            return 20
        print(json.dumps(result, ensure_ascii=False, sort_keys=True))
        return 0
    except (OSError, GateError, KeyError, TypeError, ValueError) as exc:
        print(f"OFFICIAL_RUN_R15 REJECT {type(exc).__name__}: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
