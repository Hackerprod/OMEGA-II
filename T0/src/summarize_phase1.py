"""Derive MAC-conventional Phase 1 tables from raw/summary CSVs; performs no timing."""
from __future__ import annotations

import csv
import json
import math
import os
import statistics
from pathlib import Path

T0=Path(__file__).resolve().parent.parent
RESULTS=Path(os.environ.get("T0_PHASE1_RESULTS_DIR",str(T0/"results"/"T0.0"))).resolve()
RAW=RESULTS/"phase1_raw"


def read_rows(path:Path)->list[dict[str,str]]:
    with path.open(newline="",encoding="utf-8-sig") as f:return list(csv.DictReader(f))


def q(values:list[float],p:float)->float:
    ordered=sorted(values)
    return ordered[math.floor(p*(len(ordered)-1))]


def stats(values:list[float])->dict[str,float]:
    med=statistics.median(values)
    return {"median":med,"iqr":q(values,.75)-q(values,.25),
            "mad":statistics.median([abs(x-med) for x in values]),"min":min(values),"max":max(values)}


def write_csv(path:Path,rows:list[dict],fields:list[str]|None=None)->None:
    if not rows:raise ValueError(f"no rows to write for {path.name}")
    fields=fields or list(rows[0])
    with path.open("w",newline="",encoding="utf-8") as f:
        w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerows(rows)


instr=read_rows(RAW/"p_instr_summary.csv")
q4=read_rows(RAW/"p_q4_m_summary.csv")
route_norm={"ISA_BASE_AVX2":"AVX2","ISA_OPT_AVX_VNNI_256":"AVX-VNNI-256"}
instr_by_key={(r["worker_key"],r["route"]):r for r in instr}
q4_by_key={(r["worker_key"],r["route"],int(r["m"])):r for r in q4}

# P_instr per physical core and per core class.
instr_core=[]
for r in instr:
    instr_core.append({"worker_key":r["worker_key"],"worker_type":r["worker_type"],"route":r["route"],
                       "P_instr_conventional_MAC_per_s":float(r["median_conventional_MAC_per_second"]),
                       "iqr":float(r["iqr"]),"mad":float(r["mad"]),"min":float(r["min"]),"max":float(r["max"]),
                       "N":int(r["N"]),"warmup_count":int(r["warmup_count"]),"outlier_discard_count":int(r["outlier_discard_count"])})
write_csv(RESULTS/"phase1_P_instr_by_core.csv",instr_core)
instr_type=[]
for kind in ("P-core","E-core"):
    for route in ("AVX2","AVX-VNNI-256"):
        values=[r["P_instr_conventional_MAC_per_s"] for r in instr_core if r["worker_type"]==kind and r["route"]==route]
        s=stats(values)
        instr_type.append({"worker_type":kind,"route":route,"physical_core_count":len(values),
                           "P_instr_median_of_core_medians_conventional_MAC_per_s":s["median"],
                           "iqr_of_core_medians":s["iqr"],"mad_of_core_medians":s["mad"],
                           "min_core_median":s["min"],"max_core_median":s["max"],"N_per_core":31,"warmup_count":5})
write_csv(RESULTS/"phase1_P_instr_by_type.csv",instr_type)
instr_type_lookup={(r["worker_type"],r["route"]):r for r in instr_type}

# P_Q4(r,P_ref,m), eta_instr and eta_Q4 for the deterministically selected P_ref.
q4_core=[]
for r in q4:
    route=route_norm.get(r["route"],r["route"])
    key=(r["worker_key"],route,int(r["m"]))
    peak=max(float(x["median_conventional_MAC_per_second"]) for x in q4 if x["worker_key"]==r["worker_key"] and x["route"]==r["route"])
    p_instr=float(instr_by_key[(r["worker_key"],route)]["median_conventional_MAC_per_second"])
    p_q4=float(r["median_conventional_MAC_per_second"])
    q4_core.append({"worker_key":r["worker_key"],"worker_type":r["worker_type"],"route":route,"m":key[2],
                    "P_Q4_conventional_MAC_per_s":p_q4,"iqr":float(r["iqr"]),"mad":float(r["mad"]),
                    "min":float(r["min"]),"max":float(r["max"]),"N":int(r["N"]),
                    "P_instr_conventional_MAC_per_s":p_instr,"eta_instr":p_q4/p_instr,
                    "P_Q4_peak_core_conventional_MAC_per_s":peak,"eta_Q4":p_q4/peak,
                    "BW_Q4_logical_B_per_s":p_q4*0.5625,
                    "m_tile":int(r["m_tile"]),"kernel_weight_payload_traversals_per_call":int(r["kernel_weight_payload_traversals_per_call"]),
                    "canonical_weight_payload_bytes_per_call":int(r["canonical_weight_payload_bytes_per_call"]),
                    "kernel_weight_payload_load_bytes_logical":int(r["kernel_weight_payload_load_bytes_logical"]),
                    "weight_physical_allocated_bytes":int(r["weight_physical_allocated_bytes"])})
