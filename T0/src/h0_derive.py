"""H0 offline derivation (judge-written; reads raw/summary data only, no measurement).
Gates per T0.md 4.1 (MD/29, MD/32): c_X = median over N=31 reps of team_sample_cost(rep)."""
import csv, json, math, sys
from pathlib import Path

T0 = Path(__file__).resolve().parents[1]
SEG = "h0_official_20261011b"
RAW = T0 / "results/T0.1/h0_raw" / SEG
OUT = T0 / "results/T0.1"
MAC_UNIT = 512


def team_row(cell):
    p = RAW / f"{SEG}_{cell}.summary.csv"
    for r in csv.DictReader(p.open(newline="", encoding="utf-8")):
        if r["worker_key"] == "TEAM_MAX":
            return r
    raise KeyError(cell)


def c(cell):
    return float(team_row(cell)["median"])


def recompute_c(cell):
    """Independent recomputation of c_X from the raw per-application CSV."""
    cost = {}
    eps = None
    K = None
    for r in csv.DictReader((RAW / f"{SEG}_{cell}.csv").open(newline="", encoding="utf-8")):
        if r["status"] != "OK":
            continue
        k = (int(r["rep"]), r["worker_key"])
        cost[k] = cost.get(k, 0.0) + float(r["kernel_only_ns"])
        eps = int(r["fixed_episodes"]); K = int(r["K"])
    reps = sorted({k[0] for k in cost})
    team = [max(v / (eps * K) for (rr, w), v in cost.items() if rr == rep) for rep in reps]
    team.sort(); n = len(team)
    return (team[n // 2] if n % 2 else (team[n // 2 - 1] + team[n // 2]) / 2), n


def O_of(cell):
    return int(team_row(cell)["O"])


def P(cell, m):
    return O_of(cell) * MAC_UNIT * m / c(cell)  # MAC/ns


def ratios(a, b, cc):
    ca, cb, ccc = c(a), c(b), c(cc)
    rho = max(ca / cb, ca / ccc)
    dbc = abs(cb - ccc) / ((cb + ccc) / 2)
    return dict(c_A=ca, c_B=cb, c_C=ccc, c_A_over_c_B=ca / cb, c_A_over_c_C=ca / ccc, rho=rho, delta_BC=dbc)


def main():
    out = {"segment": SEG, "verdict": "NOT_DECLARED_BY_SCRIPT"}
    # measurement validity
    noise = json.loads((RAW / "R_noise_preflight.json").read_text(encoding="utf-8"))
    ev = json.loads((RAW / "eviction_validation_8MiB.json").read_text(encoding="utf-8"))
    ratios_ev = [x["ratio_median_evicted_over_warm"] for x in ev["cores"]]
    s = sorted(ratios_ev); n = len(s)
    med_ev = s[n // 2] if n % 2 else (s[n // 2 - 1] + s[n // 2]) / 2
    out["measurement"] = dict(R_noise=noise["R_noise"], R_noise_pass=noise["R_noise"] <= 0.05,
                              eviction_status=ev["status"], eviction_mib=ev["buffer_mib_per_p_core"],
                              eviction_median_ratio=med_ev, eviction_min_ratio=min(ratios_ev),
                              eviction_pass=(med_ev >= 1.5 and min(ratios_ev) >= 1.25))
    # R1 / R4
    R1 = ratios("05_P8_W294912_K16_m1_A_1", "05_P8_W294912_K16_m1_B_2", "05_P8_W294912_K16_m1_C_3")
    R4 = ratios("08_P8_R4_K16_W294912_m4_A_1", "08_P8_R4_K16_W294912_m4_B_3", "08_P8_R4_K16_W294912_m4_C_2")
    R1["gate_rho_le_2_3"] = R1["rho"] <= 2 / 3; R1["gate_delta_BC_le_0.15"] = R1["delta_BC"] <= 0.15
    R1["STRONG_RESIDENCY_SIGNAL"] = R1["rho"] <= 0.50
    R4["gate_rho_le_0.90"] = R4["rho"] <= 0.90; R4["gate_delta_BC_le_0.15"] = R4["delta_BC"] <= 0.15
    # cross-check from raw
    xcheck = {}
    for nm, cell in [("R1_A", "05_P8_W294912_K16_m1_A_1"), ("R1_B", "05_P8_W294912_K16_m1_B_2"), ("R1_C", "05_P8_W294912_K16_m1_C_3"),
                     ("R4_A", "08_P8_R4_K16_W294912_m4_A_1"), ("R4_B", "08_P8_R4_K16_W294912_m4_B_3"), ("R4_C", "08_P8_R4_K16_W294912_m4_C_2")]:
        v, nn = recompute_c(cell); xcheck[nm] = dict(c_from_raw=v, c_from_summary=c(cell), n_reps=nn)
    out["H0_R1"] = R1; out["H0_R4"] = R4; out["crosscheck_raw_vs_summary"] = xcheck
    # K sweep (descriptive)
    ks = {}
    for K, cells in {1: ("05_P8_W294912_K1_m1_A_2", "05_P8_W294912_K1_m1_B_1", None), 2: ("05_P8_W294912_K2_m1_A_1", "05_P8_W294912_K2_m1_B_2", None),
                     4: ("05_P8_W294912_K4_m1_A_2", "05_P8_W294912_K4_m1_B_1", None), 8: ("05_P8_W294912_K8_m1_A_2", "05_P8_W294912_K8_m1_B_3", "05_P8_W294912_K8_m1_C_1"),
                     16: ("05_P8_W294912_K16_m1_A_1", "05_P8_W294912_K16_m1_B_2", "05_P8_W294912_K16_m1_C_3")}.items():
        a, b, cc = cells
        d = dict(c_A=c(a), c_B=c(b), c_A_over_c_B=c(a) / c(b))
        if cc: d.update(c_C=c(cc), c_A_over_c_C=c(a) / c(cc), rho=max(c(a) / c(b), c(a) / c(cc)))
        ks[K] = d
    out["K_sweep_descriptive"] = ks
    # H0-M: matrixization
    mcells = {1: "05_P8_W294912_K16_m1_A_1", 2: "07_P8_K16_W294912_m2_A_A_1", 4: "08_P8_R4_K16_W294912_m4_A_1",
              8: "07_P8_K16_W294912_m8_A_A_1", 16: "07_P8_K16_W294912_m16_A_A_1", 32: "07_P8_K16_W294912_m32_A_A_1"}
    PA = {m: P(cell, m) for m, cell in mcells.items()}
    out["H0_M"] = dict(P_A_MAC_per_ns=PA, S4=PA[4] / PA[1], gate_S4_ge_1_5=PA[4] / PA[1] >= 1.5,
                       S_by_m={m: PA[m] / PA[1] for m in PA})
    # H0-K knee
    W = [262152, 294912, 524286, 786438, 1048572, 1310724, 1572858, 1835010, 2097144, 2621448, 3145734]
    WREAL = {262152: 262080, 294912: 294912, 524286: 524160, 786438: 786528, 1048572: 1048608, 1310724: 1310688,
             1572858: 1572768, 1835010: 1835136, 2097144: 2097216, 2621448: 2621376, 3145734: 3145824}
    def kc(w):
        return c("05_P8_W294912_K16_m1_A_1") if w == 294912 else c(f"04_P8_Knee_K16_m1_W{w}_A_A_1")
    cA = {w: kc(w) for w in W}
    c0 = (cA[262152] + cA[294912]) / 2  # median of two values
    knee = None
    for i in range(len(W) - 1):
        if cA[W[i]] >= 1.15 * c0 and cA[W[i + 1]] >= 1.15 * c0:
            knee = i; break
    kn = dict(c0=c0, c_A_by_W_target=cA, ratio_to_c0={w: cA[w] / c0 for w in W}, W_real=WREAL)
    if knee is None:
        kn.update(knee_found=False, gate_H0_K="FAIL_no_persistent_transition" )
    else:
        wj, wj1 = WREAL[W[knee]], WREAL[W[knee - 1]] if knee > 0 else None
        kn.update(knee_found=True, W_j_real=wj, W_j_minus_1_real=wj1,
                  C_L2_knee=[wj1, wj], B_L2_operational=int(math.floor(0.70 * wj1 / 18) * 18) if wj1 else None, gate_H0_K="PASS")
    out["H0_K"] = kn
    # descriptives
    desc = {}
    for K, m, cells in [(1, 32, "09_P8_FLOPS_K1_m32_W294912"), (2, 16, "09_P8_FLOPS_K2_m16_W294912"), (4, 8, "09_P8_FLOPS_K4_m8_W294912"), (8, 4, "09_P8_FLOPS_K8_m4_W294912"), (16, 2, "09_P8_FLOPS_K16_m2_W294912"),
                        (2, 32, "09_P8_FLOPS_K2_m32_W294912"), (4, 16, "09_P8_FLOPS_K4_m16_W294912"), (8, 8, "09_P8_FLOPS_K8_m8_W294912"), (16, 4, "09_P8_FLOPS_K16_m4_W294912")]:
        pass
    flops = {}
    for p in sorted(RAW.glob(f"{SEG}_09_*A_*.summary.csv")):
        base = p.name[len(SEG) + 1:-len(".summary.csv")]
        tag = base.rsplit("_A_", 1)[0]
        a = base
        # find matching B
        stem = tag
        b = next(q.name[len(SEG) + 1:-len(".summary.csv")] for q in RAW.glob(f"{SEG}_{stem}_B_*.summary.csv"))
        flops[stem] = dict(c_A=c(a), c_B=c(b), c_A_over_c_B=c(a) / c(b))
    out["FLOPs_constant_descriptive"] = flops
    def abc(prefix):
        cells = {v: next(RAW.glob(f"{SEG}_{prefix}_{v}_*.summary.csv")).name[len(SEG) + 1:-len(".summary.csv")] for v in "ABC"}
        return ratios(cells["A"], cells["B"], cells["C"])
    out["4P_descriptive"] = abc("10_4P_K16_m1_W589824")
    out["PplusE_descriptive"] = abc("13_PplusE_K16_m1_W147456")
    out["Ecluster_arch_descriptive"] = {e: abc(f"12_{e}_arch_K16_m1_W589824") for e in ("Ecluster0", "Ecluster1")}
    out["Ecluster_A_sweep_descriptive"] = {e: {w: c(f"12_{e}_A_K16_m1_W{w}_A_1") for w in (262152, 524286, 786438, 1048572, 1310724)} for e in ("Ecluster0", "Ecluster1")}
    out["SMT_descriptive"] = dict(c_A_SMT=c("11_SMT_16LP_K16_m1_W294912_A_A_1"), c_A_8P_K16_m1_W294912=c("05_P8_W294912_K16_m1_A_1"))
    # integrity
    bad = []
    for p in sorted(RAW.glob("*.status.csv")):
        rows = list(csv.DictReader(p.open(newline="", encoding="utf-8")))
        last = rows[-1]
        if last["status"] not in ("SERIES_COMPLETE", "SERIES_RECOVERED_TECHNICAL_RETRY") or last["has_median"] != "true" or int(last["N"]) != 31 or int(last["warmup_count"]) != 5:
            bad.append(p.name)
    retries = [p.name for p in RAW.glob("*.status.csv") if len(list(csv.DictReader(p.open(newline="", encoding="utf-8")))) > 1]
    out["integrity"] = dict(status_files=len(list(RAW.glob("*.status.csv"))), bad_status=bad, series_with_technical_retry=retries)
    # mechanical proposal
    g = [out["measurement"]["R_noise_pass"], out["measurement"]["eviction_pass"]]
    meas = all(g)
    gates = dict(H0_R1=R1["gate_rho_le_2_3"] and R1["gate_delta_BC_le_0.15"], H0_R4=R4["gate_rho_le_0.90"] and R4["gate_delta_BC_le_0.15"],
                 H0_M=out["H0_M"]["gate_S4_ge_1_5"], H0_K=(kn["gate_H0_K"] == "PASS"))
    control_invalid = (R1["delta_BC"] > 0.15) or (R4["delta_BC"] > 0.15)
    out["gates_summary"] = dict(measurement_valid=meas, control_invalid_delta_BC_gt_0_15=control_invalid, **gates)
    out["mechanical_proposal"] = ("H0_INVALID" if (not meas or control_invalid) else ("H0_PASS" if all(gates.values()) else "H0_FAIL"))
    (OUT / f"h0_derived_{SEG}.json").write_text(json.dumps(out, indent=2), encoding="utf-8")
    print(json.dumps(out["gates_summary"], indent=2)); print("proposal", out["mechanical_proposal"])


if __name__ == "__main__":
    main()
