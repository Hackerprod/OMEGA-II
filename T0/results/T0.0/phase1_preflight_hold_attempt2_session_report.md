# T0.0 Official Phase 1 Session Report — Preflight HOLD, Attempt 2

## Decision

**SESSION=HOLD. No timed series started. No further retry was made.** This was the single retry authorized after the prior preflight HOLD and 90-second idle wait.

## Preflight values

| Condition | Observed | Frozen gate | Result |
|---|---:|---:|---|
| Chrome before/after preflight | 0 / 0 | absent | PASS |
| Power plan | `8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c` | exact match | PASS |
| CPU | `13th Gen Intel(R) Core(TM) i7-13700F`; 24 logical processors | exact match | PASS |
| Frozen topology SHA-256 | `8E5CDA8410873852C3DD266E2CC1535376DEEA3705328173BC879117F4574CAF` | exact match | PASS |
| Mean total CPU | 5.818% | ≤5% | **FAIL** |
| Peak total CPU | 12% | ≤10% | **FAIL** |
| Processes above 2% of total CPU | none | none | PASS |

The threshold values were unchanged. CPU samples were 6, 4, 0, 11, 4, 9, 7, 5, 4, 2, and 12 percent. The runner recorded `SESSION=HOLD` before E-order preregistration and before any performance series.

## Integrity and series status

- Authorization token: `AUTORIZADO`
- Correctness remains PASS from correctness-only attempt 22; no correctness or timing series was rerun during this attempt.
- Valid series: 0; invalid series: 0; technical retries: 0.
- Performance samples/calibrations: 0; D_read/readiness gate: not calculated.
- Attempt 1 artifacts remain preserved as `phase1_preflight_hold_attempt1_{execution_status,session_summary,session_report}`.
- Attempt 2 status and summary: `phase1_preflight_hold_attempt2_execution_status.json`, `phase1_preflight_hold_attempt2_session_summary.json`.
- Preflight log: `logs/phase1_official_session_preflight.stdout.txt`; command history: `logs/COMMANDS.json`.

No further session attempt was launched. Awaiting JUEZ direction.
