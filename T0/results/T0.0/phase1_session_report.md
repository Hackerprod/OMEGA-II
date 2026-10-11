# T0.0 Official Phase 1 Re-measurement Report

> MD/28 closure completed from the accepted 63-series session plus 16 additional P-core series; no previous series were rerun.

## Session and integrity

- Session: **COMPLETE**; the accepted run was executed in three tranches: 47 initial series, 16 E-core series after the parser fix, and the 16 MD/28 P-core m=1 series.
- Tranche 1: P_instr, P_Q4(m), P_Q4_8P_m1, BW_DRAM_read_8P completed; all 47 series preserved unchanged.
- Tranche 2: parser corrected and validated against the preserved order/seed; only the 16 E-core Q4 m=1 series were run. No earlier series were rerun.
- Correctness-only attempt 22: **PASS**; correctness fixture fields are bit-exact/allclose/finite as specified.
- Timed build attempt 39 and QA build attempt 39: exit 0.
- Pre-tranche-3 non-timing checks: build attempt 41 (timed harness and QA) exit 0; correctness-only attempt 23 PASS. After the documentary/code corrections, QA-only build attempt 42 passed without rebuilding the measured timed binary; correctness-only attempt 24 PASS, including explicit rejection of unsupported M=3 and M=6 before kernel dispatch.
- Final `SHA256SUMS.txt`: 953 entries; revalidated after the documentation corrections.
- Tranche 2 preflight values retained in the contemporaneous report: mean CPU 5.545%, peak 11%, 175 processes. Its stdout was subsequently overwritten and is now formally marked `NO RECUPERABLE` in `INCIDENT_preflight_segment2_overwritten.md` (original stdout hash `E2FB5B40…`).
- Tranche 3 preflight passed with Chrome absent before/after, expected power plan and hardware/topology. It recorded mean CPU 6.636%, peak 14%, 177 processes as observations only; no CPU-load gate applied.

## Series integrity

| Family | SERIES_COMPLETE | Invalid | Technical retries | Measured samples | Raw rows |
|---|---:|---:|---:|---:|---:|
| P_instr (8 P + 8 E, both ISAs) | 32 | 0 | 0 | 992 | 992 |
| P_Q4(m), P_ref, six M × two ISAs | 12 | 0 | 0 | 372 | 372 |
| P_Q4_8P_m1 (both ISAs) | 2 | 0 | 0 | 62 team intervals | 496 worker rows |
| BW_DRAM_read_8P | 1 | 0 | 0 | 31 team intervals | 248 worker rows |
| E-core Q4 m=1 (8 E × two ISAs) | 16 | 0 | 0 | 496 | 496 |
| P-core Q4 m=1 (8 P × two ISAs) | 16 | 0 | 0 | 496 | 496 |
| **Total** | **79** | **0** | **0** | |

All 79 series have `SERIES_COMPLETE` in `phase1_raw/series_status.csv`; zero invalid series and zero technical retries. No `INVALID_TIMER_TOO_SHORT` rows occurred. Minimum observed sample duration for the added P-core m=1 family was 143.49 ms; all exceed the contractual 100 ms minimum. Calibration target was 150 ms and calibration rows are excluded from N=31 statistics.

## Derived performance results

### Simultaneous 8P and D_read

| ISA | BW_Q4_logical_8P | BW_DRAM_read_8P | D_read | Gate |
|---|---:|---:|---:|---|
| AVX2 | 75.0364 GB/s | 74.4558 GB/s | 1.007798 | PASS |
| AVX-VNNI-256 | 77.4582 GB/s | 74.4558 GB/s | 1.040325 | PASS |

`D_read_gate_max = 1.040325`; `max(D_read_AVX2, D_read_VNNI) ≥ 1.00` is **PASS**. The 8P numerator uses simultaneous team MACs divided by a single team wall interval, not summed single-core rates.

### P_Q4(m), P_ref

P_ref: `CORE_API_00_G0_MASK_0_1`.

| ISA | P_Q4_peak | M at peak | eta_instr at peak |
|---|---:|---:|---:|
| AVX2 | 29.2728 GMAC/s | 32 | 0.256543 |
| AVX-VNNI-256 | 30.9782 GMAC/s | 8 | 0.242117 |

`phase1_P_Q4_by_core_m.csv` contains the full M sweep and exact `M/m_tile` logical traversal accounting. `eta_instr` remains diagnostic; no minimum threshold applies.

### E-core split: P_instr vs Q4

Source: `phase1_E_core_P_instr_vs_Q4.csv` (per-core paired values), with class ranges in `phase1_P_instr_by_type.csv` and `phase1_E_Q4_m1_by_type.csv`.

