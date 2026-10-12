from __future__ import annotations

import csv
import datetime as dt
import hashlib
import json
import math
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import s20_validator as s20
from artifact_store import ArtifactError, SessionAttemptStore
from adapter_example import validate_attempt


ROOT = Path(__file__).resolve().parents[3]
UTC_EPOCH = dt.datetime(1601, 1, 1)
BASE_DATE = dt.datetime(2026, 10, 12, 12, 0, 0)
BASE_QPC = 1_000_000_000
QPF = 1_000_000
FTSC = 2_112_000_000.0
ALL_EVENTS = list(s20.DEFAULT_EVENT_GROUP)


def filetime_seconds(value: dt.datetime) -> float:
    delta = value - UTC_EPOCH
    return ((delta.days * 86400 + delta.seconds) * 10_000_000 + delta.microseconds * 10) / 10_000_000


def base_config() -> dict:
    return {
        "schema_version": "S20-R2-v1",
        "profile": "s20",
        "qpf_hz": QPF,
        "qpf_source": "fixture map regression (explicitly recorded)",
        "f_tsc_qualified_hz": FTSC,
        "f_tsc_qpc_hz": FTSC,
        "f_tsc_configured_hz": FTSC,
        "f_tsc_qualification_qpf_hz": QPF,
        "emon_local_utc_offset_minutes": 0,
        "nominal_window_s": 0.05,
        "logical_processor_count": 24,
        "lp_map": [
            {"p_core": core, "core_id": core * 4, "core_type": "P", "worker_lp": core * 2,
             "smt_siblings": [core * 2, core * 2 + 1]}
            for core in range(8)
        ],
        "event_groups": [ALL_EVENTS],
        "multiplexing": False,
        "periods_lost": 0,
        "energy_event": s20.EVENT_611,
        "energy_semantics": "INTERVAL_DELTA",
        "energy_unit_joule": 2 ** -14,
        "tcc_target_c": 100,
        "thermal_history_clear_at_start": False,
        "edp_path": "fixture.edp",
        "edp_sha256": "0" * 64,
        "event_map_source": "fixture event set with one group",
        "mode": "temporal",
        "marks_clock_domain": "qpc_ticks",
    }


def make_group(i: int, *, thread_lp0: int = 250_000_000, ref_lp0: int = 105_600_000,
               l2_denom_lp0: int = 20_000, thermal19_lp0: int = 0x800A0000,
               thermal1b1: int = 0, energy: int = 1000, duration_s: float = 0.05) -> tuple[str, int]:
    end_time = BASE_DATE + dt.timedelta(seconds=(i + 1) * duration_s)
    end_qpc = BASE_QPC + round((i + 1) * duration_s * QPF)
    elapsed = round(FTSC * duration_s)
    per_lp: dict[str, list[int]] = {}
    for event in (s20.EVENT_THREAD, s20.EVENT_REF, s20.EVENT_INST, s20.EVENT_L2_MISS,
                  s20.EVENT_L2_DEMAND, s20.EVENT_LLC_MISS, s20.EVENT_LLC_DEMAND):
        vals = [0] * 24
        for lp in range(0, 16, 2):
            if event == s20.EVENT_THREAD:
                vals[lp] = thread_lp0 if lp == 0 else 250_000_000
            elif event == s20.EVENT_REF:
                vals[lp] = ref_lp0 if lp == 0 else 105_600_000
            elif event == s20.EVENT_INST:
                vals[lp] = 1_000_000
            elif event in {s20.EVENT_L2_MISS, s20.EVENT_LLC_MISS}:
                vals[lp] = 10_000
            elif event == s20.EVENT_L2_DEMAND:
                vals[lp] = l2_denom_lp0 if lp == 0 else 20_000
            elif event == s20.EVENT_LLC_DEMAND:
                vals[lp] = 20_000
        per_lp[event] = vals
    per_lp[s20.EVENT_19C] = [thermal19_lp0 if lp == 0 else 0x800A0000 for lp in range(24)]
    parts = [end_time.strftime("%m/%d/%Y %H:%M:%S.%f")[:-3]]
    def cpu_line(event: str, vals: list[int]) -> str:
        return "\t".join([event, str(elapsed), *(str(v) for v in vals)])
    lines = [cpu_line(event, per_lp[event]) for event in (
        s20.EVENT_THREAD, s20.EVENT_REF, s20.EVENT_INST, s20.EVENT_L2_MISS,
        s20.EVENT_L2_DEMAND, s20.EVENT_LLC_MISS, s20.EVENT_LLC_DEMAND,
    )]
    lines += ["\t".join([s20.EVENT_606, str(elapsed), "658947"]),
              "\t".join([s20.EVENT_611, str(elapsed), str(energy)]),
              cpu_line(s20.EVENT_19C, per_lp[s20.EVENT_19C]),
              "\t".join([s20.EVENT_1B1, str(elapsed), str(thermal1b1)])]
    return "\n".join([parts[0], *lines]), end_qpc


