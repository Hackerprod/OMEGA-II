#!/usr/bin/env python3
"""Verify one complete R18 N=3 diagnostic acquisition slice."""
from __future__ import annotations

import argparse
import csv
import json
import math
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
from artifact_store_r18 import SessionAttemptStore
import residency_plan_r18 as plans

RAW_FILES = (
    "residency_plan_r18.json", "residency_smoke_schedule_r18.tsv", "events.edp", "emon_raw.txt", "emon_stderr.txt",
    "phase_preflight_r18.json", "phase_preflight_stdout_r18.txt", "phase_preflight_stderr_r18.txt",
    "phase_qa_instruments_expulsion_r18.json", "phase_qa_instruments_expulsion_stdout_r18.txt", "phase_qa_instruments_expulsion_stderr_r18.txt",
    "phase_qualification_r18.json", "phase_qualification_stdout_r18.txt", "phase_qualification_stderr_r18.txt",
    "phase_residency_r18.json", "phase_residency_stdout_r18.txt", "phase_residency_stderr_r18.txt",
    "acquisition_chain_r18.json",
    "capture_budget_r18.json", "capture_timing_r18.json", "disassembly_qa_r18.json",
    "harness_disassembly_r18.txt", "pinstr_symbol_map_r18.txt",
    "aux_qualification_raw_r18.jsonl", "aux_qualification_summary_r18.jsonl",
    "clock_map.csv", "block_marks_r18.csv", "residency_events_r18.jsonl", "residency_episodes_r18.jsonl",
    "residency_calibration_trials_r18.jsonl", "residency_margins_r18.jsonl",
    "residency_margin_episodes_r18.jsonl", "eviction_manifest_r18.jsonl", "preflight_r18.json",
    "segment_conditioning_r18.jsonl", "residency_load_marks_r18.jsonl", "effective_config_source.json",
)


class SmokeVerifyError(RuntimeError):
    pass


_PLAN_DERIVED_IDENTITY_FIELDS = {"replica", "block_id", "segment_id", "atomic_group_id",
                                 "within_block_seed", "within_block_order_sha256"}


def _verify_plan_identity(row: dict[str, Any], expected: dict[str, Any]) -> None:
    for field in ("phase", "condition_id", "unit_id", "pair_id", "observation", "variant",
                  "replica", "W_target_kib", "O", "m", "K", "block_id", "segment_id",
                  "atomic_group_id", "within_block_seed", "within_block_order_sha256"):
        if field not in expected:
            continue
        if field not in row and field in _PLAN_DERIVED_IDENTITY_FIELDS:
            # event_id was matched against the complete ordered schedule, so
            # these omitted fields remain bound to the frozen plan by ID.
            continue
        if row.get(field) != expected[field]:
            raise SmokeVerifyError(f"event_plan_identity_mismatch:{row.get('event_id')}:{field}")


def _pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise SmokeVerifyError(f"duplicate_json_key:{key}")
        result[key] = value
    return result


def jsonl(path: Path) -> list[dict[str, Any]]:
    out = []
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        for number, line in enumerate(stream, 1):
            if not line.strip():
                raise SmokeVerifyError(f"blank_jsonl_line:{path.name}:{number}")
            try:
                item = json.loads(line, object_pairs_hook=_pairs,
                                  parse_constant=lambda value: (_ for _ in ()).throw(SmokeVerifyError(f"nonfinite:{value}")))
            except json.JSONDecodeError as exc:
                raise SmokeVerifyError(f"invalid_JSON:{path.name}:line={number}:{exc}") from exc
            if not isinstance(item, dict):
                raise SmokeVerifyError(f"JSON_row_not_object:{path.name}:line={number}")
            out.append(item)
    return out


def _finite_positive(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) and value > 0