write_csv(RESULTS/"phase1_P_Q4_by_core_m.csv",q4_core)

# The single-core Q4 sweep is intentionally P_ref only; the E-core confirmation is derived separately below.
q4_type=[]
for kind in sorted({r["worker_type"] for r in q4_core}):
    for route in ("AVX2","AVX-VNNI-256"):
        for m in (1,2,4,8,16,32):
            rows=[r for r in q4_core if r["worker_type"]==kind and r["route"]==route and r["m"]==m]
            values=[r["P_Q4_conventional_MAC_per_s"] for r in rows]
            s=stats(values)
            p_instr=float(rows[0]["P_instr_conventional_MAC_per_s"])
            q4_type.append({"worker_type":kind,"route":route,"m":m,"physical_core_count":len(values),
                            "P_Q4_median_of_core_medians_conventional_MAC_per_s":s["median"],
                            "iqr_of_core_medians":s["iqr"],"mad_of_core_medians":s["mad"],
                            "min_core_median":s["min"],"max_core_median":s["max"],
                            "P_instr_median_of_core_medians_conventional_MAC_per_s":p_instr,
                            "eta_instr":s["median"]/p_instr,"BW_Q4_logical_B_per_s":s["median"]*0.5625,
                            "N_per_core":31,"warmup_count":5})
write_csv(RESULTS/"phase1_P_Q4_by_type_m.csv",q4_type)

peak_core=[]
for kind in sorted({r["worker_type"] for r in q4_core}):
    for route in ("AVX2","AVX-VNNI-256"):
        for key in sorted({r["worker_key"] for r in q4_core if r["worker_type"]==kind and r["route"]==route}):
            group=[r for r in q4_core if r["worker_key"]==key and r["route"]==route]
            best=max(group,key=lambda r:r["P_Q4_conventional_MAC_per_s"])
            peak_core.append({"worker_key":key,"worker_type":kind,"route":route,"m_at_peak":best["m"],
                              "P_Q4_peak_conventional_MAC_per_s":best["P_Q4_conventional_MAC_per_s"],
                              "iqr_at_peak":best["iqr"],"mad_at_peak":best["mad"],"N":best["N"]})
write_csv(RESULTS/"phase1_P_Q4_peak_by_core.csv",peak_core)
peak_type=[]
for kind in sorted({r["worker_type"] for r in q4_type}):
    for route in ("AVX2","AVX-VNNI-256"):
        group=[r for r in q4_type if r["worker_type"]==kind and r["route"]==route]
        best=max(group,key=lambda r:r["P_Q4_median_of_core_medians_conventional_MAC_per_s"])
        peak_type.append({"worker_type":kind,"route":route,"m_at_peak":best["m"],
                          "P_Q4_peak_median_of_core_medians_conventional_MAC_per_s":best["P_Q4_median_of_core_medians_conventional_MAC_per_s"],
                          "iqr_of_core_medians_at_peak":best["iqr_of_core_medians"],"mad_of_core_medians_at_peak":best["mad_of_core_medians"]})
write_csv(RESULTS/"phase1_P_Q4_peak_by_type.csv",peak_type)

