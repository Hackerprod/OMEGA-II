# T0.0 — Official Phase 1 Re-measurement Report

> Populate only from the completed `phase1_session_summary.json`, raw CSVs, derived tables, and frozen plan. Do not fill from calibration trials or synthetic tests.

## Session and provenance

- Session status: `[COMPLETE / HOLD]`
- Authorization token recorded: `[AUTORIZADO]`
- Start / end UTC: `[ ]` / `[ ]`
- Timed harness SHA-256: `[ ]`
- Frozen plan SHA-256: `[ ]`
- `SHA256SUMS.txt` verification: `[entry count; mismatches]`
- Preflight: Chrome `[ ]`; power-plan GUID `[ ]`; CPU/topology match `[ ]`; mean/max CPU observation `[ ]` / `[ ]`; active processes with CPU percentages `[reference preflight JSON]`; live topology API compare `[PASS/HOLD]`
- Session sequence and command records: `[phase1_execution_status.json; COMMANDS.json]`

## Series integrity

| Family | Planned series | Completed | Technical retries | Held/invalid | Per-sample minimum | Notes |
|---|---:|---:|---:|---:|---:|---|
| P_instr (8 P + 8 E, both routes) | 32 | | | | 100 ms | |
| P_Q4(m), P_ref, six M × two routes | 12 | | | | 100 ms | |
| P_Q4_8P_m1 (both routes) | 2 | | | | 100 ms | |
| BW_DRAM_read_8P | 1 | | | | 100 ms | |
| E-core Q4 m=1 (8 E × two routes) | 16 | | | | 100 ms | |

Calibration is `ENGINEERING_CALIBRATION_ONLY` and excluded from all N=31 statistics. List any first-attempt `SERIES_INVALID_TIMER_TOO_SHORT` and the result of its single 2× fixed-units retry. A second short attempt is `SERIES=HOLD`; no session-level derived gate is then reported.

## P_instr

Source table: `phase1_P_instr_by_core.csv`; class aggregation: `phase1_P_instr_by_type.csv`.

| Core class | ISA | Core count | Median of core medians (MAC/s) | IQR | MAD | Min–max core median |
|---|---|---:|---:|---:|---:|---:|
| P-core | AVX2 | | | | | |
| P-core | AVX-VNNI-256 | | | | | |
| E-core | AVX2 | | | | | |
| E-core | AVX-VNNI-256 | | | | | |

`EFFECTIVE_FREQUENCY`: `[UNAVAILABLE / trusted source and value]`.

## P_Q4(m), P_ref

P_ref: `[physical_core_api_key]`. Source tables: `phase1_P_Q4_by_core_m.csv`, `phase1_P_Q4_peak_by_core.csv`.

| ISA | M | m_tile | P_Q4 (MAC/s) | eta_instr | P_Q4_peak (MAC/s) | eta_Q4 | Canonical payload (B/call) | Logical traversals/call | Logical payload load (B/sample) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| AVX2 | | | | | | | | | |
| AVX-VNNI-256 | | | | | | | | | |

Canonical payload and logical traversal counts are separate; logical bytes are not physical DRAM/cache traffic.

## Simultaneous 8P and D_read

Source tables: `p_q4_8p_m1_summary.csv`, `bw_dram_read_8p_summary.csv`, `phase1_D_read.csv`, and `phase1_readiness.json`.

| ISA | TEAM_MAC_RATE_8P (MAC/s) | BW_Q4_logical_8P (B/s) | D_read | D_read ≥ 1.00 |
|---|---:|---:|---:|---|
| AVX2 | | | | |
| AVX-VNNI-256 | | | | |

- BW_DRAM_read_8P (load-only): `[ ] B/s`
- Gate: `max(D_read_AVX2, D_read_VNNI) = [ ]`; `[PASS / HOLD]`
- TEAM_MAC_RATE uses summed simultaneous MACs divided by one common wall interval; single-core rates are not summed.

## E-core Q4 confirmation

Order/seed: `phase1_raw/e_confirm_order.json`. Per-core data: `phase1_E_Q4_m1_by_core.csv`; descriptive class summary: `phase1_E_Q4_m1_by_type.csv`.

| E-core | ISA | P_Q4 m=1 (MAC/s) | IQR | MAD | Series attempt |
|---|---|---:|---:|---:|---:|
| `[one row per physical E-core and route]` | | | | | |

Paired E-core comparison: `phase1_E_core_P_instr_vs_Q4.csv`; use descriptively to compare the per-core P_instr and Q4 patterns, not as a new gate.

## Interpretation and gate

- Correctness: `[PASS/HOLD; cite latest fixture-level QA log]`
- HOT_PATH_CLEAN: `[PASS/HOLD; cite review]`
- `D_read` readiness gate: `[PASS/HOLD]`
- `eta_instr`: diagnostic only; no minimum threshold.
- Session conclusion: `[ ]`
- T0.1/H0 status: `[HOLD / next authorized state]`

## Files

- Session summary: `phase1_session_summary.json`
- Execution status and commands: `phase1_execution_status.json`, `logs/COMMANDS.json`
- Raw series/calibration/status: `phase1_raw/`
- Derived tables: `phase1_*.csv`, `phase1_readiness.json`
- Hash manifest: `Ejercise/T0/SHA256SUMS.txt`