def make_fixture(directory: Path, *, count: int = 30, duration_s: float = 0.05,
                  custom_groups: list[str] | None = None) -> tuple[Path, Path, Path, dict]:
    directory.mkdir(parents=True, exist_ok=True)
    raw = directory / "emon_raw.txt"
    clock = directory / "clock_map.csv"
    marks = directory / "marks.csv"
    config = base_config()
    if custom_groups:
        config["event_groups"] = [custom_groups]
    edp = directory / "fixture.edp"
    edp.write_text("-C (\n" + "\n".join(config["event_groups"][0]) + "\n)\n", encoding="utf-8")
    config["edp_path"] = str(edp.resolve())
    config["edp_sha256"] = hashlib.sha256(edp.read_bytes()).hexdigest()
    chunks = []
    map_rows = []
    for i in range(max(count, 20)):
        group, end_qpc = make_group(i, ref_lp0=round(FTSC * duration_s), duration_s=duration_s)
        if i < count:
            chunks.append(group)
        stamp = BASE_DATE + dt.timedelta(seconds=(i + 1) * duration_s)
        end_filetime = filetime_seconds(stamp)
        map_rows.append({"sample": str(i), "qpc_pre": end_qpc - 10, "qpc_post": end_qpc + 10,
                         "filetime_100ns": round(end_filetime * 10_000_000)})
    raw.write_text("\n==========\n".join(chunks) + "\n", encoding="utf-8")
    with clock.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["sample", "qpc_pre", "qpc_post", "filetime_100ns"])
        writer.writeheader()
        writer.writerows(map_rows)
    pad_start = BASE_QPC - 300_000
    start = BASE_QPC
    end = BASE_QPC + round(count * duration_s * QPF)
    pad_end = end + 300_000
    with marks.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["sample_id", "Pcore", "replica", "condition", "worker_lp",
                                               "qpc_pad_start", "qpc_start", "qpc_end", "qpc_pad_end", "comparison_set"])
        writer.writeheader()
        writer.writerow({"sample_id": "S-A-1", "Pcore": 0, "replica": 1, "condition": "A", "worker_lp": 0,
                         "qpc_pad_start": pad_start, "qpc_start": start, "qpc_end": end, "qpc_pad_end": pad_end,
                         "comparison_set": ""})
    return raw, clock, marks, config


def mutate_cpu_event(raw_path: Path, event: str, group_index: int, lp: int, value: int) -> None:
    chunks = raw_path.read_text(encoding="utf-8").split("\n==========\n")
    lines = chunks[group_index].splitlines()
    for index, line in enumerate(lines):
        fields = line.split("\t")
        if fields[0] == event:
            fields[2 + lp] = str(value)
            lines[index] = "\t".join(fields)
            break
    chunks[group_index] = "\n".join(lines)
    raw_path.write_text("\n==========\n".join(chunks), encoding="utf-8")


def mutate_package_event(raw_path: Path, event: str, group_index: int, value: int) -> None:
    chunks = raw_path.read_text(encoding="utf-8").split("\n==========\n")
    lines = chunks[group_index].splitlines()
    for index, line in enumerate(lines):
        fields = line.split("\t")
        if fields[0] == event:
            fields[2] = str(value)
            lines[index] = "\t".join(fields)
            break
    chunks[group_index] = "\n".join(lines)
    raw_path.write_text("\n==========\n".join(chunks), encoding="utf-8")


def mark_for(result: dict, channel: str = "THREAD_REF_TSC", p_core: int = 0) -> dict:
    return next(x for x in result["results"] if x["channel"] == channel and x["core"] == p_core)