# D_read uses the Q4 logical-weight rate for the measured 8P m=1 group divided by the read-only DRAM stream.
q4_8p=read_rows(RAW/"p_q4_8p_m1_summary.csv")
dram=read_rows(RAW/"bw_dram_read_8p_summary.csv")
dram_row=next(r for r in dram if r["metric"]=="BW_DRAM_read_8P_B_per_s")
dram_rate=float(dram_row["median"])
dread_rows=[]
for r in q4_8p:
    if r["metric"]!="BW_Q4_logical_8P_B_per_s":continue
    bw=float(r["median"])
    dread_rows.append({"route":route_norm.get(r["route"],r["route"]),"BW_Q4_logical_8P_B_per_s":bw,
                       "BW_DRAM_read_8P_B_per_s":dram_rate,"D_read":bw/dram_rate,
                       "D_read_gate_pass":bw/dram_rate>=1.0,"N":int(r["N"]),"warmup_count":int(r["warmup_count"])})
write_csv(RESULTS/"phase1_D_read.csv",dread_rows)

# E-core Q4 confirmation remains per-core and is also summarized descriptively by core class.
e_confirm=read_rows(RAW/"e_confirm_q4_m1_summary.csv")
e_core=[]
for r in e_confirm:
    if r["metric"]!="conventional_MAC_per_second":continue
    route=route_norm.get(r["route"],r["route"])
    e_core.append({"order_index":int(r["order_index"]),"worker_key":r["worker_key"],"worker_type":r["worker_type"],"route":route,
                   "m":int(r["m"]),"P_Q4_conventional_MAC_per_s":float(r["median"]),"iqr":float(r["iqr"]),
                   "mad":float(r["mad"]),"min":float(r["min"]),"max":float(r["max"]),"N":int(r["N"]),
                   "fixed_units":int(r["fixed_units"]),"series_attempt":int(r["series_attempt"])})
write_csv(RESULTS/"phase1_E_Q4_m1_by_core.csv",e_core)
e_instr_lookup={(r["worker_key"],r["route"]):r for r in instr_core if r["worker_type"]=="E-core"}
e_comparison=[]
for r in e_core:
    instr_row=e_instr_lookup[(r["worker_key"],r["route"])]
    p_instr=float(instr_row["P_instr_conventional_MAC_per_s"])
    e_comparison.append({"order_index":r["order_index"],"worker_key":r["worker_key"],"route":r["route"],
                         "P_instr_conventional_MAC_per_s":p_instr,"P_Q4_m1_conventional_MAC_per_s":r["P_Q4_conventional_MAC_per_s"],
                         "eta_instr_m1":r["P_Q4_conventional_MAC_per_s"]/p_instr,
                         "P_instr_iqr":instr_row["iqr"],"P_Q4_iqr":r["iqr"]})
write_csv(RESULTS/"phase1_E_core_P_instr_vs_Q4.csv",e_comparison)
e_type=[]
for route in ("AVX2","AVX-VNNI-256"):
    values=[r["P_Q4_conventional_MAC_per_s"] for r in e_core if r["route"]==route]
    s=stats(values)
    e_type.append({"worker_type":"E-core","route":route,"physical_core_count":len(values),
                   "P_Q4_m1_median_of_core_medians_conventional_MAC_per_s":s["median"],
                   "iqr_of_core_medians":s["iqr"],"mad_of_core_medians":s["mad"],
                   "min_core_median":s["min"],"max_core_median":s["max"],"N_per_core":31,"warmup_count":5})
write_csv(RESULTS/"phase1_E_Q4_m1_by_type.csv",e_type)

readiness={"D_read_by_route":dread_rows,"D_read_gate_max":max((r["D_read"] for r in dread_rows),default=None),
           "gate_pass":bool(dread_rows) and max(r["D_read"] for r in dread_rows)>=1.0,"derived_after_all_series":True}
(RESULTS/"phase1_readiness.json").write_text(json.dumps(readiness,indent=2)+"\n",encoding="utf-8")

