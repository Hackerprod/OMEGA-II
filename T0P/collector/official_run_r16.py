#!/usr/bin/env python3
"""Sequential S-24 acquisition gate. Every transition follows its acquisition."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Callable

ROOT = Path(__file__).resolve().parents[2]
COLLECTOR = Path(__file__).resolve().parent
HARNESS = ROOT / "T0P" / "harness"
OBJ = COLLECTOR / "logs_r16" / "obj_r16"
PLAN_PATH = OBJ / "residency_plan_r16.json"
INTEGRATED_EXE = HARNESS / "logs_r16" / "obj_r16" / "harness_r16.exe"
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


class AcquisitionAdapter:
    """Explicit boundary used by the gate to acquire one phase at a time."""

    def acquire(self, phase: str, request: dict[str, Any],
                previous: dict[str, Any] | None) -> dict[str, Any]:
        raise NotImplementedError


class ExecutableAcquisitionAdapter(AcquisitionAdapter):
    """Launch the integrated executable; tests may replace only process execution."""

    def __init__(self, executable: Path, *,
                 execute: Callable[..., Any] | None = None):
        self.executable = executable
        self.execute = execute or self._execute_process
        self.calls: list[str] = []

    @staticmethod
    def _execute_process(command: list[str], *, cwd: Path,
                         stdout_path: Path, stderr_path: Path) -> int:
        with stdout_path.open("xb") as stdout, stderr_path.open("xb") as stderr:
            completed = subprocess.run(command, cwd=cwd, stdout=stdout,
                                       stderr=stderr, check=False)
        return completed.returncode

    def acquire(self, phase: str, request: dict[str, Any],
                previous: dict[str, Any] | None) -> dict[str, Any]:
        if phase not in ACQUISITION_PHASES:
            raise GateError(f"unsupported_acquisition_phase:{phase}")
        if self.execute == self._execute_process and not self.executable.is_file():
            raise GateError(f"integrated_executable_missing:{self.executable}")
        out_dir: Path = request["output_dir"]
        evidence_path = out_dir / f"phase_{phase}_r16.json"
        stdout_path = out_dir / f"phase_{phase}_stdout_r16.txt"
        stderr_path = out_dir / f"phase_{phase}_stderr_r16.txt"
        if any(path.exists() for path in (evidence_path, stdout_path, stderr_path)):
            raise GateError(f"phase_artifact_exists:{phase}")
        command = [str(self.executable), "--acquire-r16", phase,
                   "--plan", str(request["plan_path"]),
                   "--session-id", request["session_id"],
                   "--output", str(evidence_path),
                   "--mode", request.get("mode", "SMOKE")]
        if request.get("mode") == "SMOKE":
            command.extend(["--N", str(request.get("N", 3)),
                            "--residency-block", request.get("residency_block", "W288|O1024|r1")])
        if previous:
            command.extend(["--previous-evidence-sha256", previous["sha256"]])
        self.calls.append(phase)
        rc = self.execute(command, cwd=ROOT, stdout_path=stdout_path,
                          stderr_path=stderr_path)
        if rc != 0:
            stderr = stderr_path.read_text(encoding="utf-8", errors="replace") if stderr_path.exists() else ""
            raise GateError(f"acquisition_process_failed:{phase}:exit={rc}:{stderr[-500:]}")
        if not evidence_path.is_file():
            raise GateError(f"acquisition_evidence_missing:{phase}")
        evidence = strict_json_bytes(evidence_path.read_bytes())
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
    required = required and e.get("reader_state") == "QUALIFIED_FOR_REQUIRED_CHANNELS_WITH_DECLARED_LIMITATION"
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


def _validate_qualification(e: dict[str, Any], plan: dict[str, Any], previous: dict[str, Any]) -> dict[str, Any]:
    state = e.get("state")
    if e.get("plan_sha256") != plan["sha256"] or e.get("topology_map_sha256") != previous["topology_map_sha256"]:
        return _explicit_stage("HOLD_INSTRUMENTATION", "qualification", reason="qualification_plan_or_map_identity_mismatch")
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


def _qualification_cut(stage: dict[str, Any]) -> dict[str, Any]:
    if stage["status"] == "PASS":
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
        if e.get("N") != 3 or e.get("numeric_metrics_used") is not False or e.get("block_complete") is not True:
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
    name = f"gate_{seq:02d}_{stage['phase']}_r16.json"
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
        "qualification": lambda e, p: _validate_qualification(e, plan, p),
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
            cut = _qualification_cut(stage)
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
    result = {"state": state, "phases": phases,
              "acquisition_calls": list(getattr(adapter, "calls", [])),
              "residency_started": any(x["phase"] == "residency" for x in phases),
              "numeric_metrics_used": request.get("mode") == "OFFICIAL"}
    _write_once(request["output_dir"] / "acquisition_chain_r16.json", canonical_bytes(result) + b"\n")
    return result


def synthetic_d8_captures(*, bad_h2: tuple[str, int] | None = None,
                          bad_h3: tuple[str, int] | None = None,
                          low_denominator: tuple[str, int] | None = None,
                          missing_lp: tuple[str, int] | None = None,
                          mixed_windows: bool = False,
                          bad_cold_ratio: tuple[str, int] | None = None,
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
            rows.append({"lp": lp, "scope": "LP_WORKER", "capture": capture,
                         "window_ids_by_counter": counter_ids,
                         "D2": d2, "M2": m2, "M3": m3,
                         "cold_D2": 100_000, "cold_M3": 100,
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
    min_cold_ratio, min_cold_llc_hit = float("inf"), 1.0
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
            cold_d2, cold_m3 = row.get("cold_D2"), row.get("cold_M3")
            cold_lat, hot_lat = row.get("cold_latency_ns"), row.get("hot_latency_ns")
            if (type(cold_d2) is not int or type(cold_m3) is not int or cold_d2 < 10_000 or
                    cold_m3 < 0 or cold_m3 > cold_d2 or not isinstance(cold_lat, list) or
                    not isinstance(hot_lat, list) or len(cold_lat) != 31 or len(hot_lat) != 31 or
                    any(not isinstance(x, (int, float)) or isinstance(x, bool) or not math.isfinite(x) or x <= 0
                        for x in cold_lat + hot_lat)):
                return {"status": "HOLD_INSTRUMENTATION", "reason": f"D8_cold_hot_measurement_invalid:{capture}:LP{lp}"}
            cold_ratio = statistics.median(cold_lat)/statistics.median(hot_lat)
            cold_llc_hit = 1.0 - cold_m3/cold_d2
            min_cold_ratio = min(min_cold_ratio, cold_ratio)
            min_cold_llc_hit = min(min_cold_llc_hit, cold_llc_hit)
            if cold_ratio < 1.50 or cold_llc_hit < .90:
                return {"status": "HOLD_INSTRUMENTATION", "reason": f"D8_cold_or_ratio_gate_fail:{capture}:LP{lp}",
                        "cold_hot_ratio": cold_ratio, "cold_llc_hit_ratio": cold_llc_hit}
    return {"status": "PASS", "capture_pair": ["Q", "R"], "worker_lps": list(P_LPS),
            "scope": "LP_WORKER", "max_H2": worst_h2, "max_H3_over_D2": worst_h3,
            "min_cold_hot_ratio": min_cold_ratio, "min_cold_llc_hit_ratio": min_cold_llc_hit,
            "window_ids_equal_within_capture": True, "window_ids_equal_between_captures": False,
            "aggregation": "per-core ratio of sums; no cross-core compensation"}


def _fixture_evidence(phase: str, request: dict[str, Any], outcome: str = "PASS") -> dict[str, Any]:
    plan = request["plan"]
    base = {"phase": phase, "session_id": request["session_id"],
            "plan_sha256": plan["sha256"], "topology_map_sha256": "f"*64}
    if phase == "preflight":
        return {**base, "state": outcome, "large_pages": True, "qpc_hz": 10_000_000,
                "worker_lps": list(P_LPS),
                "reader_state": "QUALIFIED_FOR_REQUIRED_CHANNELS_WITH_DECLARED_LIMITATION"}
    if phase == "qa_instruments_expulsion":
        return {**base, "state": outcome, "kernel_qa": "PASS", "instrumentation": "PASS",
                "expulsion_qa": "PASS",
                "reader_state": "QUALIFIED_FOR_REQUIRED_CHANNELS_WITH_DECLARED_LIMITATION"}
    if phase == "qualification":
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
                "event_count": 68 if smoke else plan["residency_event_count"],
                "event_order_sha256": plan["residency_order_sha256"], "N": 3 if smoke else 31}
    if phase == "final_controls":
        return {**base, "state": outcome, "d8_captures": synthetic_d8_captures()}
    return {**base, "state": outcome, "verdict": "RESIDENCIA_PASS"}


def _test_adapter(request: dict[str, Any], *, qualification_state: str = "QUALIFIED_FOR_RESIDENCY",
                  hold_phase: str | None = None) -> ExecutableAcquisitionAdapter:
    def execute(command: list[str], *, cwd: Path, stdout_path: Path, stderr_path: Path) -> int:
        phase = command[command.index("--acquire-r16") + 1]
        output = Path(command[command.index("--output") + 1])
        outcome = "PASS"
        if phase == "qualification": outcome = qualification_state
        if phase == hold_phase: outcome = "HOLD_INSTRUMENTATION"
        _write_once(output, canonical_bytes(_fixture_evidence(phase, request, outcome)) + b"\n")
        _write_once(stdout_path, f"TEST_EXECUTION_DOUBLE phase={phase}\n".encode())
        _write_once(stderr_path, b"")
        return 0
    return ExecutableAcquisitionAdapter(Path("execution_double_only.exe"), execute=execute)


def _make_request(output_dir: Path, plan_path: Path, plan: dict[str, Any], *,
                  mode: str, session_id: str) -> dict[str, Any]:
    output_dir = output_dir.resolve()
    try:
        output_dir.relative_to(OBJ.resolve())
    except ValueError as exc:
        raise GateError("acquisition_output_must_be_under_logs_r16_obj_r16") from exc
    return {"output_dir": output_dir, "plan_path": plan_path.resolve(), "plan": plan,
            "plan_sha256": plan["sha256"], "session_id": session_id,
            "mode": mode, "N": 3, "residency_block": "W288|O1024|r1"}


def d1_d8_self_test(output: Path) -> list[str]:
    import residency_plan_r16 as plans
    plan = plans.build_payload()
    plan_path = OBJ / "d1_test_plan_r16.json"
    if not plan_path.exists():
        _write_once(plan_path, (json.dumps(plan, ensure_ascii=True, indent=2) + "\n").encode())
    plan = plans.verify_plan(plan_path)
    lines = []

    run_id = str(time.time_ns())
    positive_dir = OBJ / "synthetic_d1_r16" / run_id / "positive"
    positive_dir.mkdir(parents=True, exist_ok=True)
    positive_request = _make_request(positive_dir, plan_path, plan, mode="OFFICIAL", session_id="fixture-positive-r16")
    positive_adapter = _test_adapter(positive_request)
    positive = run_acquisition_chain(positive_adapter, plan=plan, request=positive_request)
    expected_calls = ["preflight", "qa_instruments_expulsion", "qualification",
                      "residency", "final_controls", "derivation_verdict"]
    if positive["state"] != "RESIDENCIA_PASS" or positive["acquisition_calls"] != expected_calls:
        raise GateError(f"D1_positive_acquisition_chain_failed:{positive['state']}:{positive['acquisition_calls']}")
    lines.append("D1_positive=PASS calls=preflight,QA+instrumentos+expulsion,qualification,residency,D8,derivation")

    fail_dir = OBJ / "synthetic_d1_r16" / run_id / "qualification_fail"
    fail_dir.mkdir(parents=True, exist_ok=True)
    fail_request = _make_request(fail_dir, plan_path, plan, mode="OFFICIAL", session_id="fixture-fail-r16")
    fail_adapter = _test_adapter(fail_request, qualification_state="FALLIDA_CUALIFICACION")
    fail = run_acquisition_chain(fail_adapter, plan=plan, request=fail_request)
    if fail["state"] != "FALLIDA_CUALIFICACION" or fail["residency_started"] or \
            fail["acquisition_calls"] != ["preflight", "qa_instruments_expulsion", "qualification"]:
        raise GateError(f"D1_qualification_cut_failed:{fail['state']}:{fail['acquisition_calls']}")
    lines.append("D1_injection=PASS FALLIDA_CUALIFICACION -> automatic_cut STOP; residency_adapter_calls=0")

    hold_dir = OBJ / "synthetic_d1_r16" / run_id / "qa_hold"
    hold_dir.mkdir(parents=True, exist_ok=True)
    hold_request = _make_request(hold_dir, plan_path, plan, mode="OFFICIAL", session_id="fixture-hold-r16")
    hold_adapter = _test_adapter(hold_request, hold_phase="qa_instruments_expulsion")
    hold = run_acquisition_chain(hold_adapter, plan=plan, request=hold_request)
    if hold["state"] != "HOLD_INSTRUMENTATION" or hold["residency_started"] or \
            hold["acquisition_calls"] != ["preflight", "qa_instruments_expulsion"]:
        raise GateError(f"D1_QA_hold_failed:{hold['state']}:{hold['acquisition_calls']}")
    lines.append("D1_injection=PASS QA/instrument HOLD -> qualification/residency adapter calls=0")

    for name, captures, expected in (
        ("H2_over_0_10", synthetic_d8_captures(bad_h2=("Q", 14)), "D8_hot_gate_fail"),
        ("H3_over_0_10", synthetic_d8_captures(bad_h3=("R", 0)), "D8_hot_gate_fail"),
        ("denominator_under_10000", synthetic_d8_captures(low_denominator=("Q", 2)), "D8_denominator_NOT_ESTIMABLE"),
        ("missing_LP", synthetic_d8_captures(missing_lp=("R", 14)), "D8_LP_coverage_invalid"),
        ("capture_window_mismatch", synthetic_d8_captures(mixed_windows=True), "D8_capture_window_alignment_invalid"),
        ("numerator_denominator_window_mismatch", synthetic_d8_captures(mixed_counter_windows=True), "D8_mixed_windows_within_capture"),
        ("cold_ratio_below_1_50", synthetic_d8_captures(bad_cold_ratio=("Q", 0)), "D8_cold_or_ratio_gate_fail"),
    ):
        result = validate_d8_post_revalidation(captures)
        if result["status"] != "HOLD_INSTRUMENTATION" or not result["reason"].startswith(expected):
            raise GateError(f"D8_negative_not_rejected:{name}:{result}")
        lines.append(f"D8_negative={name} HOLD PASS")

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
    _write_once(output, ("OFFICIAL_RUN_R16_SYNTHETIC_QA PASS\n" + "\n".join(lines) + "\n").encode())
    return lines


def run_real_smoke(*, executable: Path, plan_path: Path, attempt_dir: Path) -> dict[str, Any]:
    import residency_plan_r16 as plans
    plan = plans.verify_plan(plan_path)
    attempt_dir.mkdir(parents=True, exist_ok=False)
    request = _make_request(attempt_dir, plan_path, plan, mode="SMOKE", session_id="smoke-r16")
    result = run_acquisition_chain(ExecutableAcquisitionAdapter(executable), plan=plan, request=request)
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--self-test", action="store_true")
    modes.add_argument("--smoke", action="store_true")
    modes.add_argument("--official", action="store_true")
    parser.add_argument("--integrated-executable", type=Path, default=INTEGRATED_EXE)
    parser.add_argument("--plan", type=Path, default=PLAN_PATH)
    parser.add_argument("--output", type=Path, default=OBJ / "official_run_r16_qa.txt")
    parser.add_argument("--attempt-directory", type=Path, default=OBJ / "smoke_chain_r16")
    ns = parser.parse_args(argv)
    try:
        if ns.official:
            print("OFFICIAL_R16_REJECT session_official_prohibited_in_this_task; no AUTHORIZED.json read; measurements_started=false")
            return 20
        if ns.self_test:
            lines = d1_d8_self_test(ns.output)
            print(f"OFFICIAL_RUN_R16_SYNTHETIC_QA PASS cases={len(lines)} adapter_execution_double_only=true evidence={ns.output}")
            return 0
        result = run_real_smoke(executable=ns.integrated_executable,
                                plan_path=ns.plan, attempt_dir=ns.attempt_directory)
        print(json.dumps(result, sort_keys=True, ensure_ascii=True))
        return 0 if result["state"] == "SMOKE_ONLY" else 3
    except (OSError, GateError, KeyError, TypeError, ValueError) as exc:
        print(f"OFFICIAL_RUN_R16 REJECT {type(exc).__name__}: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