class S20NegativeAndPositiveTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="s20_test_")
        self.dir = Path(self.temp.name)
        self.raw, self.clock, self.marks, self.config = make_fixture(self.dir / "base")

    def tearDown(self):
        self.temp.cleanup()

    def run_fixture(self, raw=None, clock=None, marks=None, config=None, mode="temporal"):
        return s20.validate_capture(raw or self.raw, clock or self.clock, marks or self.marks,
                                    config or self.config, mode=mode)

    def test_one_percent_coverage_and_one_second_gap_are_not_valid(self):
        intervals = [(i * 1.01, i * 1.01 + 0.01) for i in range(100)]
        coverage, gap = s20.coverage_fraction(0.0, 100.0, intervals)
        self.assertAlmostEqual(coverage, 0.01, places=10)
        self.assertAlmostEqual(gap, 1.0, places=10)
        raw, clock, marks, config = make_fixture(self.dir / "low", count=1)
        with marks.open(encoding="utf-8", newline="") as f:
            rows = list(csv.DictReader(f))
        rows[0]["qpc_start"] = BASE_QPC
        rows[0]["qpc_end"] = BASE_QPC + 100 * QPF
        rows[0]["qpc_pad_start"] = BASE_QPC - QPF
        rows[0]["qpc_pad_end"] = BASE_QPC + 101 * QPF
        with marks.open("w", encoding="utf-8", newline="") as f:
            w = csv.DictWriter(f, fieldnames=rows[0].keys()); w.writeheader(); w.writerows(rows)
        result = self.run_fixture(raw, clock, marks, config)
        row = mark_for(result)
        codes = {x["code"] for x in row["reasons"]}
        self.assertIn("COVERAGE_LOW", codes)
        self.assertIn("GAP_TOO_LARGE", codes)

    def test_tsc_qpc_disagreement_two_percent_invalidates(self):
        config = dict(self.config)
        config["f_tsc_qpc_hz"] *= 1.02
        row = mark_for(self.run_fixture(config=config))
        self.assertIn("TSC_QPC_DISAGREEMENT", {x["code"] for x in row["reasons"]})

    def test_qpf_missing_is_an_input_error(self):
        config = dict(self.config)
        del config["qpf_hz"]
        with self.assertRaises(s20.ValidationInputError) as caught:
            self.run_fixture(config=config)
        self.assertEqual(caught.exception.code, "QPF_MISSING")

    def test_unknown_config_key_is_rejected(self):
        config = dict(self.config)
        config["state"] = "VALID"
        with self.assertRaises(s20.ValidationInputError) as caught:
            self.run_fixture(config=config)
        self.assertEqual(caught.exception.code, "CONFIG_UNKNOWN_FIELD")

    def test_duplicate_json_config_key_is_rejected(self):
        text = json.dumps(self.config, ensure_ascii=False)
        text = text.replace('"mode": "temporal"', '"mode": "temporal", "mode": "full"', 1)
        path = self.dir / "duplicate_config.json"
        path.write_text(text, encoding="utf-8")
        with self.assertRaises(s20.ValidationInputError) as caught:
            self.run_fixture(config=path)
        self.assertEqual(caught.exception.code, "JSON_DUPLICATE_KEY")

    def test_lp_core_map_must_match_effective_emon_topology(self):
        config = dict(self.config)
        config["lp_map"] = [dict(row) for row in self.config["lp_map"]]
        config["lp_map"][3]["core_id"] = 13
        with self.assertRaises(s20.ValidationInputError) as caught:
            self.run_fixture(config=config)
        self.assertEqual(caught.exception.code, "LP_MAP_INVALID")

    def test_api_adapter_snapshots_edp_and_hash_links_result(self):
        raw, clock, marks, config = make_fixture(self.dir / "adapter")
        attempt_dir = self.dir / "adapter_attempt" / "attempt-001"
        run = validate_attempt(emon_raw=raw, clock_map=clock, block_marks=marks,
            effective_config=config, attempt_directory=attempt_dir,
            session_id="adapter-session", attempt_id="attempt-001", mode="temporal")
        self.assertEqual(run["result"]["state"], "VALID")
        self.assertTrue((attempt_dir / "events.edp.sha256").is_file())
        result_lineage = json.loads((attempt_dir / "s20_validation.json.lineage.json").read_text(encoding="utf-8"))
        self.assertIn("events.edp", result_lineage["sources"])
        effective_lineage = json.loads((attempt_dir / "effective_config.json.lineage.json").read_text(encoding="utf-8"))
        self.assertEqual(effective_lineage["sources"]["events.edp"], run["source_sha256"]["events.edp"])

    def test_duplicate_marks_header_is_rejected(self):
        self.marks.write_text("Pcore,Pcore\n0,0\n", encoding="utf-8")
        with self.assertRaises(s20.ValidationInputError) as caught:
            self.run_fixture()
        self.assertEqual(caught.exception.code, "MARKS_HEADER")

    def test_duplicate_clock_map_sample_is_rejected(self):
        with self.clock.open(encoding="utf-8", newline="") as f:
            rows = list(csv.DictReader(f))
        rows[1]["sample"] = rows[0]["sample"]
        with self.clock.open("w", encoding="utf-8", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=rows[0].keys()); writer.writeheader(); writer.writerows(rows)
        with self.assertRaises(s20.ValidationInputError) as caught:
            self.run_fixture()
        self.assertEqual(caught.exception.code, "CLOCK_MAP_INVALID")

    def test_unconfigured_emon_event_is_rejected(self):
        chunks = self.raw.read_text(encoding="utf-8").split("\n==========\n")
        chunks[0] += "\nCPU_CLK_UNHALTED.UNRELATED\t105600000\t1\n"
        self.raw.write_text("\n==========\n".join(chunks), encoding="utf-8")
        with self.assertRaises(s20.ValidationInputError) as caught:
            self.run_fixture()
        self.assertEqual(caught.exception.code, "EVENT_SET_MISMATCH")

    def test_window_crossing_another_condition_is_excluded(self):
        with self.marks.open(encoding="utf-8", newline="") as f:
            rows = list(csv.DictReader(f))
        rows[0]["qpc_start"] = BASE_QPC + 200_000
        rows[0]["qpc_end"] = BASE_QPC + 1_300_000
        rows[0]["qpc_pad_start"] = BASE_QPC
        rows[0]["qpc_pad_end"] = BASE_QPC + 1_500_000
        rows[0]["condition"] = "A"
        second = dict(rows[0]); second.update({"sample_id": "S-B-1", "condition": "B", "qpc_start": BASE_QPC + 240_000,
                                               "qpc_end": BASE_QPC + 400_000, "qpc_pad_start": BASE_QPC + 100_000,
                                               "qpc_pad_end": BASE_QPC + 600_000})
        with self.marks.open("w", encoding="utf-8", newline="") as f:
            w = csv.DictWriter(f, fieldnames=rows[0].keys()); w.writeheader(); w.writerows([rows[0], second])
        result = self.run_fixture()
        target = next(x for x in result["results"] if x["sample"]["sample_id"] == "S-A-1" and x["channel"] == "THREAD_REF_TSC")
        self.assertGreater(target["metrics"]["excluded_cross_condition"], 0)
        self.assertTrue(any(w["state"] == "EXCLUDED" and w["reason"] == "CROSS_CONDITION" for w in target["window_statuses"]))

    def test_fewer_than_twenty_windows_invalid(self):
        raw, clock, marks, config = make_fixture(self.dir / "few", count=19)
        result = self.run_fixture(raw, clock, marks, config)
        self.assertIn("WINDOW_COUNT_LOW", {x["code"] for x in mark_for(result)["reasons"]})

    def test_window_over_one_hundred_ms_invalid(self):
        raw, clock, marks, config = make_fixture(self.dir / "long", count=30, duration_s=0.101)
        result = self.run_fixture(raw, clock, marks, config)
        self.assertIn("WINDOW_TOO_LONG", {x["code"] for x in mark_for(result)["reasons"]})

    def test_current_and_historical_prochot_are_separate_invalid_flags(self):
        for name, bit, expected in (("actual", 2, "PROCHOT_CURRENT"), ("historical", 3, "PROCHOT_HISTORICAL_UNCLEARED")):
            with self.subTest(name=name):
                raw, clock, marks, config = make_fixture(self.dir / name)
                mutate_cpu_event(raw, s20.EVENT_19C, 4, 0, 0x800A0000 | (1 << bit))
                result = self.run_fixture(raw, clock, marks, config)
                row = mark_for(result, "THERMAL_19C")
                self.assertIn(expected, {x["code"] for x in row["reasons"]})
                thermal = row["metrics"]["thermal_windows"][4]
                self.assertIn("prochot", thermal["current_19c"])
                self.assertIn("prochot_log", thermal["historical_19c"])

    def test_power_limit_flag_is_reported_without_becoming_a_thermal_failure(self):
        mutate_cpu_event(self.raw, s20.EVENT_19C, 4, 0, 0x800A0000 | (1 << 10))
        row = mark_for(self.run_fixture(), "THERMAL_19C")
        self.assertEqual(row["state"], "VALID")
        self.assertTrue(row["metrics"]["thermal_windows"][4]["power_limit_observed"])

    def test_1b1_bit31_is_not_transferred_as_a_validity_bit(self):
        mutate_package_event(self.raw, s20.EVENT_1B1, 4, 1 << 31)
        row = mark_for(self.run_fixture(), "THERMAL_1B1")
        self.assertEqual(row["state"], "VALID")
        self.assertNotIn("digital_reading_valid_1b1_bit31", row["metrics"]["thermal_windows"][4])

    def test_tcc_margin_below_five_degrees_is_invalid(self):
        mutate_cpu_event(self.raw, s20.EVENT_19C, 4, 0, 0x80040000)
        row = mark_for(self.run_fixture(), "THERMAL_19C")
        self.assertIn("THERMAL_MARGIN_LOW", {x["code"] for x in row["reasons"]})

    def test_19c_invalid_reading_bit31_is_rejected(self):
        mutate_cpu_event(self.raw, s20.EVENT_19C, 5, 0, 0x000A0000)
        row = mark_for(self.run_fixture(), "THERMAL_19C")
        self.assertIn("THERMAL_READING_INVALID", {x["code"] for x in row["reasons"]})

    def test_611_uint32_rollover_uses_modular_delta(self):
        config = dict(self.config)
        config["energy_semantics"] = "SNAPSHOT32"
        for i in range(30):
            value = 0xFFFFFFFE if i == 0 else 3 + (i - 1) * 1000
            mutate_package_event(self.raw, s20.EVENT_611, i, value)
        result = self.run_fixture(config=config)
        energy = mark_for(result, "ENERGY_PKG")
        self.assertEqual(energy["metrics"]["energy_delta"], 28_005)
        self.assertEqual(s20.modular_delta_32(0xFFFFFFFE, 3), 5)

    def test_smt_sibling_mixed_is_rejected(self):
        with self.marks.open(encoding="utf-8", newline="") as f:
            rows = list(csv.DictReader(f))
        rows[0]["worker_lp"] = "1"
        with self.marks.open("w", encoding="utf-8", newline="") as f:
            w = csv.DictWriter(f, fieldnames=rows[0].keys()); w.writeheader(); w.writerows(rows)
        row = mark_for(self.run_fixture())
        self.assertIn("SMT_SCOPE_MIXED", {x["code"] for x in row["reasons"]})

    def test_one_core_one_window_outside_three_percent_is_rejected(self):
        with self.marks.open(encoding="utf-8", newline="") as f:
            marks = list(csv.DictReader(f))
        for core in range(1, 8):
            row = dict(marks[0]); row.update({"sample_id": f"S-A-P{core}", "Pcore": str(core), "worker_lp": str(core * 2)})
            marks.append(row)
        with self.marks.open("w", encoding="utf-8", newline="") as f:
            w = csv.DictWriter(f, fieldnames=marks[0].keys()); w.writeheader(); w.writerows(marks)
        mutate_cpu_event(self.raw, s20.EVENT_THREAD, 8, 14, 260_000_000)
        result = self.run_fixture()
        row = mark_for(result, p_core=7)
        self.assertIn("FREQ_WINDOW_EXCURSION", {x["code"] for x in row["reasons"]})
        self.assertNotIn("FREQ_WINDOW_EXCURSION", {x["code"] for x in mark_for(result, p_core=0)["reasons"]})
        outlier = next(w for w in row["window_statuses"] if w["interval"] == 8)
        self.assertEqual(outlier["state"], "INVALID")
        cache_row = mark_for(result, "L2_DEMAND_DATA_READ", p_core=7)
        self.assertIn("FREQ_WINDOW_EXCURSION", {x["code"] for x in cache_row["reasons"]})
        cache_window = next(w for w in cache_row["window_statuses"] if w["interval"] == 8)
        self.assertEqual(cache_window["state"], "INVALID")

    def test_l2_llc_denominator_zero_is_not_estimable(self):
        for i in range(30):
            mutate_cpu_event(self.raw, s20.EVENT_L2_DEMAND, i, 0, 0)
        result = self.run_fixture()
        row = mark_for(result, "L2_DEMAND_DATA_READ")
        self.assertEqual(row["metrics"]["l2_fraction_state"], "NOT_ESTIMABLE")
        self.assertEqual(row["state"], "NOT_ESTIMABLE")

    def test_coverage_uses_seconds_not_milliseconds(self):
        coverage, gap = s20.coverage_fraction(0.0, 2.5, [(0.0, 1.0)])
        self.assertAlmostEqual(coverage, 0.4, places=12)
        self.assertAlmostEqual(gap, 1.5, places=12)

    def test_each_window_is_contracted_before_union(self):
        coverage, gap = s20.coverage_fraction(0.0, 1.0, [(0.0, 0.5), (0.5, 1.0)], uncertainty_s=0.1)
        self.assertAlmostEqual(coverage, 0.6, places=12)
        self.assertAlmostEqual(gap, 0.2, places=12)

    def test_active_frequency_uses_ratio_of_sums(self):
        active = s20.active_frequency_hz([100, 900], [50, 300], 10.0)
        self.assertAlmostEqual(active, 10.0 * 1000 / 350, places=12)

    def test_full_mode_core_dispersion_control_uses_frequency_fields(self):
        rows = control_rows(lambda core, condition, replica: 106.0 if core == 7 and replica == 1 else 100.0)
        s20._add_frequency_controls(rows, "full")
        target = next(r for r in rows if r["sample"]["p_core"] == 7 and r["sample"]["replica"] == 1 and r["sample"]["condition"] == "B")
        self.assertIn("FREQ_CORE_DISPERSION", {x["code"] for x in target["reasons"]} | set(target["_control_reasons"]))

    def test_full_mode_replica_drift_control_uses_frequency_fields(self):
        rows = control_rows(lambda core, condition, replica: 108.0 if replica == 2 else 100.0)
        s20._add_frequency_controls(rows, "full")
        target = next(r for r in rows if r["sample"]["p_core"] == 3 and r["sample"]["replica"] == 2 and r["sample"]["condition"] == "A")
        self.assertIn("FREQ_REPLICA_DRIFT", set(target["reasons"]))

    def test_real_conditioning_joint_capture_temporal_channels(self):
        raw = ROOT / "T0P/telemetry/logs/conditioning_joint_raw.txt"
        clock = ROOT / "T0P/telemetry/logs/conditioning_qpc_utc_map.csv"
        config = real_config(clock)
        mark_path = self.dir / "conditioning_marks.csv"
        write_conditioning_marks(clock, mark_path)
        result = s20.validate_capture(raw, clock, mark_path, config, mode="temporal")
        self.assertEqual(result["samples"], 8)
        self.assertEqual(result["state"], "VALID")
        self.assertGreaterEqual(min(r["metrics"]["coverage_lower"] for r in result["results"]), 0.95)
        self.assertEqual(len(result["results"]), 8 * 6)
        energy = mark_for(result, "ENERGY_PKG")
        self.assertAlmostEqual(energy["metrics"]["package_power_w"],
            energy["metrics"]["energy_delta"] * self.config["energy_unit_joule"] /
            energy["metrics"]["energy_elapsed_s"], places=12)

    def test_real_evict_capture_all_forty_blocks_conservative_r2(self):
        cap = ROOT / "T0P/evict/capture_blocks_r2"
        config = real_config(cap / "clock_map.csv")
        result = s20.validate_capture(cap / "emon_stdout.txt", cap / "clock_map.csv",
                                      cap / "eviction_windows.csv", config, mode="temporal")
        self.assertEqual(result["samples"], 40)
        temporal = [r for r in result["results"] if r["channel"] == "THREAD_REF_TSC"]
        self.assertEqual(len(temporal), 40)
        coverages = [r["metrics"]["coverage_lower"] for r in temporal]
        self.assertAlmostEqual(min(coverages) * 100.0, 96.26, places=1)
        self.assertGreaterEqual(min(r["metrics"]["window_count"] for r in temporal), 20)
        self.assertAlmostEqual(max(r["metrics"]["gap_upper_s"] for r in temporal) * 1000.0, 3.50, delta=0.02)
        self.assertTrue(all(abs(r["metrics"]["uncertainty_s"] - 0.000885) < 0.0001 for r in temporal))

    def test_r1_native_dummy_report_confirms_all_eight_workers(self):
        attempt = ROOT / "T0P/telemetry/logs/inst_cli_r6_dummy_session/attempt-002"
        clock_map = attempt / "helper_clock_map_r6.csv"
        config = real_config(clock_map)
        config.update({"profile": "r1", "mode": "r1", "event_groups": [[s20.EVENT_INST]],
                       "event_map_source": "emon --dry-run -C INST_RETIRED.ANY; grupo único",
                       "n_loop": 200_000_000_000})
        config.pop("edp_path")
        config.pop("edp_sha256")
        metadata, workers = s20.read_r1_helper_report(attempt / "helper_report_r6.csv", config)
        clock = s20.parse_clock_map(clock_map, config)
        self.assertEqual(set(workers), {0, 2, 4, 6, 8, 10, 12, 14})
        self.assertEqual(metadata["I0"], 600_000_000_004)
        self.assertTrue(all(w["result"] == 200_000_000_000 and w["duration_s"] >= 20.0 for w in workers.values()))
        self.assertLessEqual(clock["uncertainty_s"], 0.001)