# Final MD/28 per-physical-P-core m=1 values; BW is logical Q4 weight demand, not physical L2 traffic.
pcore_summary=read_rows(RAW/"p_q4_pcore_m1_summary.csv")
pcore_rows=[]
for r in pcore_summary:
    if r["metric"]!="conventional_MAC_per_second":continue
    route=route_norm.get(r["route"],r["route"])
    p_instr=float(instr_by_key[(r["worker_key"],route)]["median_conventional_MAC_per_second"])
    p_q4=float(r["median_conventional_MAC_per_second"])
    core_peak_values=[float(x["median_conventional_MAC_per_second"]) for x in q4 if x["worker_key"]==r["worker_key"] and x["route"]==r["route"]]
    peak=max(core_peak_values) if core_peak_values else None
    pcore_rows.append({"order_index":int(r["order_index"]),"worker_key":r["worker_key"],"worker_type":r["worker_type"],"route":route,
                       "m":1,"v_i_conventional_MAC_per_s":p_q4,"P_Q4_i_conventional_MAC_per_s":p_q4,"BW_Q4_logical_i_B_per_s":p_q4*0.5625,
                       "Q4_WEIGHT_DEMAND_RATE_i_B_per_s":p_q4*0.5625,"P_instr_i_conventional_MAC_per_s":p_instr,
                       "eta_instr":p_q4/p_instr,"P_Q4_peak_core_conventional_MAC_per_s":peak,"eta_Q4":p_q4/peak if peak else None,
                       "iqr":float(r["iqr"]),"mad":float(r["mad"]),"min":float(r["min"]),"max":float(r["max"]),
                       "N":int(r["N"]),"warmup_count":int(r["warmup_count"]),"fixed_units":int(r["fixed_units"]),"series_attempt":int(r["series_attempt"])})
if len(pcore_rows)!=16:raise ValueError(f"expected 16 completed P-core m1 summary rows; found {len(pcore_rows)}")
write_csv(RESULTS/"phase1_P_Q4_i_m1_by_core.csv",pcore_rows)

# Keep P_ref's place within the P-core distribution descriptive; no post-hoc outlier gate is introduced.
p_ref_key="CORE_API_00_G0_MASK_0_1"
p_ref_comparison=[]
for route in ("AVX2","AVX-VNNI-256"):
    values=[r["P_Q4_i_conventional_MAC_per_s"] for r in pcore_rows if r["route"]==route]
    s=stats(values)
    ref=next(r for r in pcore_rows if r["worker_key"]==p_ref_key and r["route"]==route)
    p_ref_comparison.append({"worker_key":p_ref_key,"route":route,"P_ref_P_Q4_i_m1_conventional_MAC_per_s":ref["P_Q4_i_conventional_MAC_per_s"],
                             "P_core_median_of_core_medians":s["median"],"P_core_IQR_of_core_medians":s["iqr"],
                             "P_core_MAD_of_core_medians":s["mad"],"P_ref_minus_P_core_median":ref["P_Q4_i_conventional_MAC_per_s"]-s["median"],
                             "P_ref_within_observed_min_max":min(values)<=ref["P_Q4_i_conventional_MAC_per_s"]<=max(values),
                             "outlier_gate":"NONE; descriptive only"})
write_csv(RESULTS/"phase1_P_ref_vs_P_core_m1.csv",p_ref_comparison)

# Final workload and memory metrics; team throughput remains a team-wall measurement.
team_rows=[]
for r in q4_8p:
    if r["metric"] not in ("P_Q4_8P_conventional_MAC_per_second","BW_Q4_logical_8P_B_per_s"):continue
    route=route_norm.get(r["route"],r["route"])
    team_rows.append({"workload":"B_8P","route":route,"metric":r["metric"],"median":float(r["median"]),"N":int(r["N"]),"warmup_count":int(r["warmup_count"]),
                      "definition":"simultaneous 8P workers divided by common team wall interval" if r["metric"]=="P_Q4_8P_conventional_MAC_per_second" else "logical Q4 payload traversal rate; not physical cache traffic"})
write_csv(RESULTS/"phase1_TEAM_MAC_RATE_8P.csv",team_rows)
dram_row=next(r for r in dram if r["metric"]=="BW_DRAM_read_8P_B_per_s")
write_csv(RESULTS/"phase1_BW_DRAM_read_8P.csv",[{"metric":"BW_DRAM_read_8P","median_B_per_s":float(dram_row["median"]),
          "N":int(dram_row["N"]),"warmup_count":int(dram_row["warmup_count"]),"access":"streaming load-only read"}])
write_csv(RESULTS/"phase1_ABC_status.csv",[
    {"quantity":"A_4P","status":"DEFERRED_TO_T0_1","value":"","basis":"not measured or derived from single-core rates"},
    {"quantity":"B_8P","status":"MEASURED_T0_0","value":"phase1_TEAM_MAC_RATE_8P.csv","basis":"P_Q4_8P_m1 simultaneous team measurement"},
    {"quantity":"C_8P_plus_8E","status":"DEFERRED_TO_T0_1","value":"","basis":"not measured or derived from single-core rates"},
    {"quantity":"r_A_r_B_r_C","status":"LEGACY_PREOPT_DESCRIPTIVE","value":"","basis":"not regenerated in T0.0"}])

