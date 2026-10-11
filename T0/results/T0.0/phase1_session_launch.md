# T0.0 MD/28 continuation launch

## Current state

- The official MD/26–27 session is accepted and complete: all 63 original series remain preserved, including the 16 E-core Q4 m=1 series.
- MD/28 requires only the remaining 16 individual P-core Q4 m=1 series, one bounded SEP/VTune effective-frequency attempt, and final derivation/handoff artifacts.
- Build attempt 41 (timed harness) and QA build attempt 41 both compile successfully. Correctness-only attempt 23 passes. The summarizer's synthetic-only test passes.
- **No MD/28 timing series or frequency attempt has been run.** No `AUTORIZADO` has been provided for this continuation.

## Exact continuation command

After the user provides the fresh token `AUTORIZADO`, close Chrome, confirm the frozen high-performance power plan and hardware/topology preflight conditions, then wait 60 seconds before running once:

```powershell
& "C:\Projects\OMEGA\.venv_cuda\Scripts\python.exe" "C:\Projects\OMEGA\Ejercise\T0\src\capture_phase1_measurements.py" AUTORIZADO --resume-pcore-m1
```

The continuation runner verifies all 63 prior completed series, checks the existing E-core output and P-core m=1 output are not being overwritten, runs the official Chrome/power/hardware/topology preflight, and either verifies the preregistered P-core order or creates it with seed `4D44323850314D31` before timing. It then runs **only** the 16 P-core series (`8 physical P cores × 2 routes`, `m=1`, `N=31`, 5 warmups, 150 ms calibration target, 100 ms minimum sample), preserving the fixed-units technical retry policy.

After all 16 series succeed, the runner makes exactly one bounded frequency attempt: a P_ref AVX2 P_instr load and one representative E-core AVX2 P_instr load. It imports SEP 5.61.22 P-state output in VTune 2026.4 and accepts only unambiguous APERF/MPERF or documented THREAD/REF_TSC evidence. It will not derive GHz from benchmark throughput or PDH. An inconclusive attempt is recorded as `UNAVAILABLE_WITH_QUALIFIED_TOOLS` and is not retried.

Only after the new measurements and frequency-attempt result exist does the runner regenerate the derived tables and final T0.0 table. A and C remain `DEFERRED_TO_T0_1`; B remains the already measured simultaneous `P_Q4_8P_m1`; legacy r_A/r_B/r_C remain `LEGACY_PREOPT_DESCRIPTIVE`.

## Frozen preflight gates

- Chrome absent before and after the 10-second process observation.
- Active power-plan GUID `8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c`.
- CPU `13th Gen Intel(R) Core(TM) i7-13700F`, 24 logical processors, frozen topology SHA-256 `8E5CDA8410873852C3DD266E2CC1535376DEEA3705328173BC879117F4574CAF`, and matching live physical-core masks/efficiency classes.
- CPU/background process load is recorded as observation only; no post-hoc load threshold is applied.
- Any governing preflight failure writes `SESSION=HOLD` before performance timing begins.

## Preserved earlier tranches

- Segment 1: the original 47 series; the E-order parser issue occurred before any E-core data.
- Segment 2: the 16 E-core Q4 m=1 series after the parser correction.
- Boundary reason: `PARSER_FIX_PRE_E_DATA`.
- The unauthorized CPU-load gate was removed pre-data: `UNAUTHORIZED_PREFLIGHT_GATE=REMOVED_PRE_DATA`.
- Neither incident invalidates or authorizes rerunning earlier data. Preserve both tranche preflights and all earlier raw outputs.

## Frozen continuation parameters

- P-core order: `std::mt19937_64 + std::shuffle`, seed hex `4D44323850314D31`, over stable physical P-core API keys.
- Route order alternates by preregistered core index: AVX2 first for even indices, AVX-VNNI-256 first for odd indices.
- `BW_Q4_logical,i = v_i × 0.5625 B/MAC`; this is logical weight-demand accounting, never `BW_L2,i` or physical L2 traffic.
- No H0 tuning, A/C measurement, scope expansion, or repeat of the 63 completed series.

## Expected outputs

- Raw P-core series: `phase1_raw/p_q4_pcore_m1_raw.csv`, summary, calibration and series-status rows.
- Preregistered order: `phase1_raw/p_core_m1_order.json`.
- Frequency attempt: `phase1_raw/effective_frequency_attempt.json` plus target profiles/import/report artifacts.
- Final derivations: `phase1_P_Q4_i_m1_by_core.csv`, `phase1_P_ref_vs_P_core_m1.csv`, `phase1_TEAM_MAC_RATE_8P.csv`, `phase1_BW_DRAM_read_8P.csv`, `phase1_ABC_status.csv`, and `phase1_T0.0_final_table.csv`, alongside existing P_instr, P_Q4(m), peak, D_read and E-core outputs.
- Final `phase1_session_report.md`, `phase1_T0.1_handoff.md`, incident records in the report/status, and refreshed `SHA256SUMS.txt` after completion.
