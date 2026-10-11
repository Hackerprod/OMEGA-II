"""Post-data corrections to the historical H0 derivation (judge-written 2026-10-11 after MD/34).
Does NOT modify raw data, plan or h0_derived_*.json; writes a separate record."""
import json, sys
sys.path.insert(0, str(__import__('pathlib').Path(__file__).parent))
import h0_derive as d
c = d.c
W = [262152, 294912, 524286, 786438, 1048572, 1310724, 1572858, 1835010, 2097144, 2621448, 3145734]
WREAL = {262152: 262080, 294912: 294912, 524286: 524160, 786438: 786528, 1048572: 1048608, 1310724: 1310688,
         1572858: 1572768, 1835010: 1835136, 2097144: 2097216, 2621448: 2621376, 3145734: 3145824}
cw = {w: c(f"04_P8_Knee_K16_m1_W{w}_A_A_1") for w in W}  # dedicated cells incl. 294,912
per_byte_ps = {w: cw[w] * 1000.0 / WREAL[w] for w in W}
plateau = (per_byte_ps[262152] + per_byte_ps[294912]) / 2
cell = lambda K, v, i: f"05_P8_W294912_K{K}_m1_{v}_{i}"
A = {1: c(cell(1,'A',2)), 8: c(cell(8,'A',2)), 16: c(cell(16,'A',1))}
B = {1: c(cell(1,'B',1)), 8: c(cell(8,'B',3)), 16: c(cell(16,'B',2))}
C = {8: c(cell(8,'C',1)), 16: c(cell(16,'C',3))}
marg = {}
for K in (8, 16):
    T = lambda X: K * X[K]
    mA = (T(A) - A[1]) / (K - 1); mB = (T(B) - B[1]) / (K - 1); mC = (T(C) - A[1]) / (K - 1)
    marg[K] = dict(c_A_marg=mA, c_B_marg=mB, c_C_marg=mC,
                   rho_original_conversacion_cA_over_cB=mA / mB,
                   rho_max_AB_AC_MD29_operator=max(mA / mB, mA / mC),
                   note="rho_original = c_A/c_B (Conv.L2813-2825; Sol MD/34: 0.91402 K=8, 0.90630 K=16). The max(A/B,A/C) operator is MD/29's and uses a C K=1 reference equal to A K=1 (C undefined for K<2; improvised, no norm defines it)")
PA = {m: d.P(cellm, m) for m, cellm in {1: "05_P8_W294912_K16_m1_A_1", 4: "08_P8_R4_K16_W294912_m4_A_1",
      8: "07_P8_K16_W294912_m8_A_A_1", 16: "07_P8_K16_W294912_m16_A_A_1", 32: "07_P8_K16_W294912_m32_A_A_1",
      2: "07_P8_K16_W294912_m2_A_A_1"}.items()}