def _integer(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _verify_workers(row: dict[str, Any], *, required_fields: tuple[str, ...],
                    expected_lps: dict[int, int]) -> None:
    workers = row.get("worker_evidence", row.get("workers"))
    if not isinstance(workers, list) or len(workers) != 8:
        raise SmokeVerifyError(f"worker_evidence_count_invalid:{row.get('event_id')}")
    by_worker = {item.get("worker"): item for item in workers if isinstance(item, dict)}
    if set(by_worker) != set(range(8)):
        raise SmokeVerifyError(f"worker_evidence_identity_invalid:{row.get('event_id')}")
    for worker, item in by_worker.items():
        if item.get("lp") != expected_lps.get(worker):
            raise SmokeVerifyError(f"worker_to_LP_permutation_invalid:{row.get('event_id')}:worker={worker}")
        for field in required_fields:
            if not _integer(item.get(field)):
                raise SmokeVerifyError(f"worker_timestamp_not_integer:{row.get('event_id')}:{field}")
        if item["last_tsc"] <= item["first_tsc"] or item["last_qpc"] <= item["first_qpc"]:
            raise SmokeVerifyError(f"worker_timestamp_order_invalid:{row.get('event_id')}:worker={worker}")
        if not _finite_positive(item.get("kernel_only_seconds", item.get("kernel_seconds", item.get("kernel_tsc_sum", 0)))):
            raise SmokeVerifyError(f"worker_kernel_evidence_invalid:{row.get('event_id')}:worker={worker}")


def _verify_r_unit_calibrations(rows: list[dict[str, Any]], *,
                                expected_units: int | None = None,
                                expected_rows: int | None = None):
    cal_by_unit: dict[tuple[int, int, int, int], dict[str, set[int]]] = defaultdict(lambda: defaultdict(set))
    for row in rows:
        key = tuple(row.get(field) for field in ("W_target_kib", "O", "m", "K"))
        if (not all(_integer(value) and value > 0 for value in key) or
                row.get("variant") not in {"A", "B", "C"} or
                not _integer(row.get("R")) or row["R"] < 1):
            raise SmokeVerifyError("R_calibration_identity_or_value_invalid")
        cal_by_unit[key][row["variant"]].add(row["R"])
    if expected_rows is not None and len(rows) != expected_rows:
        raise SmokeVerifyError(f"R_calibration_row_count_invalid:{len(rows)}:{expected_rows}")
    if expected_units is not None and len(cal_by_unit) != expected_units:
        raise SmokeVerifyError(f"R_calibration_unit_count_invalid:{len(cal_by_unit)}:{expected_units}")
    for key, variants in cal_by_unit.items():
        if set(variants) != {"A", "B", "C"} or any(len(values) != 1 for values in variants.values()):
            raise SmokeVerifyError(f"R_calibration_A_B_C_not_common:{key}")
        if len({next(iter(values)) for values in variants.values()}) != 1:
            raise SmokeVerifyError(f"R_calibration_A_B_C_disagree:{key}")
    r_values = {next(iter(next(iter(variants.values())))) for variants in cal_by_unit.values()}
    if len(cal_by_unit) < 2 or len(r_values) < 2:
        raise SmokeVerifyError(f"R_by_W_m_K_not_distinct:{len(cal_by_unit)}_units:{sorted(r_values)}")
    return cal_by_unit, r_values


def _verify_aux_qualification(directory: Path, expected_lps: dict[int, int]) -> dict[str, int]:
    rows = jsonl(directory / "aux_qualification_raw_r18.jsonl")
    if len(rows) != 240:
        raise SmokeVerifyError(f"aux_qualification_row_count_invalid:{len(rows)}")
    groups: dict[tuple[str, str, int], list[dict[str, Any]]] = defaultdict(list)
    disassembly_hashes: set[str] = set()
    pvariants: set[tuple[int, int]] = set()
    for row in rows:
        provider, cell = row.get("provider"), row.get("cell")
        if row.get("label") != "SMOKE" or row.get("status") != "SMOKE_TIMED" or row.get("N") != 3:
            raise SmokeVerifyError(f"aux_qualification_label_or_N_invalid:{cell}")
        if row.get("all_workers_concurrent") is not True:
            raise SmokeVerifyError(f"aux_qualification_concurrency_invalid:{cell}")
        if row.get("warmup") != 5 or row.get("calibration_all_workers") is not True or row.get("calibration_target_seconds") != 2.5:
            raise SmokeVerifyError(f"aux_qualification_calibration_contract_invalid:{cell}")
        worker = row.get("worker")
        if worker not in expected_lps or row.get("lp") != expected_lps[worker] or row.get("affinity") != "PASS":
            raise SmokeVerifyError(f"aux_qualification_worker_LP_or_affinity_invalid:{cell}:{worker}")
        if row.get("sink_verified") is not True or not _finite_positive(row.get("t_app_us")) or row["t_app_us"] < 2_000_000:
            raise SmokeVerifyError(f"aux_qualification_sink_or_duration_invalid:{cell}:{worker}")
        if any(not _integer(row.get(field)) for field in ("R", "R_common", "tsc_start", "tsc_end", "qpc_start", "qpc_end")):
            raise SmokeVerifyError(f"aux_qualification_integer_timing_invalid:{cell}:{worker}")
        if row["R"] <= 0 or row["R"] != row["R_common"] or row["tsc_end"] <= row["tsc_start"] or row["qpc_end"] <= row["qpc_start"]:
            raise SmokeVerifyError(f"aux_qualification_R_or_timestamp_order_invalid:{cell}:{worker}")
        if provider == "P_instr":
            chains, unroll = row.get("chains"), row.get("U")
            if (chains, unroll) not in {(1, 8), (2, 8), (4, 8), (8, 8), (10, 8), (12, 8), (14, 8), (12, 16), (14, 16)}:
                raise SmokeVerifyError(f"P_instr_variant_invalid:{cell}")
            if row.get("frontier") != "prepared" or row.get("disassembly_qa") != "PASS":
                raise SmokeVerifyError(f"P_instr_frontier_or_disassembly_invalid:{cell}")
            digest = row.get("disassembly_sha256")
            if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
                raise SmokeVerifyError(f"P_instr_disassembly_hash_invalid:{cell}")
            disassembly_hashes.add(digest); pvariants.add((chains, unroll))
        elif provider == "BW_DRAM":
            if (cell != "BW_DRAM_256MiB_8P" or row.get("large_pages") is not True or
                    row.get("bytes_per_pass_per_worker") != 256 * 1024 * 1024 or row.get("frontier") != "prepared"):
                raise SmokeVerifyError(f"BW_DRAM_buffer_or_frontier_invalid:{cell}")
        else:
            raise SmokeVerifyError(f"aux_qualification_provider_invalid:{provider}")
        groups[(provider, cell, row.get("replica"))].append(row)
    if len(pvariants) != 9 or len(disassembly_hashes) != 1:
        raise SmokeVerifyError("P_instr_nine_variants_or_single_disassembly_not_proved")
    if len(groups) != 30:
        raise SmokeVerifyError(f"aux_qualification_replica_group_count_invalid:{len(groups)}")
    for (provider, cell, replica), group in groups.items():
        if replica not in {1, 2, 3} or len(group) != 8 or {row.get("worker") for row in group} != set(range(8)):
            raise SmokeVerifyError(f"aux_qualification_worker_multiplicity_invalid:{provider}:{cell}:r{replica}")
        if len({row["R"] for row in group}) != 1:
            raise SmokeVerifyError(f"aux_qualification_R_not_common:{provider}:{cell}:r{replica}")
    for (provider, cell, _replica), _group in groups.items():
        if provider == "P_instr":
            rates = {row["R"] for (p, c, _), rows_for_replica in groups.items() if p == provider and c == cell
                     for row in rows_for_replica}
            if len(rates) != 1:
                raise SmokeVerifyError(f"P_instr_R_not_fixed_across_N3:{cell}")
    return {"aux_rows": len(rows), "P_instr_variants": len(pvariants),
            "P_instr_replica_worker_rows": sum(row.get("provider") == "P_instr" for row in rows),
            "BW_DRAM_replica_worker_rows": sum(row.get("provider") == "BW_DRAM" for row in rows)}


def verify(attempt: Path) -> dict[str, Any]:
    store = SessionAttemptStore(attempt)
    directory = store.directory
    metadata = json.loads((directory / "attempt_metadata.json").read_text(encoding="utf-8"))
    if metadata.get("protocol") != "D13-write-once-v1":
        raise SmokeVerifyError("D13_protocol_missing")
    for name in RAW_FILES:
        if not (directory / name).is_file():
            raise SmokeVerifyError(f"raw_missing:{name}")
        store.verify_raw(name)
    preflight = json.loads((directory / "preflight_r18.json").read_text(encoding="utf-8"), object_pairs_hook=_pairs)
    topology_workers = preflight.get("P_core_map", {}).get("workers", [])
    expected_lps = {item.get("worker"): item.get("lp") for item in topology_workers if isinstance(item, dict)}
    if set(expected_lps) != set(range(8)) or len(set(expected_lps.values())) != 8:
        raise SmokeVerifyError("preflight_worker_to_LP_map_invalid")
    aux_summary = _verify_aux_qualification(directory, expected_lps)
    if not (directory / "s20_validation_r18.json").is_file():
        raise SmokeVerifyError("S20_diagnostic_result_missing")

    plan_path = directory / "residency_plan_r18.json"
    plan = plans.verify_plan(plan_path)
    selected = plans.smoke_schedule(plan, replicas=3)
    content = (directory / "residency_smoke_schedule_r18.tsv").read_text(encoding="utf-8").splitlines()
    hash_line = next((line for line in content if line.startswith("#plan_sha256=")), "")
    schedule = list(csv.DictReader([line for line in content if line and not line.startswith("#")], delimiter="\t"))
    ids = [row.get("event_id") for row in schedule]
    selected_ids = [event["id"] for event in selected]
    if hash_line != f"#plan_sha256={plan['sha256']}" or ids != selected_ids or len(set(ids)) != 264:
        raise SmokeVerifyError("schedule_plan_order_or_multiplicity_invalid")
    if Counter(row.get("phase") for row in schedule) != Counter({"calibration": 60, "residency": 180, "aa": 24}):
        raise SmokeVerifyError("schedule_phase_counts_invalid")
    if sorted({row.get("segment_id") for row in schedule if row.get("segment_id")}) != ["R01", "R02", "R03"]:
        raise SmokeVerifyError("S25_replica_major_smoke_segment_order_invalid")
    qa_output = (directory / "phase_qa_instruments_expulsion_stdout_r18.txt").read_text(encoding="utf-8", errors="replace")
    if ("PROVIDER_QA_R18 PASS configurations=50 worker_dispatches=400" not in qa_output or
            qa_output.count("effective_dispatch=8/8") != 50 or
            "EXPULSION_QA_R18 PASS primitive=omega_evict::evict_range weights_only_probe=true callback=none" not in qa_output):
        raise SmokeVerifyError("D7_provider_worker_dispatch_or_expulsion_QA_not_proved")

    event_rows = jsonl(directory / "residency_events_r18.jsonl")
    event_ids = [row.get("event_id") for row in event_rows]
    if event_ids != selected_ids or len(set(event_ids)) != 264:
        raise SmokeVerifyError("event_summary_order_or_multiplicity_mismatch")
    phase_counts = Counter(row.get("phase") for row in event_rows)
    if phase_counts != Counter({"calibration": 60, "residency": 180, "aa": 24}):
        raise SmokeVerifyError(f"smoke_phase_counts_invalid:{dict(phase_counts)}")
    plan_by_id = {event["id"]: event for event in selected}
    for row in event_rows:
        expected = plan_by_id[row["event_id"]]
        _verify_plan_identity(row, expected)
        r_unit_id = f"W{row.get('W_target_kib')}|O{row.get('O')}|m{row.get('m')}|K{row.get('K')}"
        if row.get("R_unit_id") != r_unit_id:
            raise SmokeVerifyError(f"R_unit_identity_invalid:{row['event_id']}:expected={r_unit_id}")
        if row.get("label") != "SMOKE" or row.get("numeric_metrics_used") is not False:
            raise SmokeVerifyError(f"smoke_numeric_use_label_invalid:{row['event_id']}")
        if row.get("R") != row.get("R") or not _integer(row.get("R")) or row["R"] < 1:
            raise SmokeVerifyError(f"R_invalid:{row['event_id']}")
        if (row.get("timestamps_complete") is not True or row.get("episode_count") != row.get("R") or
                row.get("coordinator_worker_exchanges") != 1):
            raise SmokeVerifyError(f"D9_timestamp_or_exchange_count_invalid:{row['event_id']}")
        overhead = row.get("instrumentation_overhead_fraction_max")
        if not isinstance(overhead, (int, float)) or not math.isfinite(overhead) or overhead > .01:
            raise SmokeVerifyError(f"D9_instrumentation_budget_exceeded:{row['event_id']}")
        _verify_workers(row, required_fields=("first_tsc", "last_tsc", "first_qpc", "last_qpc"),
                        expected_lps=expected_lps)

    calibration_rows = [row for row in event_rows if row.get("phase") == "calibration"]
    cal_by_unit, r_values = _verify_r_unit_calibrations(calibration_rows, expected_units=20, expected_rows=60)
    measured_groups: dict[tuple[Any, Any, Any], dict[str, dict[str, Any]]] = defaultdict(dict)
    for row in event_rows:
        if row.get("phase") == "residency":
            expected = plan_by_id[row["event_id"]]
            group_key = (expected.get("unit_id"), expected.get("block_id"), expected.get("atomic_group_id"))
            measured_groups[group_key][row.get("variant")] = row
    if len(measured_groups) != 60 or any(set(variants) != {"A", "B", "C"} for variants in measured_groups.values()):
        raise SmokeVerifyError("A_B_C_provider_buffer_group_incomplete")
    for key, variants in measured_groups.items():
        a, b, c = variants["A"], variants["B"], variants["C"]
        if (a.get("region_instance_id") != c.get("region_instance_id") or
                a.get("weight_regions") != c.get("weight_regions") or
                a.get("activation_regions") != c.get("activation_regions") or
                a.get("output_hashes") != c.get("output_hashes")):
            raise SmokeVerifyError(f"D7_A_C_live_buffer_or_output_identity_invalid:{key}")
        regions_b = b.get("weight_regions", [])
        if (len(regions_b) != 8 * int(b["K"]) or
                len({region.get("address") for region in regions_b}) != len(regions_b) or
                b.get("region_instance_id") == a.get("region_instance_id")):
            raise SmokeVerifyError(f"D7_B_independent_worker_buffers_invalid:{key}")
    sample_rows = [row for row in event_rows if row.get("phase") != "calibration"]
    for row in sample_rows:
        key = (row["W_target_kib"], row["O"], row["m"], row["K"])
        expected_r = next(iter(cal_by_unit[key]["A"]))
        if row["R"] != expected_r:
            raise SmokeVerifyError(f"R_sample_not_common_with_calibration:{row['event_id']}")
        if (row.get("condition_region_seconds", 0) < 2.0 or
                row.get("slowest_worker_kernel_seconds", 0) < .100 or
                row.get("pre_margin_seconds", 0) < .200 or row.get("post_margin_seconds", 0) < .200):
            raise SmokeVerifyError(f"S24_duration_or_margin_below_minimum:{row['event_id']}")
        if row.get("margin_continuous") is not True or row.get("margin_condition_id") != row.get("condition_id"):
            raise SmokeVerifyError(f"margin_condition_identity_invalid:{row['event_id']}")

    sample_events = jsonl(directory / "residency_episodes_r18.jsonl")
    cal_episodes = jsonl(directory / "residency_calibration_trials_r18.jsonl")
    if {row.get("event_id") for row in sample_events} != {row["event_id"] for row in sample_rows} or len(sample_events) != 204:
        raise SmokeVerifyError("sample_episode_worker_evidence_coverage_invalid")
    if {row.get("event_id") for row in cal_episodes} != {row["event_id"] for row in calibration_rows} or len(cal_episodes) != 60:
        raise SmokeVerifyError("calibration_episode_worker_evidence_coverage_invalid")
    for row in sample_events + cal_episodes:
        _verify_workers(row, required_fields=("first_tsc", "last_tsc", "first_qpc", "last_qpc"),
                        expected_lps=expected_lps)
    margin_episode_rows = jsonl(directory / "residency_margin_episodes_r18.jsonl") if (directory / "residency_margin_episodes_r18.jsonl").stat().st_size else []
    if margin_episode_rows:
        raise SmokeVerifyError("per_episode_margin_serialization_should_be_outside_timed_blocks")

    margins = jsonl(directory / "residency_margins_r18.jsonl")
    margin_keys = [(row.get("event_id"), row.get("side")) for row in margins]
    if len(margins) != 408 or len(set(margin_keys)) != 408:
        raise SmokeVerifyError("margin_pair_order_or_multiplicity_invalid")
    for row in margins:
        if (row.get("side") not in {"pre", "post"} or not _integer(row.get("qpc_start")) or
                not _integer(row.get("qpc_end")) or row["qpc_end"] <= row["qpc_start"] or
                row.get("seconds", 0) < .200 or row.get("same_condition") is not True or
                row.get("continuous_load") is not True):
            raise SmokeVerifyError(f"margin_evidence_invalid:{row.get('event_id')}:{row.get('side')}")

    block_marks = list(csv.DictReader((directory / "block_marks_r18.csv").open("r", encoding="utf-8-sig", newline="")))
    measured_ids = {row["event_id"] for row in sample_rows}
    mark_groups: dict[str, list[dict[str, str]]] = defaultdict(list)
    probe_mark_groups: dict[str, list[dict[str, str]]] = defaultdict(list)
    for mark in block_marks:
        sample_id = mark.get("sample_id", "")
        target = probe_mark_groups if sample_id.startswith("S25PROBE-") else mark_groups
        target[sample_id].append(mark)
    worker_lps = set(expected_lps.values())
    if set(mark_groups) != measured_ids or any(len(group) != 8 for group in mark_groups.values()):
        raise SmokeVerifyError("worker_block_mark_coverage_invalid")
    segment_ids = {row["segment_id"] for row in schedule
                   if row.get("phase") == "residency" and row.get("segment_id")}
    expected_probe_ids = {f"S25PROBE-{segment}-{endpoint}-{sample:02d}"
                          for segment in segment_ids for endpoint in ("BEGIN", "END")
                          for sample in range(1, 32)}
    if set(probe_mark_groups) != expected_probe_ids or any(len(group) != 8 for group in probe_mark_groups.values()):
        raise SmokeVerifyError("S25_probe_worker_block_mark_coverage_invalid")
    for event_id, group in (*mark_groups.items(), *probe_mark_groups.items()):
        by_lp = {int(row["worker_lp"]): row for row in group}
        if set(by_lp) != worker_lps or any(row.get("clock_domain") != "qpc_ticks" for row in group):
            raise SmokeVerifyError(f"worker_block_mark_LP_or_clock_invalid:{event_id}")

    manifest = jsonl(directory / "eviction_manifest_r18.jsonl")
    if len(manifest) != 264 or {row.get("event_id") for row in manifest} != set(selected_ids):
        raise SmokeVerifyError("D8_manifest_event_coverage_invalid")
    activation_calls = weight_calls = 0
    for row in manifest:
        if (row.get("primitive") != "omega_evict::evict_range" or row.get("weight_only") is not True or
                row.get("callback_per_line") is not False or row.get("each_episode_verified") is not True or
                row.get("activation_range_calls") != 0):
            raise SmokeVerifyError(f"D8_manifest_semantics_invalid:{row.get('event_id')}")
        activation_calls += int(row["activation_range_calls"])
        weight_calls += int(row.get("weight_range_calls", 0))

    segment_rows = jsonl(directory / "segment_conditioning_r18.jsonl")
    expected_segment_ids = {row["segment_id"] for row in schedule
                            if row.get("phase") == "residency" and row.get("segment_id")}
    by_segment = {row.get("segment_id"): row for row in segment_rows}
    if len(by_segment) != len(segment_rows) or set(by_segment) != expected_segment_ids:
        raise SmokeVerifyError("smoke_segment_conditioning_coverage_invalid")
    for segment_id, segment in by_segment.items():
        if (segment.get("plan_sha256") != plan["sha256"] or
                segment.get("reader_qualified") is not True or segment.get("large_pages_reverified") is not True or
                segment.get("active_conditioning_seconds", 0) < 180 or
                not _integer(segment.get("qpc_start")) or not _integer(segment.get("qpc_end")) or
                segment["qpc_end"] <= segment["qpc_start"]):
            raise SmokeVerifyError(f"segment_reverification_or_180s_conditioning_invalid:{segment_id}")
    loads = jsonl(directory / "residency_load_marks_r18.jsonl")
    if not loads or any(not _integer(row.get("qpc_load_start")) or not _integer(row.get("qpc_load_end")) or
                        row["qpc_load_end"] < row["qpc_load_start"] or row.get("large_pages_verified") is not True
                        for row in loads):
        raise SmokeVerifyError("QPC_load_marks_or_large_page_verification_invalid")

    if (preflight.get("plan_sha256") != plan["sha256"] or preflight.get("large_pages") is not True or
            preflight.get("process_map_verified") is not True or
            preflight.get("coordinator_outside_worker_lps") is not True or preflight.get("irq_touched") is not False):
        raise SmokeVerifyError("preflight_D9_identity_process_map_or_pages_invalid")
    capture = json.loads((directory / "capture_timing_r18.json").read_text(encoding="utf-8"), object_pairs_hook=_pairs)
    if (capture.get("controlled_stop") is not True or capture.get("stop_command") != "emon -stop" or
            not _integer(capture.get("qpc_start")) or not _integer(capture.get("qpc_end")) or
            capture["qpc_end"] <= capture["qpc_start"] or capture.get("capture_bytes", 1 << 30) >= 1 << 30):
        raise SmokeVerifyError("Q_parent_capture_timing_or_controlled_stop_invalid")
    budget = json.loads((directory / "capture_budget_r18.json").read_text(encoding="utf-8"), object_pairs_hook=_pairs)
    edp = (directory / "events.edp").read_text(encoding="utf-8")
    match = re.search(r"(?:^|\s)-l(\d+)(?:\s|$)", edp)
    if not match or int(match.group(1)) != budget.get("emon_loops") or budget.get("estimated_max_raw_bytes", 1 << 30) >= 1 << 30:
        raise SmokeVerifyError("EMON_limit_not_derived_from_smoke_schedule_or_size_guard_invalid")

    d13 = json.loads((directory / "acquisition_chain_r18.json").read_text(encoding="utf-8"), object_pairs_hook=_pairs)
    if d13.get("state") != "SMOKE_ONLY" or d13.get("numeric_metrics_used") is not False or d13.get("residency_started") is not True:
        raise SmokeVerifyError("sequential_gate_smoke_chain_or_numeric_label_invalid")
    s20 = json.loads((directory / "s20_validation_r18.json").read_text(encoding="utf-8"), object_pairs_hook=_pairs)
    return {"schedule_events": len(schedule), "event_summaries": len(event_rows),
            **aux_summary,
            "calibration_units": len(cal_by_unit), "sample_rows": len(sample_rows),
            "worker_evidence_rows": len(sample_events) + len(cal_episodes),
            "margin_rows": len(margins), "segment_conditionings": len(segment_rows),
            "segment_conditioning_s_min": min(row["active_conditioning_seconds"] for row in segment_rows),
            "load_marks": len(loads), "D8_weight_calls": weight_calls,
            "D8_activation_calls": activation_calls, "marks": len(block_marks),
            "S20_state": s20.get("state"), "numeric_metrics_used": False,
            "D13_raws_verified": len(RAW_FILES), "EMON_loops": budget["emon_loops"],
            "attempt_bytes": sum(path.stat().st_size for path in directory.iterdir() if path.is_file())}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--attempt", type=Path)
    mode.add_argument("--self-test", action="store_true")
    ns = parser.parse_args()
    try:
        if ns.self_test:
            calibration_fixture = []
            for weight, m, k, repetitions in ((288, 1, 1, 32), (384, 16, 16, 64)):
                for variant in ("A", "B", "C"):
                    calibration_fixture.append({"W_target_kib": weight, "O": 1024,
                        "m": m, "K": k, "variant": variant, "R": repetitions})
            _, r_values = _verify_r_unit_calibrations(calibration_fixture, expected_units=2, expected_rows=6)
            if r_values != {32, 64}:
                raise SmokeVerifyError(f"distinct_R_fixture_invalid:{sorted(r_values)}")
            expected = {worker: worker * 2 for worker in range(8)}
            _verify_plan_identity({"event_id": "fixture", "phase": "residency", "variant": "A"},
                                 {"id": "fixture", "phase": "residency", "variant": "A",
                                  "replica": 1, "block_id": "W288|O1024|r1"})
            try:
                _verify_plan_identity({"event_id": "fixture", "phase": "residency", "variant": "B"},
                                      {"id": "fixture", "phase": "residency", "variant": "A",
                                       "replica": 1, "block_id": "W288|O1024|r1"})
            except SmokeVerifyError as exc:
                if not str(exc).endswith(":variant"):
                    raise
            else:
                raise SmokeVerifyError("plan_identity_mutation_accepted")
            row = {"event_id": "fixture", "worker_evidence": [
                {"worker": worker, "lp": expected[worker], "first_tsc": 10, "last_tsc": 20,
                 "first_qpc": 30, "last_qpc": 40, "kernel_only_seconds": .15}
                for worker in range(8)]}
            _verify_workers(row, required_fields=("first_tsc", "last_tsc", "first_qpc", "last_qpc"),
                            expected_lps=expected)
            bad = json.loads(json.dumps(row)); bad["worker_evidence"][0]["lp"], bad["worker_evidence"][1]["lp"] = 2, 0
            try:
                _verify_workers(bad, required_fields=("first_tsc", "last_tsc", "first_qpc", "last_qpc"),
                                expected_lps=expected)
            except SmokeVerifyError as exc:
                if not str(exc).startswith("worker_to_LP_permutation_invalid"):
                    raise
            else:
                raise SmokeVerifyError("worker_to_LP_permutation_mutation_accepted")
            print("RESIDENCY_SMOKE_R18_SYNTHETIC_QA PASS cases=4 R(W,m,K)_distinct=2 R_common_A_B_C=true worker_to_LP_permutation_rejected=true schedule_derived_identity=PASS")
            return 0
        result = verify(ns.attempt)
        print("RESIDENCY_SMOKE_R18_VERIFY PASS " + " ".join(f"{key}={value}" for key, value in result.items()))
        return 0
    except Exception as exc:
        print(f"RESIDENCY_SMOKE_R18_VERIFY REJECT {type(exc).__name__}: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
