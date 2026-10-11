"""Synthetic-only smoke test for offline Phase 1 derivations; never reads official raw data."""
from __future__ import annotations

import csv
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

T0=Path(__file__).resolve().parent.parent
SUMMARIZER=T0/"src"/"summarize_phase1.py"


def write_csv(path:Path,fields:list[str],rows:list[dict])->None:
    with path.open("w",newline="",encoding="utf-8") as f:
        writer=csv.DictWriter(f,fieldnames=fields);writer.writeheader();writer.writerows(rows)


with tempfile.TemporaryDirectory(prefix="phase1_synthetic_summary_") as temp:
    results=Path(temp)/"results"/"T0.0"
    raw=results/"phase1_raw"
    raw.mkdir(parents=True)

    instr_fields=["worker_key","worker_type","route","metric","median_conventional_MAC_per_second","iqr","mad","min","max","N","warmup_count","outlier_discard_count"]
    instr_rows=[]
    core_specs=[(f"CORE_API_{i:02d}_G0_MASK_{2*i}_{2*i+1}","P-core",1000.0+i*10) for i in range(8)]
    core_specs.extend((f"CORE_API_{8+i:02d}_G0_MASK_{16+i}","E-core",400.0+i*10) for i in range(8))
    for key,kind,base in core_specs:
        for route,mult in (("AVX2",1.0),("AVX-VNNI-256",2.0)):
            value=base*mult
            instr_rows.append(dict(worker_key=key,worker_type=kind,route=route,metric="conventional_MAC_per_second",median_conventional_MAC_per_second=value,
                                   iqr=10,mad=3,min=value-5,max=value+5,N=31,warmup_count=5,outlier_discard_count=0))
    write_csv(raw/"p_instr_summary.csv",instr_fields,instr_rows)

    q4_fields=["worker_key","worker_type","route","m","m_tile","kernel_weight_payload_traversals_per_call","metric","median_conventional_MAC_per_second","iqr","mad","min","max","median_BW_Q4_logical_B_per_s","N","warmup_count","outlier_discard_count","canonical_weight_payload_bytes_per_call","kernel_weight_payload_load_bytes_logical","weight_physical_allocated_bytes","activation_q_allocated_bytes","activation_scale_allocated_bytes","output_allocated_bytes","extra_kernel_persistent_bytes","resident_buffers_bytes","fixed_units","series_attempt"]
    q4_rows=[]
    values={"AVX2":[100,200,300,250,280,220],"AVX-VNNI-256":[200,400,600,500,560,440]}
    route_ids={"AVX2":"ISA_BASE_AVX2","AVX-VNNI-256":"ISA_OPT_AVX_VNNI_256"}
    for route,rate_list in values.items():
        for m,rate in zip((1,2,4,8,16,32),rate_list):
            tile=1 if m==1 else (2 if m==2 else 4)
            traversals=m//tile
            q4_rows.append(dict(worker_key="CORE_API_00_G0_MASK_0_1",worker_type="P-core",route=route_ids[route],m=m,m_tile=tile,
                                kernel_weight_payload_traversals_per_call=traversals,metric="conventional_MAC_per_second",
                                median_conventional_MAC_per_second=rate,iqr=10,mad=3,min=rate-5,max=rate+5,
                                median_BW_Q4_logical_B_per_s=rate*0.5625,N=31,warmup_count=5,outlier_discard_count=0,
                                canonical_weight_payload_bytes_per_call=1000,kernel_weight_payload_load_bytes_logical=1000*traversals,
                                weight_physical_allocated_bytes=1100,activation_q_allocated_bytes=m*512,activation_scale_allocated_bytes=m*64,
                                output_allocated_bytes=m*512*4,extra_kernel_persistent_bytes=0,resident_buffers_bytes=100000,fixed_units=50,series_attempt=1))
    write_csv(raw/"p_q4_m_summary.csv",q4_fields,q4_rows)

    team_fields=["worker_set","route","metric","median","iqr","mad","min","max","N","warmup_count","outlier_discard_count","fixed_units","series_attempt"]
    team_rows=[]
    for route,bw in (("ISA_BASE_AVX2",100.0),("ISA_OPT_AVX_VNNI_256",120.0)):
        team_rows.append(dict(worker_set="8P",route=route,metric="P_Q4_8P_conventional_MAC_per_second",median=bw/0.5625,iqr=1,mad=1,min=bw/0.5625-1,max=bw/0.5625+1,N=31,warmup_count=5,outlier_discard_count=0,fixed_units=10,series_attempt=1))
        team_rows.append(dict(worker_set="8P",route=route,metric="BW_Q4_logical_8P_B_per_s",median=bw,iqr=1,mad=1,min=bw-1,max=bw+1,N=31,warmup_count=5,outlier_discard_count=0,fixed_units=10,series_attempt=1))
    write_csv(raw/"p_q4_8p_m1_summary.csv",team_fields,team_rows)
    dram_fields=["worker_set","metric","median","iqr","mad","min","max","N","warmup_count","outlier_discard_count","working_set_bytes_per_worker","fixed_units","series_attempt"]
    write_csv(raw/"bw_dram_read_8p_summary.csv",dram_fields,[dict(worker_set="8P",metric="BW_DRAM_read_8P_B_per_s",median=100,iqr=1,mad=1,min=99,max=101,N=31,warmup_count=5,outlier_discard_count=0,working_set_bytes_per_worker=125829120,fixed_units=2,series_attempt=1)])

    e_fields=["order_index","worker_key","worker_type","route","m","metric","median","iqr","mad","min","max","median_BW_Q4_logical_B_per_s","N","warmup_count","outlier_discard_count","fixed_units","series_attempt"]
    e_rows=[]
    for i in range(8):
        for route,mult in (("AVX2",1.0),("AVX-VNNI-256",1.5)):
            value=(10+i)*mult
            e_rows.append(dict(order_index=i,worker_key=f"CORE_API_{8+i:02d}_G0_MASK_{16+i}",worker_type="E-core",route=route,m=1,metric="conventional_MAC_per_second",
                               median=value,iqr=1,mad=1,min=value-0.5,max=value+0.5,median_BW_Q4_logical_B_per_s=value*0.5625,
                               N=31,warmup_count=5,outlier_discard_count=0,fixed_units=20,series_attempt=1))
    write_csv(raw/"e_confirm_q4_m1_summary.csv",e_fields,e_rows)

    pcore_fields=["order_index","worker_key","worker_type","route","m","m_tile","kernel_weight_payload_traversals_per_call","metric",
                  "median_conventional_MAC_per_second","iqr","mad","min","max","median_BW_Q4_logical_B_per_s","N","warmup_count",
                  "outlier_discard_count","canonical_weight_payload_bytes_per_call","kernel_weight_payload_load_bytes_logical","weight_physical_allocated_bytes","fixed_units","series_attempt"]
    pcore_rows=[]
    for i in range(8):
        for route,mult in (("AVX2",1.0),("AVX-VNNI-256",1.1)):
            value=(100+i*5)*mult
            pcore_rows.append(dict(order_index=i,worker_key=f"CORE_API_{i:02d}_G0_MASK_{2*i}_{2*i+1}",worker_type="P-core",route=route,m=1,m_tile=1,
                                   kernel_weight_payload_traversals_per_call=1,metric="conventional_MAC_per_second",median_conventional_MAC_per_second=value,
                                   iqr=2,mad=1,min=value-1,max=value+1,median_BW_Q4_logical_B_per_s=value*0.5625,N=31,warmup_count=5,
                                   outlier_discard_count=0,canonical_weight_payload_bytes_per_call=1000,kernel_weight_payload_load_bytes_logical=1000,
                                   weight_physical_allocated_bytes=1100,fixed_units=20,series_attempt=1))
    write_csv(raw/"p_q4_pcore_m1_summary.csv",pcore_fields,pcore_rows)
    (raw/"effective_frequency_attempt.json").write_text(json.dumps({"attempt_count":1,"status":"UNAVAILABLE_WITH_QUALIFIED_TOOLS"}),encoding="utf-8")
    (raw/"series_status.csv").write_text("series,status\n",encoding="utf-8")

    env=os.environ.copy();env["T0_PHASE1_RESULTS_DIR"]=str(results)
    run=subprocess.run([sys.executable,str(SUMMARIZER)],env=env,capture_output=True,text=True,check=False)
    if run.returncode!=0:raise RuntimeError(f"synthetic summarizer failed: {run.stderr}")
    readiness=json.loads((results/"phase1_readiness.json").read_text(encoding="utf-8"))
    assert readiness["gate_pass"] is True
    assert abs(readiness["D_read_gate_max"]-1.2)<1e-12
    with (results/"phase1_P_Q4_by_core_m.csv").open(newline="",encoding="utf-8") as f:
        q4=list(csv.DictReader(f))
    avx_m4=next(r for r in q4 if r["route"]=="AVX2" and int(r["m"])==4)
    assert abs(float(avx_m4["eta_instr"])-0.3)<1e-12
    assert abs(float(avx_m4["P_Q4_peak_core_conventional_MAC_per_s"])-300.0)<1e-12
    assert abs(float(avx_m4["eta_Q4"])-1.0)<1e-12
    with (results/"phase1_P_Q4_peak_by_core.csv").open(newline="",encoding="utf-8") as f:
        peak=list(csv.DictReader(f))
    assert next(r for r in peak if r["route"]=="AVX2")["m_at_peak"]=="4"
    with (results/"phase1_E_Q4_m1_by_core.csv").open(newline="",encoding="utf-8") as f:
        ecore=list(csv.DictReader(f))
    assert len(ecore)==16
    with (results/"phase1_E_core_P_instr_vs_Q4.csv").open(newline="",encoding="utf-8") as f:
        paired=list(csv.DictReader(f))
    assert len(paired)==16
    assert abs(float(paired[0]["eta_instr_m1"])-float(paired[0]["P_Q4_m1_conventional_MAC_per_s"])/float(paired[0]["P_instr_conventional_MAC_per_s"]))<1e-12
    with (results/"phase1_P_Q4_i_m1_by_core.csv").open(newline="",encoding="utf-8") as f:
        pcore=list(csv.DictReader(f))
    assert len(pcore)==16 and all(abs(float(r["BW_Q4_logical_i_B_per_s"])-float(r["v_i_conventional_MAC_per_s"])*0.5625)<1e-12 for r in pcore)
    with (results/"phase1_T0.0_final_table.csv").open(newline="",encoding="utf-8") as f:
        final=list(csv.DictReader(f))
    assert next(r for r in final if r["field"]=="EFFECTIVE_FREQUENCY")["status"]=="UNAVAILABLE_WITH_QUALIFIED_TOOLS"
    assert (results/"phase1_session_report.md").is_file() and (results/"phase1_T0.1_handoff.md").is_file()
    print(json.dumps({"synthetic_only":True,"timing":False,"derived":run.stdout.strip(),"D_read_max":readiness["D_read_gate_max"],
                      "eta_instr_AVX2_m4":float(avx_m4["eta_instr"]),"P_Q4_peak_AVX2":float(avx_m4["P_Q4_peak_core_conventional_MAC_per_s"]),
                      "E_core_route_rows":len(ecore)}))
