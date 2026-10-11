# T0.0 Official Phase 1 Session Report — Preflight HOLD

## Decision

**SESSION=HOLD. No timed series started.** The authorized runner was launched once after the requested 30-second wait and a Chrome process check. Do not retry this session automatically.

## Launch and integrity

- Authorization token: `AUTORIZADO`
- Launch command: `& "C:\Projects\OMEGA\.venv_cuda\Scripts\python.exe" "C:\Projects\OMEGA\Ejercise\T0\src\capture_phase1_measurements.py" AUTORIZADO`
- Runner elapsed: preflight approximately 20 seconds.
- Timed build: `phase1_build_timed_kernel_attempt37`, exit 0.
- QA build: `phase1_build_qa_correctness_attempt37`, exit 0.
- Correctness-only attempt 22: PASS; fixture-level exactness/allclose/finite checks and batch calls=1 vs N=31 PASS.
- SHA256SUMS: 756 entries checked, zero mismatches before this report was added.

## Preflight

| Condition | Observed | Gate | Result |
|---|---:|---:|---|
| Chrome before/after preflight | 0 / 0 | absent | PASS |
| Power plan | `8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c` | exact match | PASS |
| CPU | `13th Gen Intel(R) Core(TM) i7-13700F`, 24 logical processors | exact match | PASS |
| Frozen topology SHA-256 | `8E5CDA8410873852C3DD266E2CC1535376DEEA3705328173BC879117F4574CAF` | exact match | PASS |
| Mean total CPU | 5% | ≤5% | PASS |
| Peak total CPU | 11% | ≤10% | **FAIL** |
| Processes above 2% of total CPU | none | none | PASS |

The frozen peak-CPU threshold failed. Other preflight checks passed. Therefore the runner recorded `SESSION=HOLD` and stopped before E-core order registration or any performance series.

## Series, retries, and gate

- Valid series: 0
- Invalid series: 0 (no series started)
- Technical retries: 0
- Performance samples/calibrations: 0
- D_read / readiness gate: not calculated
- No performance median or derived performance table was produced.

## Evidence paths

- `phase1_execution_status.json`
- `phase1_session_summary.json`
- `logs/phase1_official_session_preflight.stdout.txt`
- `logs/phase1_official_session_preflight.command.txt`
- `logs/COMMANDS.json`

The authorized run did not launch Chrome or any other application. The official `phase1_raw/` directory remains absent.
