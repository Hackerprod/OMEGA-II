#!/usr/bin/env python3
"""Sequential S-24 acquisition gate. Every transition follows its acquisition."""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import math
import os
import re
import statistics
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Callable

ROOT = Path(__file__).resolve().parents[2]
COLLECTOR = Path(__file__).resolve().parent
HARNESS = ROOT / "T0P" / "harness"
OBJ = COLLECTOR / "logs_r17" / "obj_r17"
PLAN_PATH = OBJ / "residency_plan_r17.json"
INTEGRATED_EXE = HARNESS / "logs_r17" / "obj_r17" / "harness_r17.exe"
P_LPS = (0, 2, 4, 6, 8, 10, 12, 14)
RETRYABLE_REASONS = ("DURATION_INSUFFICIENT", "FREQUENCY_UNSTABLE",
                     "TELEMETRY_TRANSIENT_LOSS", "EMON_TRANSIENT_LOSS")
ACQUISITION_PHASES = (
    "preflight",
    "qa_instruments_expulsion",
    "qualification",
    "residency",
    "final_controls",
    "derivation_verdict",
)


class GateError(ValueError):
    pass


def canonical_bytes(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=True, allow_nan=False).encode("ascii")


def sha_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def authorization_binding(plan_path: Path, executable: Path) -> dict[str, Any]:
    import residency_plan_r17 as plans
    plan = plans.verify_plan(plan_path)
    torch_root = Path(r"C:\Projects\OMEGA\.venv_cuda\Lib\site-packages\torch\lib")
    library_hashes = {}
    for name in ("torch.dll", "torch_cpu.dll", "c10.dll"):
        path = torch_root / name
        if not path.is_file():
            raise GateError(f"AUTHORIZED_REQUIRED_LIBRARY_MISSING:{path}")
        library_hashes[name] = sha_file(path)
    segments = plan.get("segments")
    qualification_units = plan.get("qualification", {}).get("units")
    orders = plan.get("S25_replica_orders")
    if not isinstance(segments, dict) or len(segments) != 37:
        raise GateError("AUTHORIZED_PLAN_SEGMENTATION_INVALID")
    if not isinstance(qualification_units, list) or len(qualification_units) != 72:
        raise GateError("AUTHORIZED_PLAN_QUALIFICATION_UNITS_INVALID")
    if not isinstance(orders, list) or len(orders) != 31:
        raise GateError("AUTHORIZED_PLAN_S25_ORDERS_INVALID")
    computed_orders_sha = sha_bytes(canonical_bytes(orders))
    if computed_orders_sha != plan.get("S25_replica_orders_sha256"):
        raise GateError("AUTHORIZED_PLAN_S25_ORDER_HASH_MISMATCH")
    return {
        "integrated_executable_sha256": sha_file(executable),
        "loaded_libraries_root": str(torch_root.resolve()),
        "loaded_libraries_sha256": library_hashes,
        "plan_sha256": plan["sha256"],
        "segmentation_sha256": sha_bytes(canonical_bytes(segments)),
        "qualification_units_sha256": sha_bytes(canonical_bytes(qualification_units)),
        "s25_replica_orders_sha256": computed_orders_sha,
        "governing_validator_sha256": sha_file(COLLECTOR / "s20_validator_r17.py"),
    }


def validate_authorization_document(document: Any, expected_bindings: dict[str, Any],
                                   expected_session_id: str | None = None) -> None:
    if not isinstance(document, dict):
        raise GateError("AUTHORIZED_DOCUMENT_OBJECT_REQUIRED")
    expected_keys = {"schema_version", "session_id", "acquisition_phases", "bindings", "retry_policy"}
    if set(document) != expected_keys:
        raise GateError("AUTHORIZED_FIELDS_MISMATCH")
    if document.get("schema_version") != "T0P_AUTHORIZED_R17_V1":
        raise GateError("AUTHORIZED_SCHEMA_VERSION_MISMATCH")
    session_id = document.get("session_id")
    if not isinstance(session_id, str) or not re.fullmatch(r"[A-Za-z0-9._-]{1,64}", session_id):
        raise GateError("AUTHORIZED_SESSION_ID_INVALID")
    if expected_session_id is not None and session_id != expected_session_id:
        raise GateError("AUTHORIZED_SESSION_ID_MISMATCH")
    if document.get("acquisition_phases") != list(ACQUISITION_PHASES):
        raise GateError("AUTHORIZED_ACQUISITION_PHASES_MISMATCH")
    if document.get("bindings") != expected_bindings:
        raise GateError("AUTHORIZED_BINDINGS_MISMATCH")
    if document.get("retry_policy") != {"retryable_reasons": list(RETRYABLE_REASONS),
            "global_technical_retry_budget": 1, "planned_replica_pause_consumes_budget": False}:
        raise GateError("AUTHORIZED_RETRY_POLICY_MISMATCH")


def verify_authorization_file(path: Path, plan_path: Path, executable: Path,
                              expected_session_id: str) -> dict[str, Any]:
    try:
        document = strict_json_bytes(path.read_bytes())
    except OSError as exc:
        raise GateError(f"AUTHORIZED_MISSING_OR_UNREADABLE:{exc}") from exc
    validate_authorization_document(document, authorization_binding(plan_path, executable), expected_session_id)
    return document


def _strict_pairs(pairs):
    out = {}
    for key, value in pairs:
        if key in out:
            raise GateError(f"duplicate_json_key:{key}")
        out[key] = value
    return out


def strict_json_bytes(data: bytes) -> Any:
    try:
        value = json.loads(data.decode("utf-8"), object_pairs_hook=_strict_pairs,
                           parse_constant=lambda value: (_ for _ in ()).throw(GateError(f"nonfinite:{value}")))
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise GateError(f"invalid_json:{exc}") from exc
    def finite(node: Any) -> None:
        if isinstance(node, float) and not math.isfinite(node):
            raise GateError("nonfinite_json_number")
        if isinstance(node, dict):
            for child in node.values(): finite(child)
        elif isinstance(node, list):
            for child in node: finite(child)
    finite(value)
    return value