def control_rows(frequency):
    rows = []
    for replica in (1, 2):
        for condition in ("A", "B", "C"):
            for core in range(8):
                rows.append({"sample": {"comparison_set": "ABC", "p_core": core, "replica": replica,
                                         "condition": condition}, "channel": "THREAD_REF_TSC", "core": core,
                             "state": "VALID", "reasons": [], "_control_reasons": [],
                             "metrics": {"f_active_hz": frequency(core, condition, replica)}})
    return rows


def real_config(clock_path: Path) -> dict:
    lines = clock_path.read_text(encoding="utf-8-sig").splitlines()
    header = next(i for i, line in enumerate(lines) if line.startswith("sample,"))
    rows = list(csv.DictReader(lines[header:]))
    points = [(float(x["filetime_100ns"]) / 10_000_000,
               (float(x["qpc_pre"]) + float(x["qpc_post"])) / 2)
              for x in rows if str(x.get("sample", "")).isdigit()]
    xm = math.fsum(x for x, _ in points) / len(points)
    ym = math.fsum(y for _, y in points) / len(points)
    qpf = math.fsum((x - xm) * (y - ym) for x, y in points) / math.fsum((x - xm) ** 2 for x, _ in points)
    edp = ROOT / "T0P/telemetry/reader/emon_joint_qualification.edp"
    config = base_config()
    config.update({
        "profile": "s20", "qpf_hz": qpf,
        "qpf_source": f"least-squares QPC/FILETIME slope from {clock_path.name}; not a default",
        "f_tsc_qualified_hz": 2_112_003_048.8599,
        "f_tsc_qpc_hz": 2_112_003_048.8599,
        "f_tsc_configured_hz": 2_112_000_000.0,
        "f_tsc_qualification_qpf_hz": qpf,
        "emon_local_utc_offset_minutes": -420,
        "edp_path": str(edp.resolve()),
        "edp_sha256": hashlib.sha256(edp.read_bytes()).hexdigest(),
        "event_map_source": "EMON V11.61 -M mapping in saved CLI capture; one effective joint EDP group",
        "event_groups": [ALL_EVENTS], "mode": "temporal", "marks_clock_domain": "qpc_ticks",
        "thermal_history_clear_at_start": False,
    })
    return config