| ISA | E-core P_instr median (range) | E-core Q4 m=1 median (range) | eta_instr m=1 median |
|---|---:|---:|---:|
| AVX2 | 32.391 GMAC/s (32.255–32.475) | 6.556 GMAC/s (6.517–6.570) | 0.202174 |
| AVX-VNNI-256 | 32.439 GMAC/s (32.347–32.517) | 6.930 GMAC/s (6.903–6.957) | 0.213686 |

The eight E-core P_instr values are tightly grouped, as are the Q4 m=1 values within each ISA; the previously noted two-population split is not apparent in this remeasurement.

## Session conclusion

- `D_read` gate: **PASS** (both routes pass; maximum 1.040325).
- Correctness: **PASS**.
- Series integrity: **79 complete, zero invalid, zero retries**.
- Final derived tables were generated after all 79 series and after the single bounded effective-frequency attempt.
- T0.1/H0 remains governed by its own held state and required JUEZ/Sol process.

## Incident decisions retained for final closure

- `UNAUTHORIZED_PREFLIGHT_GATE=REMOVED_PRE_DATA`. The observed CPU background values are recorded only; they are not retrospective exclusion thresholds.
- `SESSION_SEGMENT_1=series 1..47`; `SESSION_SEGMENT_2=16 E-core Q4 m=1 series`; `SESSION_SEGMENT_3=16 P-core Q4 m=1 series`; `segment_boundary_reason=PARSER_FIX_PRE_E_DATA` for the first boundary. The parser defect occurred before E-core data and does not invalidate the first two segments.
- `PREFLIGHT_SEGMENT_2_RAW_OVERWRITTEN=NO RECUPERABLE`, original SHA-256 `E2FB5B40107DC86FA3B4FC1E215B17B40DA21B9A81B42DD0812B1509FE844A6A`. The only surviving values are those recorded above and in the incident file; environmental values were observational, not gates.
- Segment 3 preserved all earlier results and added only the 16 individual P-core m=1 series. Its stdout is the current contents of the historical shared path `logs/phase1_resume_official_session_preflight.stdout.txt`; the runner is now changed to use unique per-segment preflight and status filenames going forward.

## Evidence

- Session state/summary for the completed tranche 3: `phase1_execution_status.json`, `phase1_session_summary.json` (these record the last run; the runner has now been changed to emit unique status filenames per future segment).
- Full raw/calibration/series status: `phase1_raw/`
- Derived results: `phase1_T0.0_final_table.csv`, `phase1_D_read.csv`, `phase1_readiness.json`, `phase1_P_instr_by_core.csv`, `phase1_P_Q4_by_core_m.csv`, `phase1_P_Q4_peak_by_core.csv`, `phase1_P_Q4_i_m1_by_core.csv`, `phase1_E_core_P_instr_vs_Q4.csv`
- Legacy labels and incident: `legacy_labels_manifest.md`, `INCIDENT_preflight_segment2_overwritten.md`.
- The legacy `e_pico_mac.csv` deliverable name does not exist; actual files are `e.md`, `e_MACconv.md`, `phase1_P_Q4_peak_by_core.csv` and `phase1_P_Q4_peak_by_type.csv`.
- Three-tranche provenance: `phase1_session_launch.md`, `phase1_session_partial_before_resume_attempt3_report.md`, `INCIDENT_preflight_segment2_overwritten.md`
- Command/log manifest: `logs/COMMANDS.json`


## MD/28 final closure

- Final series count: 79 completed; 0 invalid/held series; 0 series with a technical retry record.
- `UNAUTHORIZED_PREFLIGHT_GATE=REMOVED_PRE_DATA`.
- `SESSION_SEGMENT_1=series 1..47`; `SESSION_SEGMENT_2=16 E-core Q4 m=1 series`; `segment_boundary_reason=PARSER_FIX_PRE_E_DATA`.
- The 16 required P-core Q4 m=1 series are complete. P_ref comparison is descriptive and introduces no post-hoc outlier gate.
- `A_4P=DEFERRED_TO_T0_1`; `B_8P=MEASURED_T0_0`; `C_8P_PLUS_8E=DEFERRED_TO_T0_1`; `r_A/r_B/r_C=LEGACY_PREOPT_DESCRIPTIVE`.
- `EFFECTIVE_FREQUENCY=UNAVAILABLE_WITH_QUALIFIED_TOOLS`; attempt count=1. No throughput- or PDH-derived frequency is used.

### P-core individual m=1

