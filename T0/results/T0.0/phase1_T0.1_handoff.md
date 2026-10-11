# T0.0 → T0.1 measured handoff

## Platform

- CPU: 13th Gen Intel Core i7-13700F; 8 P-cores + 8 E-cores; 24 logical processors.
- Cache topology: 2 MiB private P-core L2; 4 MiB per four-E-core cluster; 30 MiB shared LLC; P-core SMT siblings are recorded in `b_topologia.csv`.
- Memory: firmware-reported 2 × 16 GiB DDR5-5200 DIMMs on two populated channels. Preserve `SEP_MEMORY_TOPOLOGY=CONFLICTING_OBSERVATION`; firmware SMBIOS/WMI is governing per MD/23.
- ISA: AVX2 and AVX-VNNI-256 available; AVX-512 unavailable.

## Kernel and measurement contract

- Q4 payload: 18 bytes per 32 packed weights; logical demand factor `q_eff=0.5625 B/MAC`.
- Official tile policy: m=1→1; m=2→2; m≥4→4. Dispatch is outside the timed region.
- QPC wall timing; N=31; warmup=5; calibrated target=150 ms; minimum accepted timed region=100 ms; zero outlier discards; one existing-policy technical retry maximum at 2× fixed units.
- Affinity/topology and environmental observations are in `phase1_session_report.md`, preflight logs and `b_topologia.csv`.

## Final compute and memory tables

- `P_instr_i`: `phase1_P_instr_by_core.csv`.
- `P_Q4_i(m=1)` and exact `BW_Q4_logical,i = v_i × 0.5625 B/MAC`: `phase1_P_Q4_i_m1_by_core.csv`.
- P_ref full `P_Q4(m)`, peaks, `eta_instr`, and `eta_Q4`: `phase1_P_Q4_by_core_m.csv`, `phase1_P_Q4_peak_by_core.csv`, and `phase1_P_Q4_by_type_m.csv`.
- E-core m=1 confirmation: `phase1_E_Q4_m1_by_core.csv` and `phase1_E_core_P_instr_vs_Q4.csv`.
- Simultaneous team metrics: `phase1_TEAM_MAC_RATE_8P.csv` and `phase1_BW_DRAM_read_8P.csv`.
- `D_read` values and gate: `phase1_D_read.csv`; A/C remain `DEFERRED_TO_T0_1`; B is the measured P_Q4_8P_m1 result; old r_A/r_B/r_C remain descriptive only.
- Historical `BW_memcpy_logical(n)` values remain in `f_ancho_banda.csv` as legacy logical read+write-copy rates; they are not the `D_read` denominator and are not reinterpreted as physical traffic.
- Effective-frequency source/status: `phase1_raw/effective_frequency_attempt.json`; unavailable is an allowed non-gating outcome.

## Boundary for T0.1

These are characterization inputs, not H0 tuning or a final ISA/work-policy selection. Do not open T0.1 AMBs until T0.0 is marked `IMPLEMENTADA`, then `VERIFICADA_POR_JUEZ`, inspected by Opus, and marked Completed under the Roadmap.