frequency_path=RAW/"effective_frequency_attempt.json"
if not frequency_path.is_file():raise ValueError("effective-frequency attempt result is missing")
frequency=json.loads(frequency_path.read_text(encoding="utf-8"))
final_table=[
    {"field":"P_Q4_i_m1","status":"MEASURED","value":"phase1_P_Q4_i_m1_by_core.csv","unit":"conventional_MAC/s"},
    {"field":"BW_Q4_logical_i","status":"MEASURED","value":"phase1_P_Q4_i_m1_by_core.csv","unit":"B/s (logical payload demand; 0.5625 B/MAC)"},
    {"field":"P_instr_i","status":"MEASURED","value":"phase1_P_instr_by_core.csv","unit":"conventional_MAC/s"},
    {"field":"P_Q4(m), P_Q4_peak, eta_instr, eta_Q4","status":"MEASURED","value":"phase1_P_Q4_by_core_m.csv; phase1_P_Q4_peak_by_core.csv","unit":"conventional_MAC/s; dimensionless efficiencies"},
    {"field":"TEAM_MAC_RATE_8P","status":"MEASURED","value":"phase1_TEAM_MAC_RATE_8P.csv","unit":"conventional_MAC/s"},
    {"field":"BW_DRAM_read_8P","status":"MEASURED","value":"phase1_BW_DRAM_read_8P.csv","unit":"B/s"},
    {"field":"D_read","status":"MEASURED_GATE_PASS" if readiness["gate_pass"] else "MEASURED_GATE_FAIL","value":"phase1_D_read.csv","unit":"dimensionless"},
    {"field":"E_core_confirmation","status":"MEASURED","value":"phase1_E_Q4_m1_by_core.csv; phase1_E_core_P_instr_vs_Q4.csv","unit":"conventional_MAC/s"},
    {"field":"A_4P / C_8P_plus_8E","status":"DEFERRED_TO_T0_1","value":"phase1_ABC_status.csv","unit":"not applicable"},
    {"field":"r_A / r_B / r_C","status":"LEGACY_PREOPT_DESCRIPTIVE","value":"phase1_ABC_status.csv","unit":"not applicable"},
    {"field":"EFFECTIVE_FREQUENCY","status":frequency["status"],"value":"results/T0.0/phase1_raw/effective_frequency_attempt.json","unit":"GHz if measured; no inferred estimate"},
]
write_csv(RESULTS/"phase1_T0.0_final_table.csv",final_table)

# Final closure report and T0.1 handoff are generated only after the runner has the new series and the bounded frequency outcome.
status_rows=read_rows(RAW/"series_status.csv")
series_status={}
for row in status_rows:series_status.setdefault(row["series"],[]).append(row["status"])
completed_count=sum(v[-1] in ("SERIES_COMPLETE","SERIES_RECOVERED_TECHNICAL_RETRY") for v in series_status.values())
invalid_count=sum(any("INVALID" in value or value.endswith("HOLD") for value in v) for v in series_status.values())
retry_count=sum(len(v)>1 for v in series_status.values())
peak_lines=[f"| {r['route']} | {float(r['P_Q4_peak_median_of_core_medians_conventional_MAC_per_s']):.6f} | {int(r['m_at_peak'])} |" for r in peak_type if r["worker_type"]=="P-core"]
dread_lines=[f"| {r['route']} | {float(r['BW_Q4_logical_8P_B_per_s']):.6f} | {float(r['BW_DRAM_read_8P_B_per_s']):.6f} | {float(r['D_read']):.6f} |" for r in dread_rows]
pcore_lines=[f"| {r['worker_key']} | {r['route']} | {r['P_Q4_i_conventional_MAC_per_s']:.6f} | {r['BW_Q4_logical_i_B_per_s']:.6f} | {r['P_instr_i_conventional_MAC_per_s']:.6f} | {r['eta_instr']:.6f} |" for r in pcore_rows]
frequency_lines=[]
for target in frequency.get("targets",[]):
    value=target.get("effective_frequency_ratio",target.get("effective_frequency_GHz",""))
    unit=target.get("frequency_unit","GHz" if target.get("effective_frequency_GHz") is not None else "")
    frequency_lines.append(f"| {target.get('label','')} | {target.get('worker_key','')} | {target.get('status','')} | {value} {unit} | {target.get('source_event','')} | {target.get('formula',target.get('reason',''))} |")