| Physical core | ISA route | v_i = P_Q4_i (conventional MAC/s) | BW_Q4_logical,i (B/s) | P_instr_i (conventional MAC/s) | eta_instr |
|---|---|---:|---:|---:|---:|
| CORE_API_02_G0_MASK_4_5 | AVX2 | 16760900000.000000 | 9428006250.000000 | 114906000000.000000 | 0.145866 |
| CORE_API_02_G0_MASK_4_5 | AVX-VNNI-256 | 17365600000.000000 | 9768150000.000000 | 128036000000.000000 | 0.135631 |
| CORE_API_00_G0_MASK_0_1 | AVX-VNNI-256 | 17438300000.000000 | 9809043750.000000 | 127947000000.000000 | 0.136293 |
| CORE_API_00_G0_MASK_0_1 | AVX2 | 16669500000.000000 | 9376593750.000000 | 114105000000.000000 | 0.146089 |
| CORE_API_06_G0_MASK_12_13 | AVX2 | 17056400000.000000 | 9594225000.000000 | 115444000000.000000 | 0.147746 |
| CORE_API_06_G0_MASK_12_13 | AVX-VNNI-256 | 17678100000.000000 | 9943931250.000000 | 129634000000.000000 | 0.136369 |
| CORE_API_04_G0_MASK_8_9 | AVX-VNNI-256 | 17063000000.000000 | 9597937500.000000 | 129968000000.000000 | 0.131286 |
| CORE_API_04_G0_MASK_8_9 | AVX2 | 16396800000.000000 | 9223200000.000000 | 115749000000.000000 | 0.141658 |
| CORE_API_05_G0_MASK_10_11 | AVX2 | 16962800000.000000 | 9541575000.000000 | 115884000000.000000 | 0.146377 |
| CORE_API_05_G0_MASK_10_11 | AVX-VNNI-256 | 17576800000.000000 | 9886950000.000000 | 129324000000.000000 | 0.135913 |
| CORE_API_01_G0_MASK_2_3 | AVX-VNNI-256 | 17458500000.000000 | 9820406250.000000 | 127386000000.000000 | 0.137052 |
| CORE_API_01_G0_MASK_2_3 | AVX2 | 16656600000.000000 | 9369337500.000000 | 114368000000.000000 | 0.145640 |
| CORE_API_03_G0_MASK_6_7 | AVX2 | 16638900000.000000 | 9359381250.000000 | 113917000000.000000 | 0.146062 |
| CORE_API_03_G0_MASK_6_7 | AVX-VNNI-256 | 17535900000.000000 | 9863943750.000000 | 127977000000.000000 | 0.137024 |
| CORE_API_07_G0_MASK_14_15 | AVX-VNNI-256 | 17625600000.000000 | 9914400000.000000 | 129790000000.000000 | 0.135801 |
| CORE_API_07_G0_MASK_14_15 | AVX2 | 17038700000.000000 | 9584268750.000000 | 115499000000.000000 | 0.147522 |

### P_ref Q4 peak and D_read

| ISA route | P_Q4_peak (conventional MAC/s) | m at peak |
|---|---:|---:|
| AVX2 | 29272800000.000000 | 32 |
| AVX-VNNI-256 | 30978200000.000000 | 8 |

| ISA route | BW_Q4_logical_8P (B/s) | BW_DRAM_read_8P (B/s) | D_read |
|---|---:|---:|---:|
| AVX2 | 75036400000.000000 | 74455800000.000000 | 1.007798 |
| AVX-VNNI-256 | 77458200000.000000 | 74455800000.000000 | 1.040325 |

### Effective-frequency attempt

| Target | Core | Status | Measured value | Source | Formula or outcome |
|---|---|---|---|---|---|
| P_ref | CORE_API_00_G0_MASK_0_1 | UNAVAILABLE_WITH_QUALIFIED_TOOLS |   |  | VTune-imported SEP data did not expose usable APERF/MPERF or THREAD/REF_TSC counts |
| E_representative | CORE_API_08_G0_MASK_16 | UNAVAILABLE_WITH_QUALIFIED_TOOLS |   |  | VTune-imported SEP data did not expose usable APERF/MPERF or THREAD/REF_TSC counts |

Full derived tables: `phase1_T0.0_final_table.csv`, `phase1_P_Q4_i_m1_by_core.csv`, `phase1_P_ref_vs_P_core_m1.csv`, `phase1_TEAM_MAC_RATE_8P.csv`, `phase1_BW_DRAM_read_8P.csv`, `phase1_D_read.csv`, and the existing P_instr/P_Q4/E-core CSVs. T0.1 must await JUEZ verification and Opus inspection.
