"""Audit S-25-D source boundaries and measure its final-binary probe overhead."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import statistics
import sys
from pathlib import Path
from typing import Any

from artifact_store_r18 import SessionAttemptStore


def sha_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_source_boundaries(source_path: Path) -> dict[str, Any]:
    source = source_path.read_text(encoding="utf-8")
    start = source.index("void acquire_hot_probe_endpoint(")
    end = source.index("struct EpisodeResult", start)
    body = source[start:end]
    if (r'<< "\",\"comparison_set\":\""' in body or
            r'<< ",\"comparison_set\":\""' not in body):
        raise ValueError("S25_D_probe_json_member_separator_invalid")
    probe_row = body[body.index("std::ostringstream row;"):]
    if ("record.replica" not in probe_row or "record.sample" not in probe_row or
            r'\"sample\"' not in probe_row):
        raise ValueError("S25_D_probe_plan_replica_or_sample_identity_missing")
    pad_start = body.index("record.qpc_pad_start = qpc_now();")
    measured_start = body.index("record.qpc_start = qpc_now();", pad_start)
    dispatch = body.index("dispatch({CommandKind::Probe, 'P', kHotProbeK, probe_r});", measured_start)
    copy_stamps = body.index("target.tsc_start = source.hot_probe_tsc_start", dispatch)
    post_margin = body.index("run_hot_probe_margin(probe_r);", dispatch)
    pad_end = body.index("record.qpc_pad_end = qpc_now();", post_margin)
    if not pad_start < measured_start < dispatch < copy_stamps < post_margin < pad_end:
        raise ValueError("S25_D_pad_or_worker_stamp_order_invalid")
    inside = body[pad_start:pad_end]
    forbidden = ("nlohmann::json", "std::ofstream", "std::ostringstream", "std::filesystem",
                 "std::cout", "std::cerr", "sha256", "hash_file", "new ", "reserve(",
                 "resize(", "marks.line(", "probe_raw.line(", "validate(")
    present = [token for token in forbidden if token in inside]
    if present:
        raise ValueError(f"S25_D_forbidden_work_inside_pad:{','.join(present)}")
    if body.index("g_hot_probe_records->capacity()") > pad_start:
        raise ValueError("S25_D_probe_buffer_capacity_not_checked_before_pad")
    if body.index("g_hot_probe_records->push_back(std::move(record))") < pad_end:
        raise ValueError("S25_D_probe_record_persisted_inside_pad")
    if body.index("marks.line(", pad_end) < pad_end or body.index("probe_raw.line(", pad_end) < pad_end:
        raise ValueError("S25_D_persistence_not_after_pad")
    final_endpoint = source.rindex(
        "acquire_hot_probe_endpoint(active_segment, active_segment_replica, 1, hot_probe_r, marks, hot_probe_raw);")
    probe_flush = source.rindex("hot_probe_raw.flush();")
    worker_stop = source.rindex("worker_cleanup.stop();")
    if not final_endpoint < worker_stop < probe_flush:
        raise ValueError("S25_D_probe_raw_not_flushed_after_final_endpoint_and_worker_stop")
    return {"state": "PASS", "pad_bracket": "qpc_pad_start..qpc_pad_end",
            "worker_stamp_copy": "after_measured_dispatch_before_post_margin",
            "preallocated_capacity_checked_before_pad": True,
            "binary_buffer_push_and_file_persistence_after_pad": True,
            "probe_raw_flush_after_final_endpoint_and_worker_stop": True,
            "hash_json_print_file_io_allocation_validation_inside_pad": False}


def _schedule_segment_replicas(path: Path, plan_path: Path) -> dict[str, int]:
    lines = path.read_text(encoding="utf-8").splitlines()
    header = "sequence\tevent_id\tphase\tblock_id\tatomic_group_id\tsegment_id"
    if header not in lines:
        raise ValueError("S25_D_schedule_header_invalid")
    scheduled_ids = set()
    for line in lines[lines.index(header) + 1:]:
        fields = line.split("\t")
        if len(fields) != 6:
            raise ValueError("S25_D_schedule_row_invalid")
        if fields[2] == "residency" and fields[5]:
            scheduled_ids.add(fields[1])
    plan = json.loads(plan_path.read_text(encoding="utf-8"))
    segments: dict[str, int] = {}
    for event in plan.get("events", []):
        if event.get("id") not in scheduled_ids or event.get("phase") != "residency":
            continue
        segment, replica = str(event.get("segment_id", "")), int(event.get("replica", 0))
        if not segment or replica < 1 or (segment in segments and segments[segment] != replica):
            raise ValueError(f"S25_D_segment_replica_mapping_invalid:{segment}:{replica}")
        segments[segment] = replica
    if not segments:
        raise ValueError("S25_D_schedule_has_no_residency_segments")
    return segments


def verify_attempt(attempt: Path, executable: Path, source: Path) -> dict[str, Any]:
    attempt = attempt.resolve()
    store = SessionAttemptStore(attempt)
    boundary = verify_source_boundaries(source)
    raw_name = "instrumentation_probe_r18.jsonl"
    probe_sha = store.verify_raw(raw_name)
    probe_path = attempt / raw_name
    rows = [json.loads(line) for line in probe_path.read_text(encoding="utf-8").splitlines() if line]
    qpf = float(json.loads((attempt / "preflight_r18.json").read_text(encoding="utf-8"))["qpf_hz"])
    if not math.isfinite(qpf) or qpf <= 0:
        raise ValueError("S25_D_parent_QPF_invalid")
    plan_sha = json.loads((attempt / "residency_plan_r18.json").read_text(encoding="utf-8"))["sha256"]
    disassembly = json.loads((attempt / "disassembly_qa_r18.json").read_text(encoding="utf-8"))
    binary_sha = sha_file(executable)
    if disassembly.get("executable_sha256") != binary_sha:
        raise ValueError("S25_D_disassembly_not_bound_to_final_binary")
    segment_replicas = _schedule_segment_replicas(attempt / "residency_smoke_schedule_r18.tsv",
                                                  attempt / "residency_plan_r18.json")
    expected_segments = sorted(segment_replicas)
    expected = {(segment, endpoint, replica, sample) for segment, replica in segment_replicas.items()
                for endpoint in ("BEGIN", "END") for sample in range(1, 32)}
    actual: list[tuple[str, str, int, int]] = []
    overheads = []
    for row in rows:
        if row.get("label") != "SMOKE" or row.get("plan_sha256", plan_sha) != plan_sha:
            raise ValueError("S25_D_probe_label_or_plan_invalid")
        workers = row.get("worker_evidence")
        if not isinstance(workers, list) or len(workers) != 8:
            raise ValueError("S25_D_worker_evidence_cardinality_invalid")
        qpc_start, qpc_end = int(row["qpc_start"]), int(row["qpc_end"])
        if qpc_end <= qpc_start:
            raise ValueError("S25_D_region_QPC_order_invalid")
        region_s = (qpc_end - qpc_start) / qpf
        if not math.isclose(region_s, float(row["region_seconds"]), rel_tol=2e-4, abs_tol=2e-6):
            raise ValueError("S25_D_region_seconds_not_from_parent_QPC")
        first = min(int(worker["first_qpc"]) for worker in workers)
        last = max(int(worker["last_qpc"]) for worker in workers)
        if last <= first:
            raise ValueError("S25_D_application_minimal_worker_span_invalid")
        application_minimal_s = (last - first) / qpf
        measured = max(0.0, region_s - application_minimal_s) / application_minimal_s
        stated = float(row["instrumentation_overhead_fraction"])
        if not math.isfinite(stated) or not math.isclose(measured, stated, rel_tol=.01, abs_tol=2e-4):
            raise ValueError(f"S25_D_overhead_formula_mismatch:{row.get('event_id')}:{measured}:{stated}")
        if region_s < 2.0 or stated > .01:
            raise ValueError(f"S25_D_budget_or_minimum_region_failed:{row.get('event_id')}:{stated}")
        sample = int(row["sample"])
        if not 1 <= sample <= 31:
            raise ValueError(f"S25_D_probe_sample_out_of_range:{sample}")
        actual.append((str(row["segment_id"]), str(row["endpoint"]), int(row["replica"]), sample))
        overheads.append(stated)
    if len(actual) != len(set(actual)) or set(actual) != expected:
        raise ValueError(f"S25_D_begin_end_coverage_invalid:expected={len(expected)}:actual={len(actual)}")
    report = {"schema_version": "S25-D-R18-v1", "state": "PASS",
              "source_boundary_audit": boundary, "binary_sha256": binary_sha,
              "probe_raw_sha256": probe_sha, "plan_sha256": plan_sha,
              "application_minimal_definition": "QPC span min(worker first_qpc)..max(worker last_qpc) for the same final-binary dispatch",
              "samples": len(rows), "segments": expected_segments,
              "segment_replicas": segment_replicas,
              "application_region_seconds_min": min(float(row["region_seconds"]) for row in rows),
              "application_region_seconds_median": statistics.median(float(row["region_seconds"]) for row in rows),
              "application_region_seconds_max": max(float(row["region_seconds"]) for row in rows),
              "max_overhead_fraction": max(overheads),
              "p99_overhead_fraction": sorted(overheads)[max(0, math.ceil(.99 * len(overheads)) - 1)],
              "mean_overhead_fraction": statistics.fmean(overheads),
              "threshold_fraction": .01, "all_samples_within_budget": True,
              "hash_json_print_file_io_allocations_validation_inside_pad": False}
    source_raw = [name for name in (raw_name, "residency_plan_r18.json",
                    "residency_smoke_schedule_r18.tsv", "preflight_r18.json",
                    "disassembly_qa_r18.json", "residency_events_r18.jsonl")
                  if store._raw_sidecar(name).is_file()]
    store.write_derived("instrumentation_budget_r18.json",
        (json.dumps(report, sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n").encode("utf-8"),
        source_raw=source_raw,
        metadata={"protocol": "S25-D", "binary_sha256": binary_sha,
                  "application_minimal": report["application_minimal_definition"]})
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1] / "harness" / "residency_r18.cpp")
    parser.add_argument("--attempt", type=Path)
    parser.add_argument("--executable", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    ns = parser.parse_args(argv)
    try:
        if ns.attempt is None:
            result = verify_source_boundaries(ns.source)
        else:
            if ns.executable is None:
                raise ValueError("--executable_required_with_attempt")
            result = verify_attempt(ns.attempt, ns.executable, ns.source)
        payload = json.dumps(result, sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n"
        ns.output.parent.mkdir(parents=True, exist_ok=True)
        ns.output.write_text(payload, encoding="utf-8", newline="\n")
        print(f"S25_D_INSTRUMENTATION_R18 {result['state']} "
              f"samples={result.get('samples','source_only')} "
              f"max_fraction={result.get('max_overhead_fraction','n/a')} "
              f"threshold={result.get('threshold_fraction','.01')} "
              f"hash_json_print_file_io_allocations_validation_inside_pad=false evidence={ns.output}")
        return 0
    except (OSError, KeyError, TypeError, ValueError) as exc:
        print(f"S25_D_INSTRUMENTATION_R18 INVALID {type(exc).__name__}:{exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
