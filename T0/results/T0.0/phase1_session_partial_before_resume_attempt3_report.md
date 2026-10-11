# T0.0 Official Phase 1 Session Report — Stopped at E-core Q4

## Session status

**SESSION=HOLD (incomplete).** The preflight passed, and the single authorized runner proceeded through P_instr, P_Q4(m), P_Q4_8P_m1 and BW_DRAM_read_8P. The final E-core Q4 command exited immediately before starting its series because the preregistered order reader parsed the JSON array punctuation as a core key:

```text
phase1_measure_harness_error: unknown core key in preregistered E-core order: : [
```

No retry or second session launch was made. `summarize_phase1.py` was not run because the prescribed series sequence did not complete.

## Preflight observations and gates

- Chrome: 0 before and after.
- Power plan GUID: expected `8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c` — PASS.
- CPU and topology SHA-256 — PASS: i7-13700F, 24 LP, `8E5CDA8410873852C3DD266E2CC1535376DEEA3705328173BC879117F4574CAF`.
- Live topology API comparison and E-order preregistration — PASS.
- CPU/background information was recorded only: mean 5%, peak 10%, 175 active processes; no CPU threshold was applied as a gate.

## Series integrity

`phase1_raw/series_status.csv` contains **47 SERIES_COMPLETE**, zero invalid series, and zero technical retries:

| Family | Valid | Invalid | Not run |
|---|---:|---:|---:|
| P_instr (8 P + 8 E, both routes) | 32 | 0 | 0 |
| P_Q4(m), P_ref, six M × two routes | 12 | 0 | 0 |
| P_Q4_8P_m1 (both routes) | 2 | 0 | 0 |
| BW_DRAM_read_8P | 1 | 0 | 0 |
| E-core Q4 m=1 (8 E × two routes) | 0 | 0 | 16 |
| **Total** | **47** | **0** | **16** |

The 47 complete series have N=31 and no timer-short retry. E-core Q4 did not begin. Raw samples and calibration-only records for completed families are preserved; the E-core order file and failed-command logs are also preserved.

## Derived gate

`D_read`, `eta_instr`, `P_Q4_peak` and the readiness gate were not generated in this session because E-core Q4 did not complete. Do not treat partial family summary CSVs as a completed-session gate.

## Evidence

- `phase1_execution_status.json`, `phase1_session_summary.json`
- `phase1_raw/series_status.csv`, family raw/summary CSVs, `engineering_calibration_only.csv`, `e_confirm_order.json`
- `logs/phase1_run_e_core_confirm.stderr.txt`, `logs/COMMANDS.json`
- Preserved earlier HOLDs: `phase1_preflight_hold_attempt1_*` and `phase1_preflight_hold_attempt2_*`
- Correctness-only attempt 22: PASS. No correctness rerun or performance relaunch occurred in this session.

Awaiting JUEZ direction on the preregistered-order parser defect before any continuation.