def write_conditioning_marks(clock_path: Path, destination: Path) -> None:
    lines = clock_path.read_text(encoding="utf-8-sig").splitlines()
    header = next(i for i, line in enumerate(lines) if line.startswith("sample,"))
    rows = list(csv.DictReader(lines[header:]))
    begin = next(r for r in rows if r["sample"] == "BEGIN")
    end = next(r for r in rows if r["sample"] == "END")
    qpf = real_config(clock_path)["qpf_hz"]
    q0 = (float(begin["qpc_pre"]) + float(begin["qpc_post"])) / 2
    q1 = (float(end["qpc_pre"]) + float(end["qpc_post"])) / 2
    with destination.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["sample_id", "Pcore", "replica", "condition", "qpc_pad_start", "qpc_start",
                                               "qpc_end", "qpc_pad_end", "clock_domain"])
        writer.writeheader()
        for core in range(8):
            writer.writerow({"sample_id": f"conditioning-P{core}", "Pcore": core, "replica": 1, "condition": "DUMMY8P",
                             "qpc_pad_start": q0, "qpc_start": q0 + 0.201 * qpf,
                             "qpc_end": q1 - 0.201 * qpf, "qpc_pad_end": q1, "clock_domain": "qpc_ticks"})


if __name__ == "__main__":
    unittest.main(verbosity=2)