def _write_once(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.write(data)
        stream.flush()


def _attempt_bytes(directory: Path) -> int:
    total = 0
    for entry in directory.iterdir():
        if entry.is_file():
            total += entry.stat().st_size
        elif entry.is_dir() and entry.name in {"raw_manifest_r17", "derived_lineage_r17"}:
            for sidecar in entry.iterdir():
                if sidecar.is_file():
                    total += sidecar.stat().st_size
    return total


class AcquisitionAdapter:
    """Explicit boundary used by the gate to acquire one phase at a time."""

    def acquire(self, phase: str, request: dict[str, Any],
                previous: dict[str, Any] | None) -> dict[str, Any]:
        raise NotImplementedError


class ExecutableAcquisitionAdapter(AcquisitionAdapter):
    """Launch the integrated executable; tests may replace only process execution."""

    def __init__(self, executable: Path, *,
                 execute: Callable[..., Any] | None = None):
        self.executable = executable.resolve()
        self.execute = execute or self._execute_process
        self.calls: list[str] = []
        self.emon_process: subprocess.Popen | None = None
        self.emon_stdout = None
        self.emon_stderr = None
        self.capture_start_qpc: int | None = None

    @staticmethod
    def _execute_process(command: list[str], *, cwd: Path,
                         stdout_path: Path, stderr_path: Path) -> int:
        env = os.environ.copy()
        torch_lib = r"C:\Projects\OMEGA\.venv_cuda\Lib\site-packages\torch\lib"
        ggml_bin = str(ROOT / "T0P" / "ref_ggml" / "build_llama" / "bin")
        env["PATH"] = os.pathsep.join([torch_lib, ggml_bin, env.get("PATH", "")])
        try:
            attempt_directory = Path(command[command.index("--output") + 1]).resolve().parent
        except (ValueError, IndexError) as exc:
            raise GateError("acquisition_attempt_directory_argument_missing") from exc
        with stdout_path.open("xb") as stdout, stderr_path.open("xb") as stderr:
            process = subprocess.Popen(command, cwd=cwd, stdout=stdout, stderr=stderr, env=env,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            while process.poll() is None:
                free = shutil.disk_usage("C:\\").free
                used = _attempt_bytes(attempt_directory)
                if free < 4 * 1024**3 or used >= 1024**3:
                    process.terminate()
                    try: process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill(); process.wait(timeout=5)
                    raise GateError(f"STOP_DURING_SMOKE:free_bytes={free}:attempt_bytes={used}")
                time.sleep(.25)
        return process.returncode

    @staticmethod
    def _qpc() -> int:
        counter = ctypes.c_longlong()
        if not ctypes.windll.kernel32.QueryPerformanceCounter(ctypes.byref(counter)):
            raise GateError("parent_QPC_read_failed")
        return int(counter.value)

    def _start_emon(self, request: dict[str, Any]) -> None:
        if self.execute != self._execute_process:
            return
        if self.emon_process is not None:
            raise GateError("EMON_already_started")
        emon = Path(request["emon_path"])
        edp = Path(request["edp_path"])
        self.emon_stdout = (request["output_dir"] / "emon_raw.txt").open("xb")
        self.emon_stderr = (request["output_dir"] / "emon_stderr.txt").open("xb")
        self.emon_process = subprocess.Popen([str(emon), "-collect-edp", f"edp_file={edp}"],
            cwd=request["output_dir"], stdout=self.emon_stdout, stderr=self.emon_stderr,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        self.capture_start_qpc = self._qpc()
        time.sleep(2)
        if self.emon_process.poll() is not None:
            self._close_emon_handles()
            raise GateError(f"EMON_start_failed:exit={self.emon_process.returncode}")

    def _close_emon_handles(self) -> None:
        for handle in (self.emon_stdout, self.emon_stderr):
            if handle:
                handle.close()
        self.emon_stdout = self.emon_stderr = None

    def _prepare_disassembly(self, request: dict[str, Any]) -> str:
        path = request["disassembly_path"]
        if path.is_file():
            data = strict_json_bytes(path.read_bytes())
            if data.get("state") != "PASS":
                raise GateError("existing_disassembly_QA_not_PASS")
            return str(data["sha256"])
        root = Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC")
        candidates = list(root.glob("*/bin/Hostx64/x64/dumpbin.exe"))
        if len(candidates) != 1:
            raise GateError(f"dumpbin_not_unique:{len(candidates)}")
        dumpbin = candidates[0]
        disassembly = request["output_dir"] / "harness_disassembly_r17.txt"
        symbol_extract = request["output_dir"] / "pinstr_symbol_map_r17.txt"
        for target, args in ((disassembly, ["/DISASM"]),):
            with target.open("xb") as stream:
                completed = subprocess.run([str(dumpbin), *args, str(self.executable)],
                    cwd=request["output_dir"], stdout=stream, stderr=subprocess.PIPE, check=False)
            if completed.returncode != 0:
                tail = target.read_text(encoding="utf-8", errors="replace")[-500:]
                raise GateError(f"dumpbin_failed:{args[0]}:{completed.returncode}:{tail}")
        map_path = self.executable.with_suffix(".map")
        if not map_path.is_file():
            raise GateError(f"linker_map_missing_for_disassembly:{map_path}")
        symbol_text = map_path.read_text(encoding="utf-8", errors="replace")
        disasm_text = disassembly.read_text(encoding="utf-8", errors="replace")
        required = [f"pinstr_c{c}_u{u}" for c, u in
                    ((1, 8), (2, 8), (4, 8), (8, 8), (10, 8), (12, 8), (14, 8), (12, 16), (14, 16))]
        mapped: list[tuple[str, str]] = []
        missing = []
        for symbol in required:
            match = re.search(rf"^\s*[0-9A-Fa-f]+:[0-9A-Fa-f]+\s+{re.escape(symbol)}\s+([0-9A-Fa-f]{{16}})\s+f\s+provider_aux_r17\.obj\s*$",
                              symbol_text, re.MULTILINE)
            if not match or not re.search(re.escape(match.group(1)) + r":", disasm_text, re.IGNORECASE):
                missing.append(symbol)
            else:
                mapped.append((symbol, match.group(1)))
        if missing or disassembly.stat().st_size < 10_000:
            raise GateError(f"P_instr_disassembly_incomplete:missing={missing}:bytes={disassembly.stat().st_size}")
        with symbol_extract.open("x", encoding="ascii", newline="\n") as symbol_file:
            symbol_file.write("\n".join(f"{address} {name}" for name, address in mapped) + "\n")
        digest = sha_file(disassembly)
        artifact = {"state": "PASS", "method": "dumpbin /DISASM matched to linker map",
                    "sha256": digest, "symbol_map_sha256": sha_file(symbol_extract),
                    "linker_map_sha256": sha_file(map_path), "executable_sha256": sha_file(self.executable),
                    "required_variants": required, "disassembly_bytes": disassembly.stat().st_size}
        _write_once(path, canonical_bytes(artifact) + b"\n")
        return digest

    def _stop_emon(self, request: dict[str, Any]) -> dict[str, Any]:
        proc = self.emon_process
        if self.execute != self._execute_process:
            return {"state": "TEST_DOUBLE_NOT_RUNNING"}
        if proc is None:
            raise GateError("EMON_not_started_before_residency")
        if proc.poll() is not None:
            self._close_emon_handles()
            raise GateError(f"EMON_ended_before_controlled_stop:exit={proc.returncode}")
        stop = subprocess.run([str(request["emon_path"]), "-stop"], cwd=request["output_dir"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0), check=False)
        if stop.returncode != 0:
            raise GateError(f"EMON_controlled_stop_failed:exit={stop.returncode}")
        try:
            exit_code = proc.wait(timeout=30)
        except subprocess.TimeoutExpired as exc:
            raise GateError("EMON_did_not_finish_within_30s_after_stop") from exc
        end_qpc = self._qpc()
        self._close_emon_handles()
        if exit_code != 0:
            raise GateError(f"EMON_exit_after_controlled_stop:{exit_code}")
        raw = request["output_dir"] / "emon_raw.txt"
        if not raw.is_file() or raw.stat().st_size >= 1024**3:
            raise GateError("EMON_capture_missing_or_at_least_1GiB")
        timing = {"qpc_start": self.capture_start_qpc, "qpc_end": end_qpc,
            "qpc_source": "parent_QueryPerformanceCounter", "stop_command": "emon -stop",
            "stop_exit": stop.returncode, "emon_exit": exit_code,
            "capture_bytes": raw.stat().st_size, "controlled_stop": True}
        _write_once(request["output_dir"] / "capture_timing_r17.json", canonical_bytes(timing) + b"\n")
        return timing

    def acquire(self, phase: str, request: dict[str, Any],
                previous: dict[str, Any] | None) -> dict[str, Any]:
        if phase not in ACQUISITION_PHASES:
            raise GateError(f"unsupported_acquisition_phase:{phase}")
        if self.execute == self._execute_process and not self.executable.is_file():
            raise GateError(f"integrated_executable_missing:{self.executable}")
        out_dir: Path = request["output_dir"]
        evidence_path = out_dir / f"phase_{phase}_r17.json"
        stdout_path = out_dir / f"phase_{phase}_stdout_r17.txt"
        stderr_path = out_dir / f"phase_{phase}_stderr_r17.txt"
        if any(path.exists() for path in (evidence_path, stdout_path, stderr_path)):
            raise GateError(f"phase_artifact_exists:{phase}")
        if phase == "qualification" and self.execute == self._execute_process:
            self._prepare_disassembly(request)
        command = [str(self.executable), "--acquire-r17", phase,
                   "--plan", str(request["plan_path"]),
                   "--session-id", request["session_id"],
                   "--output", str(evidence_path),
                   "--mode", request.get("mode", "SMOKE")]
        command.extend(["--schedule", str(request["schedule_path"]),
                        "--disassembly", str(request["disassembly_path"])])
        if phase == "residency" and self.execute == self._execute_process:
            if self.emon_process is None or self.emon_process.poll() is not None:
                raise GateError("EMON_reader_not_live_before_residency_segment")
            command.extend(["--reader-state", "QUALIFIED_FOR_REQUIRED_CHANNELS_WITH_DECLARED_LIMITATION"])
        if request.get("mode") == "SMOKE":
            command.extend(["--N", str(request.get("N", 3)),
                            "--residency-block", request.get("residency_block", "W288|O1024|r1")])
        if request.get("emon_path"):
            command.extend(["--emon-path", str(request["emon_path"]),
                            "--edp", str(request["edp_path"]),
                            "--capture-budget", str(request["capture_budget_path"])])
        if previous:
            command.extend(["--previous-evidence", str(previous["evidence_path"]),
                            "--previous-evidence-sha256", previous["sha256"]])
        self.calls.append(phase)
        rc = self.execute(command, cwd=ROOT, stdout_path=stdout_path,
                          stderr_path=stderr_path)
        if rc != 0:
            stderr = stderr_path.read_text(encoding="utf-8", errors="replace") if stderr_path.exists() else ""
            raise GateError(f"acquisition_process_failed:{phase}:exit={rc}:{stderr[-500:]}")
        if not evidence_path.is_file():
            raise GateError(f"acquisition_evidence_missing:{phase}")
        evidence = strict_json_bytes(evidence_path.read_bytes())
        if previous and evidence.get("previous_evidence_sha256") != previous.get("sha256"):
            raise GateError(f"acquisition_previous_evidence_link_invalid:{phase}")
        if phase == "qa_instruments_expulsion" and self.execute == self._execute_process:
            self._start_emon(request)
            evidence["reader_state"] = "QUALIFIED_FOR_REQUIRED_CHANNELS_WITH_DECLARED_LIMITATION"
            evidence["emon_capture_started_by_parent"] = True
        if phase == "residency" and self.execute == self._execute_process:
            timing = self._stop_emon(request)
            evidence["capture_timing"] = timing
            evidence["controlled_emon_stop"] = timing.get("controlled_stop") is True
        if (not isinstance(evidence, dict) or evidence.get("phase") != phase or
                evidence.get("plan_sha256") != request["plan_sha256"] or
                evidence.get("session_id") != request["session_id"]):
            raise GateError(f"acquisition_evidence_identity_invalid:{phase}")
        evidence["evidence_path"] = str(evidence_path)
        evidence["sha256"] = sha_file(evidence_path)
        return evidence


def _explicit_stage(status: str, phase: str, **fields: Any) -> dict[str, Any]:
    return {"status": status, "phase": phase, **fields}


def _validate_preflight(e: dict[str, Any], plan: dict[str, Any]) -> dict[str, Any]:
    required = e.get("state") == "PASS" and e.get("plan_sha256") == plan["sha256"]
    required = required and e.get("large_pages") is True and e.get("qpc_hz", 0) > 0
    required = required and e.get("worker_lps") == list(P_LPS)
    required = required and e.get("topology_map_sha256") and re.fullmatch(r"[0-9a-f]{64}", str(e.get("topology_map_sha256")))
    if not required:
        return _explicit_stage("HOLD_INSTRUMENTATION", "preflight",
                               reason=e.get("reason", "preflight_identity_topology_pages_clock_or_reader_invalid"))
    return _explicit_stage("PASS", "preflight", plan_sha256=plan["sha256"],
                           topology_map_sha256=e["topology_map_sha256"],
                           worker_lps=list(P_LPS), qpc_hz=e["qpc_hz"], large_pages=True)


def _validate_qa_instruments_expulsion(e: dict[str, Any], previous: dict[str, Any]) -> dict[str, Any]:
    ok = (e.get("state") == "PASS" and e.get("kernel_qa") == "PASS" and
          e.get("instrumentation") == "PASS" and e.get("expulsion_qa") == "PASS" and
          e.get("topology_map_sha256") == previous["topology_map_sha256"] and
          e.get("reader_state") == "QUALIFIED_FOR_REQUIRED_CHANNELS_WITH_DECLARED_LIMITATION")
    if not ok:
        return _explicit_stage("HOLD_INSTRUMENTATION", "qa_instruments_expulsion",
                               reason=e.get("reason", "QA_instrument_or_expulsion_not_qualified"))
    return _explicit_stage("PASS", "qa_instruments_expulsion", qa=e["kernel_qa"],
                           instrumentation=e["instrumentation"], expulsion=e["expulsion_qa"],
                           topology_map_sha256=e["topology_map_sha256"])


def _validate_qualification(e: dict[str, Any], plan: dict[str, Any], previous: dict[str, Any],
                           mode: str = "OFFICIAL") -> dict[str, Any]:
    state = e.get("state")
    if e.get("plan_sha256") != plan["sha256"] or e.get("topology_map_sha256") != previous["topology_map_sha256"]:
        return _explicit_stage("HOLD_INSTRUMENTATION", "qualification", reason="qualification_plan_or_map_identity_mismatch")
    if mode == "SMOKE" and state == "SMOKE_ONLY" and previous.get("phase") == "qa_instruments_expulsion":
        acquired_aux = e.get("acquired_aux_validation", {})
        smoke_ok = (e.get("numeric_metrics_used") is False and
                    e.get("p_instr") == "ACQUIRED_N3" and e.get("bw_dram") == "ACQUIRED_N3" and
                    e.get("p_instr_variants") == 9 and
                    e.get("N") == 3 and e.get("all_workers_calibrated") is True and
                    re.fullmatch(r"[0-9a-f]{64}", str(e.get("disassembly_sha256", ""))) and
                    acquired_aux.get("complete") is True and acquired_aux.get("reason") == "none" and
                    acquired_aux.get("rows") == 240 and acquired_aux.get("cells") == 10 and
                    acquired_aux.get("P_instr_variants") == 9 and acquired_aux.get("BW_DRAM_rows") == 24 and
                    acquired_aux.get("workers_per_replica") == 8 and acquired_aux.get("R_common_by_cell"))
        if not smoke_ok:
            return _explicit_stage("HOLD_INSTRUMENTATION", "qualification",
                                   reason="minimum_smoke_qualification_invalid")
        return _explicit_stage("PASS", "qualification", state="SMOKE_ONLY",
                               diagnostic_only=True, numeric_metrics_used=False,
                               topology_map_sha256=previous["topology_map_sha256"])
    if e.get("selection_event_count") != 11718 or e.get("comparison_event_count") != 6696:
        return _explicit_stage("HOLD_INSTRUMENTATION", "qualification", reason="qualification_event_count_invalid")
    if e.get("qualification_order_sha256") != plan["qualification_order_sha256"]:
        return _explicit_stage("HOLD_INSTRUMENTATION", "qualification", reason="qualification_order_hash_mismatch")
    if state == "FALLIDA_CUALIFICACION":
        if e.get("valid_scientific_inputs") is not True or not e.get("failed_gates"):
            return _explicit_stage("HOLD_INSTRUMENTATION", "qualification", reason="scientific_fail_without_valid_inputs")
        return _explicit_stage("FALLIDA_CUALIFICACION", "qualification",
                               reason="valid_scientific_gate_failed", failed_gates=e["failed_gates"])
    if state == "HOLD_INSTRUMENTATION":
        return _explicit_stage("HOLD_INSTRUMENTATION", "qualification",
                               reason=e.get("reason", "qualification_instrument_hold"),
                               retry_reason=e.get("retry_reason"))
    good = (state == "QUALIFIED_FOR_RESIDENCY" and e.get("bw_dram") == "PASS" and
            e.get("p_instr") == "PASS" and e.get("reference_selection") == "PASS" and
            e.get("comparison") == "PASS" and e.get("R_common_by_unit") is True and
            e.get("all_31_samples") is True)
    if not good:
        return _explicit_stage("HOLD_INSTRUMENTATION", "qualification",
                               reason="qualification_subgate_or_series_contract_invalid")
    return _explicit_stage("PASS", "qualification", state=state,
                           selection_events=11718, comparison_events=6696,
                           topology_map_sha256=e["topology_map_sha256"],
                           failed_gates=[])


def _qualification_cut(stage: dict[str, Any], mode: str = "OFFICIAL") -> dict[str, Any]:
    if stage["status"] == "PASS":
        if mode == "SMOKE" and stage.get("state") == "SMOKE_ONLY":
            return _explicit_stage("PASS", "automatic_cut", decision="ALLOW_DIAGNOSTIC_SMOKE_RESIDENCY",
                                   qualification_state="SMOKE_ONLY", numeric_metrics_used=False)
        return _explicit_stage("PASS", "automatic_cut", decision="ALLOW_RESIDENCY",
                               qualification_state="QUALIFIED_FOR_RESIDENCY")
    if stage["status"] == "FALLIDA_CUALIFICACION":
        return _explicit_stage("STOP", "automatic_cut", decision="STOP_BEFORE_RESIDENCY",
                               qualification_state="FALLIDA_CUALIFICACION")
    return _explicit_stage("STOP", "automatic_cut", decision="HOLD_BEFORE_RESIDENCY",
                           qualification_state=stage["status"])


def _validate_residency(e: dict[str, Any], plan: dict[str, Any], previous: dict[str, Any], mode: str) -> dict[str, Any]:
    if e.get("plan_sha256") != plan["sha256"] or e.get("topology_map_sha256") != previous["topology_map_sha256"]:
        return _explicit_stage("NO_ADJUDICABLE_CONTROLES", "residency", reason="residency_plan_or_map_identity_mismatch")
    if mode == "SMOKE" and e.get("state") == "SMOKE_ONLY":
        if (e.get("N") != 3 or e.get("numeric_metrics_used") is not False or
                e.get("block_complete") is not True or e.get("event_count") != 264 or
                e.get("selected_schedule_rows") != 264 or e.get("calibration_events") != 60 or
                e.get("event_order_matches_schedule") is not True or
                e.get("controlled_emon_stop") is not True):
            return _explicit_stage("NO_ADJUDICABLE_CONTROLES", "residency", reason="smoke_not_N3_or_complete_block")
        return _explicit_stage("SMOKE_ONLY", "residency", events=e.get("event_count"),
                               block_id=e.get("block_id"), numeric_metrics_used=False,
                               topology_map_sha256=e.get("topology_map_sha256"))
    if e.get("state") != "PASS" or e.get("event_count") != plan["residency_event_count"]:
        return _explicit_stage("NO_ADJUDICABLE_CONTROLES", "residency", reason=e.get("reason", "residency_plan_execution_incomplete"))
    if e.get("event_order_sha256") != plan["residency_order_sha256"] or e.get("N") != 31:
        return _explicit_stage("NO_ADJUDICABLE_CONTROLES", "residency", reason="residency_order_or_N_invalid")
    return _explicit_stage("PASS", "residency", event_count=e["event_count"], N=31,
                           topology_map_sha256=e["topology_map_sha256"])


def _validate_final_controls(e: dict[str, Any], previous: dict[str, Any]) -> dict[str, Any]:
    d8 = validate_d8_post_revalidation(e.get("d8_captures", {}))
    if (e.get("state") != "PASS" or d8.get("status") != "PASS" or
            e.get("topology_map_sha256") != previous["topology_map_sha256"]):
        return _explicit_stage("HOLD_INSTRUMENTATION", "final_controls",
                               reason=d8.get("reason", e.get("reason", "final_controls_or_D8_invalid")))
    return _explicit_stage("PASS", "final_controls", D8=d8)


def _validate_derivation(e: dict[str, Any], plan: dict[str, Any]) -> dict[str, Any]:
    if e.get("plan_sha256") != plan["sha256"]:
        return _explicit_stage("NO_ADJUDICABLE_CONTROLES", "derivation_verdict", reason="derivation_plan_hash_mismatch")
    verdict = e.get("verdict")
    if verdict not in {"RESIDENCIA_PASS", "FALLO_RESIDENCIA_VALIDO", "L2_NOT_ESTABLISHED",
                       "NO_ADJUDICABLE_CONTROLES"}:
        return _explicit_stage("NO_ADJUDICABLE_CONTROLES", "derivation_verdict", reason="invalid_derivation_verdict")
    if verdict == "NO_ADJUDICABLE_CONTROLES":
        return _explicit_stage("NO_ADJUDICABLE_CONTROLES", "derivation_verdict", verdict=verdict,
                               reason=e.get("reason", "controls_not_adjudicable"))
    return _explicit_stage("PASS", "derivation_verdict", verdict=verdict)


def _write_stage(output_dir: Path, seq: int, stage: dict[str, Any]) -> None:
    name = f"gate_{seq:02d}_{stage['phase']}_r17.json"
    _write_once(output_dir / name, canonical_bytes(stage) + b"\n")


def run_acquisition_chain(adapter: AcquisitionAdapter, *, plan: dict[str, Any],
                          request: dict[str, Any]) -> dict[str, Any]:
    """Acquire, validate, and transition before asking the adapter for the next phase."""
    phases: list[dict[str, Any]] = []
    previous_evidence: dict[str, Any] | None = None
    previous_stage: dict[str, Any] | None = None
    validators = {
        "preflight": lambda e, p: _validate_preflight(e, plan),
        "qa_instruments_expulsion": lambda e, p: _validate_qa_instruments_expulsion(e, p),
        "qualification": lambda e, p: _validate_qualification(e, plan, p, request.get("mode", "OFFICIAL")),
        "residency": lambda e, p: _validate_residency(e, plan, p, request.get("mode", "SMOKE")),
        "final_controls": lambda e, p: _validate_final_controls(e, p),
        "derivation_verdict": lambda e, p: _validate_derivation(e, plan),
    }
    last = "NOT_STARTED"
    for phase in ACQUISITION_PHASES:
        if phase != "preflight" and (previous_stage is None or previous_stage["status"] != "PASS"):
            raise GateError(f"illegal_transition_without_PASS:{phase}")
        try:
            evidence = adapter.acquire(phase, request, previous_evidence)
        except Exception as exc:
            stage = _explicit_stage("HOLD_INSTRUMENTATION", phase,
                                    reason=f"acquisition_adapter_error:{type(exc).__name__}:{exc}")
            phases.append(stage); _write_stage(request["output_dir"], len(phases), stage)
            last = stage["status"]
            break
        stage = validators[phase](evidence, previous_stage)
        stage["acquisition_evidence_sha256"] = evidence.get("sha256")
        phases.append(stage); _write_stage(request["output_dir"], len(phases), stage)
        previous_evidence, previous_stage = evidence, stage
        last = stage["status"]

        if phase == "qualification":
            cut = _qualification_cut(stage, request.get("mode", "OFFICIAL"))
            phases.append(cut); _write_stage(request["output_dir"], len(phases), cut)
            if cut["status"] != "PASS":
                last = stage["status"] if stage["status"] == "FALLIDA_CUALIFICACION" else cut["status"]
                break
        if phase == "residency" and stage["status"] == "SMOKE_ONLY":
            last = "SMOKE_ONLY"
            break
        if stage["status"] != "PASS":
            break
    if last == "PASS" and phases and phases[-1]["phase"] == "derivation_verdict":
        state = phases[-1].get("verdict", "NO_ADJUDICABLE_CONTROLES")
    elif last == "FALLIDA_CUALIFICACION":
        state = "FALLIDA_CUALIFICACION"
    elif last == "SMOKE_ONLY":
        state = "SMOKE_ONLY"
    else:
        state = "HOLD_INSTRUMENTATION" if last in {"HOLD_INSTRUMENTATION", "STOP"} else "NO_ADJUDICABLE_CONTROLES"
    if getattr(adapter, "emon_process", None) is not None and adapter.emon_process.poll() is None:
        try:
            adapter._stop_emon(request)
        except Exception as exc:
            phases.append(_explicit_stage("HOLD_INSTRUMENTATION", "emon_controlled_stop",
                                          reason=f"controlled_stop_failed:{type(exc).__name__}:{exc}"))
            state = "HOLD_INSTRUMENTATION"
    result = {"state": state, "phases": phases,
              "acquisition_calls": list(getattr(adapter, "calls", [])),
              "residency_started": any(x["phase"] == "residency" for x in phases),
              "numeric_metrics_used": request.get("mode") == "OFFICIAL"}
    _write_once(request["output_dir"] / "acquisition_chain_r17.json", canonical_bytes(result) + b"\n")
    return result


def synthetic_d8_captures(*, bad_h2: tuple[str, int] | None = None,
                          bad_h3: tuple[str, int] | None = None,
                          low_denominator: tuple[str, int] | None = None,
                          missing_lp: tuple[str, int] | None = None,
                          mixed_windows: bool = False,
                          bad_cold_ratio: tuple[str, int] | None = None,
                          bad_cold_llc: tuple[str, int] | None = None,
                          mixed_counter_windows: bool = False) -> dict[str, list[dict[str, Any]]]:
    captures: dict[str, list[dict[str, Any]]] = {}
    for capture in ("Q", "R"):
        ids = [f"{capture}-window-{i}" for i in range(1, 33)]
        rows = []
        for lp in P_LPS:
            d2, m2, m3 = 100_000, 100, 20
            if bad_h2 == (capture, lp): m2 = 10_001
            if bad_h3 == (capture, lp): m3 = 10_001
            if low_denominator == (capture, lp): d2 = 9_999
            row_ids = ids[:-1] if mixed_windows and capture == "R" and lp == 14 else ids
            if missing_lp == (capture, lp): continue
            counter_ids = {"D2": list(row_ids), "M2": list(row_ids), "M3": list(row_ids)}
            if mixed_counter_windows and capture == "R" and lp == 14:
                counter_ids["M2"] = row_ids[:-1]
            cold_lat, hot_lat = [3.0]*31, [1.0]*31
            if bad_cold_ratio == (capture, lp): cold_lat = [1.2]*31
            cold_offcore, cold_m3 = 100_000, 99_600
            if bad_cold_llc == (capture, lp): cold_m3 = 5_000
            rows.append({"lp": lp, "scope": "LP_WORKER", "capture": capture,
                         "window_ids_by_counter": counter_ids,
                         "D2": d2, "M2": m2, "M3": m3,
                         "cold_offcore": cold_offcore, "cold_M3": cold_m3,
                         "cold_latency_ns": cold_lat, "hot_latency_ns": hot_lat,
                         "cold_hot_window_ids": list(row_ids), "cold_hot_N": 31,
                         "raw_sha256": "a"*64})
        captures[capture] = rows
    return captures


def validate_d8_post_revalidation(captures: dict[str, list[dict[str, Any]]]) -> dict[str, Any]:
    if set(captures) != {"Q", "R"}:
        return {"status": "HOLD_INSTRUMENTATION", "reason": "D8_capture_pair_missing"}
    capture_windows: dict[str, set[str]] = {}
    worst_h2, worst_h3 = 0.0, 0.0
    min_cold_ratio, min_cold_llc_miss = float("inf"), 1.0
    for capture in ("Q", "R"):
        rows = captures[capture]
        by_lp = {row.get("lp"): row for row in rows if isinstance(row, dict)}
        if set(by_lp) != set(P_LPS):
            return {"status": "HOLD_INSTRUMENTATION", "reason": f"D8_LP_coverage_invalid:{capture}"}
        for lp in P_LPS:
            row = by_lp[lp]
            if row.get("capture") != capture or row.get("scope") != "LP_WORKER":
                return {"status": "HOLD_INSTRUMENTATION", "reason": f"D8_provenance_invalid:{capture}:LP{lp}"}
            if not re.fullmatch(r"[0-9a-f]{64}", str(row.get("raw_sha256", ""))):
                return {"status": "HOLD_INSTRUMENTATION", "reason": f"D8_raw_hash_missing:{capture}:LP{lp}"}
            ids_by_counter = row.get("window_ids_by_counter")
            if not isinstance(ids_by_counter, dict) or set(ids_by_counter) != {"D2", "M2", "M3"}:
                return {"status": "HOLD_INSTRUMENTATION", "reason": f"D8_counter_window_provenance_missing:{capture}:LP{lp}"}
            id_sets = []
            for channel in ("D2", "M2", "M3"):
                ids = ids_by_counter[channel]
                if not isinstance(ids, list) or len(ids) < 20 or len(ids) != len(set(ids)):
                    return {"status": "HOLD_INSTRUMENTATION", "reason": f"D8_window_ids_invalid:{capture}:LP{lp}:{channel}"}
                id_sets.append(set(ids))
            if any(ids != id_sets[0] for ids in id_sets[1:]):
                return {"status": "HOLD_INSTRUMENTATION", "reason": f"D8_mixed_windows_within_capture:{capture}:LP{lp}"}
            ids = id_sets[0]
            if capture not in capture_windows:
                capture_windows[capture] = ids
            elif ids != capture_windows[capture]:
                return {"status": "HOLD_INSTRUMENTATION", "reason": f"D8_capture_window_alignment_invalid:{capture}:LP{lp}"}
            if row.get("cold_hot_window_ids") != ids_by_counter["D2"] or row.get("cold_hot_N") != 31:
                return {"status": "HOLD_INSTRUMENTATION", "reason": f"D8_cold_hot_window_alignment_invalid:{capture}:LP{lp}"}
            d2, m2, m3 = row.get("D2"), row.get("M2"), row.get("M3")
            if any(type(value) is not int or value < 0 for value in (d2, m2, m3)):
                return {"status": "HOLD_INSTRUMENTATION", "reason": f"D8_count_invalid:{capture}:LP{lp}"}
            if d2 < 10_000:
                return {"status": "HOLD_INSTRUMENTATION", "reason": f"D8_denominator_NOT_ESTIMABLE:{capture}:LP{lp}"}
            if m2 > d2:
                return {"status": "HOLD_INSTRUMENTATION", "reason": f"D8_M2_exceeds_D2:{capture}:LP{lp}"}
            h2, h3 = m2/d2, m3/d2
            worst_h2, worst_h3 = max(worst_h2, h2), max(worst_h3, h3)
            if h2 > .10 or h3 > .10:
                return {"status": "HOLD_INSTRUMENTATION", "reason": f"D8_hot_gate_fail:{capture}:LP{lp}",
                        "H2": h2, "H3_over_D2": h3}
            cold_offcore, cold_m3 = row.get("cold_offcore"), row.get("cold_M3")
            cold_lat, hot_lat = row.get("cold_latency_ns"), row.get("hot_latency_ns")
            if (type(cold_offcore) is not int or type(cold_m3) is not int or cold_offcore < 10_000 or
                    cold_m3 < 0 or cold_m3 > cold_offcore or not isinstance(cold_lat, list) or
                    not isinstance(hot_lat, list) or len(cold_lat) != 31 or len(hot_lat) != 31 or
                    any(not isinstance(x, (int, float)) or isinstance(x, bool) or not math.isfinite(x) or x <= 0
                        for x in cold_lat + hot_lat)):
                return {"status": "HOLD_INSTRUMENTATION", "reason": f"D8_cold_hot_measurement_invalid:{capture}:LP{lp}"}
            cold_ratio = statistics.median(cold_lat)/statistics.median(hot_lat)
            # S-19 §3 and the qualified eviction analyzer define this as the
            # fraction of cold reads that MISS LLC, using OFFCORE as denominator.
            cold_llc_miss = cold_m3/cold_offcore
            min_cold_ratio = min(min_cold_ratio, cold_ratio)
            min_cold_llc_miss = min(min_cold_llc_miss, cold_llc_miss)
            if cold_ratio < 1.50 or cold_llc_miss < .90:
                return {"status": "HOLD_INSTRUMENTATION", "reason": f"D8_cold_or_ratio_gate_fail:{capture}:LP{lp}",
                        "cold_hot_ratio": cold_ratio, "cold_llc_miss_fraction": cold_llc_miss}
    return {"status": "PASS", "capture_pair": ["Q", "R"], "worker_lps": list(P_LPS),
            "scope": "LP_WORKER", "max_H2": worst_h2, "max_H3_over_D2": worst_h3,
            "min_cold_hot_ratio": min_cold_ratio, "min_cold_llc_miss_fraction": min_cold_llc_miss,
            "window_ids_equal_within_capture": True, "window_ids_equal_between_captures": False,
            "aggregation": "per-core ratio of sums; no cross-core compensation"}


def _fixture_evidence(phase: str, request: dict[str, Any], outcome: str = "PASS") -> dict[str, Any]:
    plan = request["plan"]
    base = {"phase": phase, "session_id": request["session_id"],
            "plan_sha256": plan["sha256"], "topology_map_sha256": "f"*64}
    if phase == "preflight":
        return {**base, "state": outcome, "large_pages": True, "qpc_hz": 10_000_000,
                "worker_lps": list(P_LPS)}
    if phase == "qa_instruments_expulsion":
        return {**base, "state": outcome, "kernel_qa": "PASS", "instrumentation": "PASS",
                "expulsion_qa": "PASS",
                "reader_state": "QUALIFIED_FOR_REQUIRED_CHANNELS_WITH_DECLARED_LIMITATION"}
    if phase == "qualification":
        if request.get("mode") == "SMOKE":
            if outcome == "HOLD_INSTRUMENTATION":
                return {**base, "state": outcome, "reason": "injected_qualification_hold"}
            return {**base, "state": "SMOKE_ONLY", "numeric_metrics_used": False,
                    "bw_dram": "ACQUIRED_N3", "p_instr": "ACQUIRED_N3", "p_instr_variants": 9,
                    "N": 3, "all_workers_calibrated": True, "disassembly_sha256": "a"*64,
                    "acquired_aux_validation": {"complete": True, "reason": "none", "rows": 240,
                        "cells": 10, "P_instr_variants": 9, "BW_DRAM_rows": 24,
                        "workers_per_replica": 8, "R_common_by_cell": {"P_instr_c1_U8": 2}}}
        return {**base, "state": outcome, "selection_event_count": 11718,
                "comparison_event_count": 6696, "qualification_order_sha256": plan["qualification_order_sha256"],
                "valid_scientific_inputs": True, "failed_gates": (["G_ggml"] if outcome == "FALLIDA_CUALIFICACION" else []),
                "bw_dram": "PASS", "p_instr": "PASS", "reference_selection": "PASS",
                "comparison": "PASS", "R_common_by_unit": True, "all_31_samples": True}
    if phase == "residency":
        smoke = request.get("mode") == "SMOKE"
        return {**base, "state": "SMOKE_ONLY" if smoke else outcome,
                "N": request.get("N", 3), "numeric_metrics_used": False,
                "block_complete": True, "block_id": request.get("residency_block"),
                "event_count": 264 if smoke else plan["residency_event_count"],
                "selected_schedule_rows": 264 if smoke else plan["residency_event_count"],
                "calibration_events": 60 if smoke else 420,
                "event_order_matches_schedule": True,
                "controlled_emon_stop": True,
                "event_order_sha256": plan["residency_order_sha256"], "N": 3 if smoke else 31}
    if phase == "final_controls":
        return {**base, "state": outcome, "d8_captures": synthetic_d8_captures()}
    return {**base, "state": outcome, "verdict": "RESIDENCIA_PASS"}


def _test_adapter(request: dict[str, Any], *, qualification_state: str = "QUALIFIED_FOR_RESIDENCY",
                  hold_phase: str | None = None) -> ExecutableAcquisitionAdapter:
    def execute(command: list[str], *, cwd: Path, stdout_path: Path, stderr_path: Path) -> int:
        phase = command[command.index("--acquire-r17") + 1]
        output = Path(command[command.index("--output") + 1])
        outcome = "PASS"
        if phase == "qualification": outcome = qualification_state
        if phase == hold_phase: outcome = "HOLD_INSTRUMENTATION"
        evidence = _fixture_evidence(phase, request, outcome)
        if "--previous-evidence-sha256" in command:
            evidence["previous_evidence_sha256"] = command[command.index("--previous-evidence-sha256") + 1]
        _write_once(output, canonical_bytes(evidence) + b"\n")
        _write_once(stdout_path, f"TEST_EXECUTION_DOUBLE phase={phase}\n".encode())
        _write_once(stderr_path, b"")
        return 0
    return ExecutableAcquisitionAdapter(Path("execution_double_only.exe"), execute=execute)


def _make_request(output_dir: Path, plan_path: Path, plan: dict[str, Any], *,
                  mode: str, session_id: str, schedule_path: Path | None = None,
                  emon_path: Path | None = None) -> dict[str, Any]:
    output_dir = output_dir.resolve()
    try:
        output_dir.relative_to(OBJ.resolve())
    except ValueError as exc:
        raise GateError("acquisition_output_must_be_under_logs_r17_obj_r17") from exc
    return {"output_dir": output_dir, "plan_path": plan_path.resolve(), "plan": plan,
            "plan_sha256": plan["sha256"], "session_id": session_id,
            "schedule_path": (schedule_path or (HARNESS / "logs_r17" / "obj_r17" / "residency_smoke_schedule_r17.tsv")).resolve(),
            "disassembly_path": (output_dir / "disassembly_qa_r17.json").resolve(),
            "emon_path": str(emon_path.resolve()) if emon_path else None,
            "edp_path": (output_dir / "events.edp").resolve(),
            "capture_budget_path": (output_dir / "capture_budget_r17.json").resolve(),
            "mode": mode, "N": 3, "residency_block": "W288|O1024|r1"}


def d1_d8_self_test(output: Path) -> list[str]:
    import residency_plan_r17 as plans
    plan = plans.build_payload()
    plan_path = OBJ / f"d1_test_plan_r17_{time.time_ns()}.json"
    _write_once(plan_path, (json.dumps(plan, ensure_ascii=True, indent=2) + "\n").encode())
    plan = plans.verify_plan(plan_path)
    lines = []

    auth_bindings = {"integrated_executable_sha256": "1" * 64,
        "loaded_libraries_root": r"C:\Projects\OMEGA\.venv_cuda\Lib\site-packages\torch\lib",
        "loaded_libraries_sha256": {"torch.dll": "2" * 64, "torch_cpu.dll": "3" * 64, "c10.dll": "4" * 64},
        "plan_sha256": "5" * 64, "segmentation_sha256": "6" * 64,
        "qualification_units_sha256": "7" * 64, "s25_replica_orders_sha256": "8" * 64,
        "governing_validator_sha256": "9" * 64}
    auth = {"schema_version": "T0P_AUTHORIZED_R17_V1", "session_id": "synthetic-r17",
        "acquisition_phases": list(ACQUISITION_PHASES), "bindings": auth_bindings,
        "retry_policy": {"retryable_reasons": list(RETRYABLE_REASONS),
            "global_technical_retry_budget": 1, "planned_replica_pause_consumes_budget": False}}
    validate_authorization_document(auth, auth_bindings, "synthetic-r17")
    auth_mutations = 0
    for field in ("schema_version", "session_id", "acquisition_phases", "bindings", "retry_policy"):
        mutated = json.loads(json.dumps(auth)); mutated[field] = "MUTATED"
        try: validate_authorization_document(mutated, auth_bindings, "synthetic-r17")
        except GateError: auth_mutations += 1
        else: raise GateError(f"AUTHORIZED_mutation_accepted:{field}")
    try: validate_authorization_document(auth, auth_bindings, "different-session")
    except GateError: auth_mutations += 1
    else: raise GateError("AUTHORIZED_session_binding_mutation_accepted")
    for field in auth_bindings:
        mutated = json.loads(json.dumps(auth))
        if field == "loaded_libraries_sha256":
            mutated["bindings"][field]["torch.dll"] = "f" * 64
        else:
            mutated["bindings"][field] = "f" * 64
        try: validate_authorization_document(mutated, auth_bindings, "synthetic-r17")
        except GateError: auth_mutations += 1
        else: raise GateError(f"AUTHORIZED_binding_mutation_accepted:{field}")
    for name in ("torch_cpu.dll", "c10.dll"):
        mutated = json.loads(json.dumps(auth)); mutated["bindings"]["loaded_libraries_sha256"][name] = "f" * 64
        try: validate_authorization_document(mutated, auth_bindings, "synthetic-r17")
        except GateError: auth_mutations += 1
        else: raise GateError(f"AUTHORIZED_library_mutation_accepted:{name}")
    lines.append(f"D14_authorization=PASS field_mutations_rejected={auth_mutations} phases=6 retry_budget=global_one")

    no_preflight_dir = OBJ / "synthetic_missing_preflight_r17"
    s20_hold = _s20_missing_preflight_hold(no_preflight_dir)
    if s20_hold != {"state": "HOLD_INSTRUMENTATION",
                    "reason": "residency_preflight_not_written_before_acquisition_hold"}:
        raise GateError(f"D10_missing_preflight_did_not_fail_closed:{s20_hold}")
    lines.append("D10_postprocess=PASS missing_governing_preflight_fails_closed")

    run_id = str(time.time_ns())
    positive_dir = OBJ / "synthetic_d1_r17" / run_id / "positive"
    positive_dir.mkdir(parents=True, exist_ok=True)
    positive_request = _make_request(positive_dir, plan_path, plan, mode="OFFICIAL", session_id="fixture-positive-r17")
    positive_adapter = _test_adapter(positive_request)
    positive = run_acquisition_chain(positive_adapter, plan=plan, request=positive_request)
    expected_calls = ["preflight", "qa_instruments_expulsion", "qualification",
                      "residency", "final_controls", "derivation_verdict"]
    if positive["state"] != "RESIDENCIA_PASS" or positive["acquisition_calls"] != expected_calls:
        raise GateError(f"D1_positive_acquisition_chain_failed:{positive['state']}:{positive['acquisition_calls']}")
    lines.append("D1_positive=PASS calls=preflight,QA+instrumentos+expulsion,qualification,residency,D8,derivation")

    fail_dir = OBJ / "synthetic_d1_r17" / run_id / "qualification_fail"
    fail_dir.mkdir(parents=True, exist_ok=True)
    fail_request = _make_request(fail_dir, plan_path, plan, mode="OFFICIAL", session_id="fixture-fail-r17")
    fail_adapter = _test_adapter(fail_request, qualification_state="FALLIDA_CUALIFICACION")
    fail = run_acquisition_chain(fail_adapter, plan=plan, request=fail_request)
    if fail["state"] != "FALLIDA_CUALIFICACION" or fail["residency_started"] or \
            fail["acquisition_calls"] != ["preflight", "qa_instruments_expulsion", "qualification"]:
        raise GateError(f"D1_qualification_cut_failed:{fail['state']}:{fail['acquisition_calls']}")
    lines.append("D1_injection=PASS FALLIDA_CUALIFICACION -> automatic_cut STOP; residency_adapter_calls=0")

    hold_dir = OBJ / "synthetic_d1_r17" / run_id / "qa_hold"
    hold_dir.mkdir(parents=True, exist_ok=True)
    hold_request = _make_request(hold_dir, plan_path, plan, mode="OFFICIAL", session_id="fixture-hold-r17")
    hold_adapter = _test_adapter(hold_request, hold_phase="qa_instruments_expulsion")
    hold = run_acquisition_chain(hold_adapter, plan=plan, request=hold_request)
    if hold["state"] != "HOLD_INSTRUMENTATION" or hold["residency_started"] or \
            hold["acquisition_calls"] != ["preflight", "qa_instruments_expulsion"]:
        raise GateError(f"D1_QA_hold_failed:{hold['state']}:{hold['acquisition_calls']}")
    lines.append("D1_injection=PASS QA/instrument HOLD -> qualification/residency adapter calls=0")

    qhold_dir = OBJ / "synthetic_d1_r17" / run_id / "qualification_hold"
    qhold_dir.mkdir(parents=True, exist_ok=True)
    qhold_request = _make_request(qhold_dir, plan_path, plan, mode="OFFICIAL", session_id="fixture-qhold-r17")
    qhold_adapter = _test_adapter(qhold_request, hold_phase="qualification")
    qhold = run_acquisition_chain(qhold_adapter, plan=plan, request=qhold_request)
    if qhold["state"] != "HOLD_INSTRUMENTATION" or qhold["residency_started"] or \
            qhold["acquisition_calls"] != ["preflight", "qa_instruments_expulsion", "qualification"]:
        raise GateError(f"D1_qualification_hold_failed:{qhold['state']}:{qhold['acquisition_calls']}")
    lines.append("D1_injection=PASS qualification HOLD -> residency adapter calls=0")

    smoke_dir = OBJ / "synthetic_d1_r17" / run_id / "diagnostic_smoke"
    smoke_dir.mkdir(parents=True, exist_ok=True)
    smoke_request = _make_request(smoke_dir, plan_path, plan, mode="SMOKE", session_id="fixture-smoke-r17")
    smoke_adapter = _test_adapter(smoke_request)
    smoke_result = run_acquisition_chain(smoke_adapter, plan=plan, request=smoke_request)
    if smoke_result["state"] != "SMOKE_ONLY" or smoke_result["acquisition_calls"] != [
            "preflight", "qa_instruments_expulsion", "qualification", "residency"]:
        raise GateError(f"D1_diagnostic_smoke_transition_failed:{smoke_result['state']}:{smoke_result['acquisition_calls']}")
    lines.append("D1_smoke=PASS minimum qualification diagnostic cut -> one complete residency slice; no final controls")

    for name, captures, expected in (
        ("H2_over_0_10", synthetic_d8_captures(bad_h2=("Q", 14)), "D8_hot_gate_fail"),
        ("H3_over_0_10", synthetic_d8_captures(bad_h3=("R", 0)), "D8_hot_gate_fail"),
        ("denominator_under_10000", synthetic_d8_captures(low_denominator=("Q", 2)), "D8_denominator_NOT_ESTIMABLE"),
        ("missing_LP", synthetic_d8_captures(missing_lp=("R", 14)), "D8_LP_coverage_invalid"),
        ("capture_window_mismatch", synthetic_d8_captures(mixed_windows=True), "D8_capture_window_alignment_invalid"),
        ("numerator_denominator_window_mismatch", synthetic_d8_captures(mixed_counter_windows=True), "D8_mixed_windows_within_capture"),
        ("cold_ratio_below_1_50", synthetic_d8_captures(bad_cold_ratio=("Q", 0)), "D8_cold_or_ratio_gate_fail"),
        ("cold_LLC_miss_below_0_90", synthetic_d8_captures(bad_cold_llc=("R", 14)), "D8_cold_or_ratio_gate_fail"),
    ):
        result = validate_d8_post_revalidation(captures)
        if result["status"] != "HOLD_INSTRUMENTATION" or not result["reason"].startswith(expected):
            raise GateError(f"D8_negative_not_rejected:{name}:{result}")
        lines.append(f"D8_negative={name} HOLD PASS")

    valid_cold = validate_d8_post_revalidation(synthetic_d8_captures())
    if valid_cold.get("status") != "PASS" or abs(valid_cold.get("min_cold_llc_miss_fraction", 0) - .996) > 1e-12:
        raise GateError(f"D8_valid_cold_eviction_not_accepted:{valid_cold}")
    lines.append("OP1_positive=PASS cold_M3/OFFCORE=0.996 (cold LLC misses >= 0.90)")
    old_inverted = synthetic_d8_captures()
    for row in old_inverted["Q"]:
        row["cold_M3"] = 100  # would have passed the former inverted LLC-hit check
    inverted_rejected = validate_d8_post_revalidation(old_inverted)
    if inverted_rejected.get("status") != "HOLD_INSTRUMENTATION" or not str(inverted_rejected.get("reason", "")).startswith("D8_cold_or_ratio_gate_fail"):
        raise GateError(f"OP1_inverted_false_positive_still_passes:{inverted_rejected}")
    lines.append("OP1_negative=HOLD cold_M3/OFFCORE=0.001 (cold LLC hit >= 0.90) rejected")

    distinct = synthetic_d8_captures()
    for row in distinct["R"]:
        row_ids = [f"R-new-window-{i}" for i in range(1, 33)]
        row["window_ids_by_counter"] = {channel: list(row_ids) for channel in ("D2", "M2", "M3")}
        row["cold_hot_window_ids"] = list(row_ids)
    accepted = validate_d8_post_revalidation(distinct)
    if accepted["status"] != "PASS" or accepted.get("window_ids_equal_between_captures") is not False:
        raise GateError(f"D8_distinct_Q_R_windows_rejected:{accepted}")
    lines.append("D8_Q_R=PASS different capture IDs accepted; IDs equal only within each capture/scope")

    output.parent.mkdir(parents=True, exist_ok=True)
    _write_once(output, ("OFFICIAL_RUN_R17_SYNTHETIC_QA PASS\n" + "\n".join(lines) + "\n").encode())
    return lines


def run_real_smoke(*, executable: Path, plan_path: Path, attempt_dir: Path,
                   emon_path: Path | None = None) -> dict[str, Any]:
    import residency_plan_r17 as plans
    import telemetry_r17 as telemetry
    plan = plans.verify_plan(plan_path)
    if not attempt_dir.is_dir():
        raise GateError("smoke_attempt_must_be_prepared_by_telemetry_r17")
    free = shutil.disk_usage("C:\\").free
    if free < 4 * 1024**3:
        raise GateError(f"STOP_BEFORE_SMOKE_C_FREE_BELOW_4G:{free}")
    budget_path = attempt_dir / "capture_budget_r17.json"
    if not budget_path.is_file() or strict_json_bytes(budget_path.read_bytes()).get("estimated_max_raw_bytes", 1024**3) >= 1024**3:
        raise GateError("STOP_BEFORE_SMOKE_CAPTURE_BUDGET_MISSING_OR_AT_LEAST_1G")
    request = _make_request(attempt_dir, plan_path, plan, mode="SMOKE", session_id="smoke-r17",
                            schedule_path=attempt_dir / "residency_smoke_schedule_r17.tsv", emon_path=emon_path)
    result = run_acquisition_chain(ExecutableAcquisitionAdapter(executable), plan=plan, request=request)
    missing_preflight = _s20_missing_preflight_hold(attempt_dir)
    if missing_preflight:
        result["s20_postprocess"] = missing_preflight
    else:
        try:
            s20 = telemetry.capture_postprocess(attempt_dir, attempt_dir / "telemetry_status_r17.json")
            result["s20_postprocess"] = {"state": s20.get("state"), "mode": s20.get("s20_mode"),
                "validation_sha256": s20.get("sha256"), "samples": s20.get("samples"),
                "emon_interval_groups": s20.get("emon_interval_groups")}
        except Exception as exc:
            result["s20_postprocess"] = {"state": "HOLD_INSTRUMENTATION",
                "reason": f"{type(exc).__name__}:{exc}"}
    _write_once(attempt_dir / "smoke_chain_summary_r17.json", canonical_bytes(result) + b"\n")
    return result


def _s20_missing_preflight_hold(attempt_dir: Path) -> dict[str, str] | None:
    if (attempt_dir / "preflight_r17.json").is_file():
        return None
    return {"state": "HOLD_INSTRUMENTATION",
            "reason": "residency_preflight_not_written_before_acquisition_hold"}


def run_instrument_qualification_smoke(*, executable: Path, plan_path: Path,
                                      schedule_path: Path, attempt_dir: Path,
                                      emon_path: Path) -> dict[str, Any]:
    """Run the authorized one-block S-25-B diagnostic with controlled EMON stop."""
    import telemetry_r17 as telemetry
    try:
        attempt_dir = attempt_dir.resolve()
        attempt_dir.relative_to(OBJ.resolve())
    except ValueError as exc:
        raise GateError("instrument_smoke_attempt_must_be_under_logs_r17_obj_r17") from exc
    if attempt_dir.exists():
        raise GateError("instrument_smoke_requires_new_attempt_directory")
    if not executable.is_file() or not plan_path.is_file() or not schedule_path.is_file() or not emon_path.is_file():
        raise GateError("instrument_smoke_required_file_missing")
    free_before = shutil.disk_usage("C:\\").free
    if free_before < 4 * 1024**3:
        raise GateError(f"STOP_BEFORE_SMOKE_C_FREE_BELOW_4G:{free_before}")
    edp_source = ROOT / "T0P" / "telemetry" / "reader" / "emon_joint_qualification.edp"
    budget = telemetry.prepare(attempt_dir, plan_path, schedule_path, edp_source)
    if budget["schedule_mode"] != "SMOKE_N1" or budget["schedule_events"] != 6:
        raise GateError("instrument_smoke_requires_frozen_S25B_SMOKE_N1_schedule")
    if shutil.disk_usage("C:\\").free < 4 * 1024**3:
        raise GateError("STOP_BEFORE_EMON_C_FREE_BELOW_4G_AFTER_PREPARE")
    env = os.environ.copy()
    torch_lib = r"C:\Projects\OMEGA\.venv_cuda\Lib\site-packages\torch\lib"
    ggml_bin = str(ROOT / "T0P" / "ref_ggml" / "build_llama" / "bin")
    env["PATH"] = os.pathsep.join([torch_lib, ggml_bin, str(emon_path.parent), env.get("PATH", "")])
    emon_stdout_path, emon_stderr_path = attempt_dir / "emon_raw.txt", attempt_dir / "emon_stderr.txt"
    harness_stdout_path = attempt_dir / "instrument_qualification_stdout_r17.txt"
    harness_stderr_path = attempt_dir / "instrument_qualification_stderr_r17.txt"
    emon_out = emon_stdout_path.open("xb")
    emon_err = emon_stderr_path.open("xb")
    emon_proc = harness_proc = None
    qpc_frequency = ctypes.c_longlong()
    if not ctypes.windll.kernel32.QueryPerformanceFrequency(ctypes.byref(qpc_frequency)):
        emon_out.close(); emon_err.close()
        raise GateError("parent_QPF_read_failed")
    def qpc() -> int:
        value = ctypes.c_longlong()
        if not ctypes.windll.kernel32.QueryPerformanceCounter(ctypes.byref(value)):
            raise GateError("parent_QPC_read_failed")
        return int(value.value)
    start_qpc = end_qpc = None
    stop_exit = emon_exit = None
    failure = None
    try:
        emon_proc = subprocess.Popen([str(emon_path), "-collect-edp", f"edp_file={attempt_dir / 'events.edp'}"],
            cwd=attempt_dir, stdout=emon_out, stderr=emon_err,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0), env=env)
        start_qpc = qpc()
        time.sleep(2.0)
        if emon_proc.poll() is not None:
            raise GateError(f"EMON_start_failed_exit={emon_proc.returncode}")
        command = [str(executable), "--instrument-qualification-r17", str(attempt_dir),
                   str(plan_path.resolve()), str(schedule_path.resolve())]
        with harness_stdout_path.open("xb") as stdout, harness_stderr_path.open("xb") as stderr:
            harness_proc = subprocess.Popen(command, cwd=ROOT, stdout=stdout, stderr=stderr,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0), env=env)
            deadline = time.monotonic() + float(budget["maximum_capture_s"]) + 120.0
            while harness_proc.poll() is None:
                free = shutil.disk_usage("C:\\").free
                used = sum(path.stat().st_size for path in attempt_dir.iterdir() if path.is_file())
                if free < 4 * 1024**3 or used >= 1024**3:
                    harness_proc.kill()
                    raise GateError(f"STOP_DURING_SMOKE:free_bytes={free}:attempt_bytes={used}")
                if time.monotonic() > deadline:
                    harness_proc.kill()
                    raise GateError("instrument_qualification_capture_budget_deadline_exceeded")
                time.sleep(.25)
        if harness_proc.returncode != 0:
            stderr_tail = harness_stderr_path.read_text(encoding="utf-8", errors="replace")[-800:]
            raise GateError(f"instrument_qualification_acquisition_exit={harness_proc.returncode}:{stderr_tail}")
    except Exception as exc:
        failure = f"{type(exc).__name__}:{exc}"
    finally:
        if emon_proc is not None and emon_proc.poll() is None:
            stop = subprocess.run([str(emon_path), "-stop"], cwd=attempt_dir,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0), env=env, check=False)
            stop_exit = stop.returncode
            try:
                emon_exit = emon_proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                emon_proc.kill(); emon_exit = emon_proc.wait(timeout=10)
        elif emon_proc is not None:
            emon_exit = emon_proc.returncode
        end_qpc = qpc()
        emon_out.close(); emon_err.close()
    capture_bytes = emon_stdout_path.stat().st_size if emon_stdout_path.exists() else 0
    timing = {"qpc_start": start_qpc, "qpc_end": end_qpc, "qpf_hz": qpc_frequency.value,
              "qpc_source": "parent_QueryPerformanceCounter", "stop_command": "emon -stop",
              "stop_exit": stop_exit, "emon_exit": emon_exit, "capture_bytes": capture_bytes,
              "controlled_stop": stop_exit == 0 and emon_exit == 0}
    _write_once(attempt_dir / "capture_timing_r17.json", canonical_bytes(timing) + b"\n")
    if failure is None and (stop_exit != 0 or emon_exit != 0):
        failure = f"EMON_controlled_stop_failed:stop_exit={stop_exit}:emon_exit={emon_exit}"
    if failure is None and capture_bytes >= 1024**3:
        failure = f"EMON_capture_at_least_1GiB:{capture_bytes}"
    if failure is None:
        status = telemetry.capture_postprocess(attempt_dir, attempt_dir / "telemetry_status_r17.json")
        verdict = status.get("instrumentation_qualification", {})
        result = {"state": "ACQUISITION_COMPLETE" if verdict.get("verdict") == "INSTRUMENTATION_QUALIFICATION_PASS" else "HOLD_INSTRUMENTATION_QUALIFICATION",
                  "verdict": verdict.get("verdict"), "reason": verdict.get("reason", "none"),
                  "s20_state": status.get("state"), "mode": status.get("s20_mode"),
                  "attempt_directory": str(attempt_dir), "capture_bytes": capture_bytes,
                  "controlled_emon_stop": timing["controlled_stop"]}
        print(json.dumps(result, sort_keys=True, ensure_ascii=True))
        return result
    failure_record = {"state": "SMOKE_BLOCKED_OR_FAILED", "reason": failure,
                      "attempt_bytes": sum(path.stat().st_size for path in attempt_dir.iterdir() if path.is_file()),
                      "capture_bytes": capture_bytes, "controlled_emon_stop": timing["controlled_stop"]}
    evidence = OBJ / f"instrument_smoke_blocker_{time.time_ns()}.json"
    _write_once(evidence, canonical_bytes(failure_record) + b"\n")
    shutil.rmtree(attempt_dir)
    raise GateError(f"{failure}; failed attempt removed; blocker_evidence={evidence}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--self-test", action="store_true")
    modes.add_argument("--smoke", action="store_true")
    modes.add_argument("--instrument-smoke", action="store_true")
    modes.add_argument("--official", action="store_true")
    parser.add_argument("--integrated-executable", type=Path, default=INTEGRATED_EXE)
    parser.add_argument("--plan", type=Path, default=PLAN_PATH)
    parser.add_argument("--output", type=Path, default=OBJ / f"official_run_r17_qa_{time.time_ns()}.txt")
    parser.add_argument("--attempt-directory", type=Path, default=OBJ / "smoke_chain_r17")
    parser.add_argument("--emon", type=Path)
    parser.add_argument("--authorized", type=Path)
    parser.add_argument("--session-id")
    parser.add_argument("--schedule", type=Path, default=OBJ / "instrument_qualification_smoke_n1_schedule_r17.tsv")
    ns = parser.parse_args(argv)
    try:
        if ns.official:
            if ns.authorized is None or ns.session_id is None:
                print("OFFICIAL_R17_REJECT AUTHORIZED_path_and_session_id_required; measurements_started=false")
                return 20
            verify_authorization_file(ns.authorized, ns.plan, ns.integrated_executable, ns.session_id)
            print("OFFICIAL_R17_REJECT session_official_prohibited_in_this_task; measurements_started=false")
            return 20
        if ns.self_test:
            lines = d1_d8_self_test(ns.output)
            print(f"OFFICIAL_RUN_R17_SYNTHETIC_QA PASS cases={len(lines)} adapter_execution_double_only=true evidence={ns.output}")
            return 0
        if ns.instrument_smoke:
            if ns.emon is None:
                raise GateError("--instrument-smoke requires the EMON executable path")
            result = run_instrument_qualification_smoke(executable=ns.integrated_executable,
                plan_path=ns.plan, schedule_path=ns.schedule, attempt_dir=ns.attempt_directory,
                emon_path=ns.emon)
            return 0 if result["state"] == "ACQUISITION_COMPLETE" else 3
        result = run_real_smoke(executable=ns.integrated_executable,
                                plan_path=ns.plan, attempt_dir=ns.attempt_directory, emon_path=ns.emon)
        print(json.dumps(result, sort_keys=True, ensure_ascii=True))
        return 0 if result["state"] == "SMOKE_ONLY" else 3
    except (OSError, GateError, KeyError, TypeError, ValueError) as exc:
        print(f"OFFICIAL_RUN_R17 REJECT {type(exc).__name__}: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