P_INSTR_VNNI_P = 128.68  # GMAC/s per P-core, T0.0 official (phase1_P_instr_by_core.csv)
out = dict(record="h0_corrections_post_MD34", segment=d.SEG, author="JUEZ (Claude), after data, per MD/34 D-B/D-C/D-D; not frozen-before-measurement code",
           raw_data_modified=False, derived_historical_unchanged="results/T0.1/h0_derived_h0_official_20261011b.json",
           H0_VERDICT="H0_FAIL (firme; MD/34 D-A)",
           H0_K_LITERAL_MD29="PASS (resultado mecánico histórico; no es evidencia de rodilla)", H0_K_SCIENTIFIC="NOT_ESTABLISHED",
           C_L2_knee=None, B_L2_operational=None,
           H0_K_diagnostic_only_not_a_gate=dict(cost_per_byte_ps_by_W_target=per_byte_ps, plateau_ps_per_B=plateau,
               max_ratio_to_plateau=max(v / plateau for v in per_byte_ps.values()), min_ps_per_B=min(per_byte_ps.values()), max_ps_per_B=max(per_byte_ps.values()),
               dedicated_cell_for_294912="04_P8_Knee_K16_m1_W294912_A_A_1", c_A_dedicated_294912=cw[294912],
               work_ratio_524160_over_294912=524160 / 294912, cost_ratio=cw[524286] / cw[294912]),
           marginal_rho_reconstruction=marg,
           matrixization_original_S8_S16=dict(S8=PA[8] / PA[1], S16=PA[16] / PA[1]),
           eta_instr_VNNI_per_core=dict(P_instr_P_GMACs=P_INSTR_VNNI_P, eta_by_m={m: PA[m] / P_INSTR_VNNI_P for m in PA},
               note="P_A(m)=MAC/ns of the worker at W=294,912 (8P, VNNI, K=16, A); P_instr from T0.0"),
           SMT_note="SMT wall comparison per Opus/Sol recompute: ~31,592.9 vs ~31,222.3 ns/app (SMT ~1.2% slower); summed sibling times are not latency",
           limitations=["eviction cost IS instrumented in the raw CSVs (initial_eviction_ns, eviction_between_ns; always outside kernel_only_ns); pure barrier/sync overhead is NOT separately instrumented (residual mixes synchronization, instrumentation and coordination); no new measurement authorized",
                        "raw column weight_storage_address=0 in ALL official timed rows: per-application addresses NOT RECORDED (only shared_weight_storage_address was); B-copy distinctness was verified by QA before timing; no addresses reconstructed",
                        "W sweep max A weight footprint 24 MiB < nominal LLC 30 MiB: no LLC->DRAM transition demonstrated",
                        "scope reduction: K swept only at m=1 and m only at K=16 (no full K×m grid)", "PRIMARY_WORKER_POLICY / P-E effects / TOPOLOGY_ALTERNATIVE_FOR_T0.2: not selected, NOT_APPLICABLE after H0_FAIL (MD/34); descriptive 4P/E/P+E/SMT rows reported in h0_derived", "eviction validation certifies local L2 perturbation, not full LLC eviction; B copies are INDEPENDENT_NON_ALIASING_ALLOCATIONS (no PFN)"])
p = d.OUT / f"h0_corrections_{d.SEG}.json"
if not p.exists(): p.write_text(json.dumps(out, indent=2), encoding="utf-8")
print(json.dumps({k: out[k] for k in ("H0_K_diagnostic_only_not_a_gate", "marginal_rho_reconstruction", "matrixization_original_S8_S16", "eta_instr_VNNI_per_core")}, indent=1)[:3500])

# ---- D-8 (MD/29 14.2): expulsion cost reported from raw columns (no new measurement) ----
import csv, statistics, pathlib
def evict_costs(cell):
    ini, btw = [], []
    for r in csv.DictReader((d.RAW / f"{d.SEG}_{cell}.csv").open(newline="", encoding="utf-8")):
        if r["status"] != "OK": continue
        if int(r["application_index"]) == 0 or r["initial_eviction_ns"] != "0": ini.append(float(r["initial_eviction_ns"]))
        if float(r["eviction_between_ns"]) > 0: btw.append(float(r["eviction_between_ns"]))
    return dict(median_initial_eviction_ns_per_episode_per_worker=statistics.median([x for x in ini if x > 0]) if any(x > 0 for x in ini) else None,
                median_eviction_between_ns_per_application=statistics.median(btw) if btw else None, n_between=len(btw),
                scope="outside kernel_only_ns; per worker; 8 MiB load-only sweep")
ev = {nm: evict_costs(cell) for nm, cell in [("R1_A", "05_P8_W294912_K16_m1_A_1"), ("R1_B", "05_P8_W294912_K16_m1_B_2"), ("R1_C", "05_P8_W294912_K16_m1_C_3"),
                                             ("R4_C", "08_P8_R4_K16_W294912_m4_C_2")]}
p2 = d.OUT / f"h0_eviction_cost_{d.SEG}.json"
assert not p2.exists()
p2.write_text(json.dumps(dict(record="h0_eviction_cost_from_raw", cells=ev), indent=2), encoding="utf-8")
print(json.dumps(ev, indent=1))