report=(RESULTS/"phase1_session_report.md")
old_report=report.read_text(encoding="utf-8") if report.is_file() else ""
pending_note="> This report's result tables currently document the accepted 63-series MD/26–27 session. MD/28 closure is still pending: 16 P-core Q4 m=1 series and one bounded SEP/VTune effective-frequency attempt have not been run. See `phase1_session_launch.md`; do not treat this report as final T0.0 closure yet."
completed_note=f"> MD/28 closure completed from the accepted 63-series session plus {completed_count-63} additional P-core series; no previous series were rerun."
if pending_note in old_report:old_report=old_report.replace(pending_note,completed_note)
if "## MD/28 final closure" in old_report:old_report=old_report.split("## MD/28 final closure",1)[0]
closure=f"""

## MD/28 final closure

- Final series count: {completed_count} completed; {invalid_count} invalid/held series; {retry_count} series with a technical retry record.
- `UNAUTHORIZED_PREFLIGHT_GATE=REMOVED_PRE_DATA`.
- `SESSION_SEGMENT_1=series 1..47`; `SESSION_SEGMENT_2=16 E-core Q4 m=1 series`; `segment_boundary_reason=PARSER_FIX_PRE_E_DATA`.
- The 16 required P-core Q4 m=1 series are complete. P_ref comparison is descriptive and introduces no post-hoc outlier gate.
- `A_4P=DEFERRED_TO_T0_1`; `B_8P=MEASURED_T0_0`; `C_8P_PLUS_8E=DEFERRED_TO_T0_1`; `r_A/r_B/r_C=LEGACY_PREOPT_DESCRIPTIVE`.
- `EFFECTIVE_FREQUENCY={frequency['status']}`; attempt count={frequency.get('attempt_count',0)}. No throughput- or PDH-derived frequency is used.

### P-core individual m=1

| Physical core | ISA route | v_i = P_Q4_i (conventional MAC/s) | BW_Q4_logical,i (B/s) | P_instr_i (conventional MAC/s) | eta_instr |
|---|---|---:|---:|---:|---:|
{chr(10).join(pcore_lines)}

### P_ref Q4 peak and D_read

| ISA route | P_Q4_peak (conventional MAC/s) | m at peak |
|---|---:|---:|
{chr(10).join(peak_lines)}

| ISA route | BW_Q4_logical_8P (B/s) | BW_DRAM_read_8P (B/s) | D_read |
|---|---:|---:|---:|
{chr(10).join(dread_lines)}

### Effective-frequency attempt

| Target | Core | Status | Measured value | Source | Formula or outcome |
|---|---|---|---|---|---|
{chr(10).join(frequency_lines) if frequency_lines else '| P_ref and representative E-core | — | UNAVAILABLE_WITH_QUALIFIED_TOOLS | — | SEP/VTune | No unambiguous qualified counter result |'}

Full derived tables: `phase1_T0.0_final_table.csv`, `phase1_P_Q4_i_m1_by_core.csv`, `phase1_P_ref_vs_P_core_m1.csv`, `phase1_TEAM_MAC_RATE_8P.csv`, `phase1_BW_DRAM_read_8P.csv`, `phase1_D_read.csv`, and the existing P_instr/P_Q4/E-core CSVs. T0.1 must await JUEZ verification and Opus inspection.
"""
report.write_text(old_report.rstrip()+"\n"+closure,encoding="utf-8")

handoff=f"""# T0.0 → T0.1 measured handoff

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
"""
(RESULTS/"phase1_T0.1_handoff.md").write_text(handoff,encoding="utf-8")

print(json.dumps({"P_instr_core_rows":len(instr_core),"P_Q4_core_m_rows":len(q4_core),
                  "P_Q4_peak_core_rows":len(peak_core),"D_read_routes":len(dread_rows),"E_Q4_core_rows":len(e_core),
                  "P_Q4_i_m1_rows":len(pcore_rows),"frequency_status":frequency["status"],"timing":False}))
