#!/usr/bin/env python3
"""S-14 machine gate. Numerical, dispatch and page policy are independent states."""
import json, re, sys
from pathlib import Path

MS=(1,4,8,16)
FIXTURES=("uniform","mixed_cancellation","varied_scales","sub127","extremes","zero_input","single_coordinate","cancellation_high","cancellation_extreme","cancellation_exact_groups")

def read_log(path):
    raw=Path(path).read_bytes()
    if raw.startswith(b"\xff\xfe"): return raw.decode("utf-16le",errors="replace")
    if raw.startswith(b"\xfe\xff"): return raw.decode("utf-16be",errors="replace")
    if raw.startswith(b"\xef\xbb\xbf"): return raw.decode("utf-8-sig",errors="replace")
    return raw.decode("utf-8",errors="replace")

def expected_page_rows(reference):
    data=json.loads((Path(__file__).with_name("PAGE_EXPECTED_S15.json")).read_text(encoding="utf-8"))
    return data["references"][reference]

def page_policy(text, reference, candidate=None):
    """Require every frozen buffer record, exact logical coverage, and all queried pages."""
    privilege=re.search(r"LARGE_PAGE_PRIVILEGE=(AVAILABLE|HOLD|FAIL)",text)
    if not privilege or privilege.group(1)!="AVAILABLE": return "FAIL"
    expected=expected_page_rows(reference)
    if candidate is not None:
        expected=[r for r in expected if r["candidate"]==candidate]
    if not expected: return "FAIL"
    pattern=(r"PAGE fixture=(\S+) m=(\d+) candidate=(\S+) buffer=(\S+) function=(\S+) alias=(\S+) address=(0x[0-9a-fA-F]+) bytes=(\d+) queried_pages=(\d+) valid_pages=(\d+) large_pages=(\d+) QueryWorkingSetEx=(PASS|FAIL)")
    rows=re.findall(pattern,text)
    wanted={(r["fixture"],str(r["m"]),r["candidate"],r["buffer"],r["function"]):r for r in expected}
    found={}
    for fixture,m,cand,buf,fn,alias,address,size,q,v,l,status in rows:
        key=(fixture,m,cand,buf,fn)
        if cand!=candidate and candidate is not None: continue
        if key in found: return "FAIL"  # duplicates, including contradictory duplicates
        found[key]=(alias,address,int(size),int(q),int(v),int(l),status)
    if set(found)!=set(wanted): return "FAIL"
    alias_addresses={}
    for key,record in found.items():
        alias,address,size,q,v,l,status=record; spec=wanted[key]
        if alias!=spec["alias"] or size!=spec["bytes"] or q<=0 or q!=v or q!=l or status!="PASS": return "FAIL"
        if alias in alias_addresses and alias_addresses[alias]!=(address,size): return "FAIL"
        alias_addresses[alias]=(address,size)
    return "PASS"

def state_line(candidate, numerical, dispatch, pages):
    states=(numerical,dispatch,pages)
    ready="PASS" if all(x=="PASS" for x in states) else "FAIL" if "FAIL" in states else "HOLD"
    print(f"candidate={candidate} QA_NUMERICAL={numerical} DISPATCH_QUALIFIED={dispatch} PAGE_POLICY={pages} REFERENCE_READY={ready}")
    return ready

def gate_ggml(path):
    text=read_log(path); overall=True
    cells={"flat":set(),"repack":set()}
    for line in text.splitlines():
        cm=re.search(r"CELL fixture=(\S+) m=(\d+) route=(flat|repack).*?finite=(\d+) S12_pass=(\d+) S12_failures=(\d+)",line)
        if cm:
            f,m,r,finite,s12,fail=cm.groups(); cells[r].add((f,int(m)))
    sweep={}
    for m,pos,batches,flatfail,repackfail,exact in re.findall(r"SINGLE_SWEEP M=(\d+) positions=(\d+) batches=(\d+) flat_failures=(\d+) flat_max_error_over_B=\S+ repack_failures=(\d+) repack_max_error_over_B=\S+ exact_integer_outputs=(PASS|FAIL)",text):
        sweep.setdefault("flat",{})[int(m)]=(int(pos),int(flatfail),0,"PASS" if int(flatfail)==0 else "FAIL")
        sweep.setdefault("repack",{})[int(m)]=(int(pos),0,int(repackfail),"PASS" if int(repackfail)==0 else "FAIL")
    groups={}
    for r,c,f,s in re.findall(r"EXACT_GROUP_DOT_ISOLATION route=ggml_(flat|repack) checks=(\d+) failures=(\d+) status=(PASS|FAIL)",text): groups[r]=(int(c),int(f),s)
    cancels={}
    for r,m,c,f,s in re.findall(r"EXACT_CANCELLATION route=(flat|repack) m=(\d+) checks=(\d+) failures=(\d+) status=(PASS|FAIL)",text): cancels[(r,int(m))]=(int(c),int(f),s)
    static=re.search(r"STATIC_ROUTE_ISOLATION .*status=(PASS|FAIL)",text)
    dynamic=re.search(r"DYNAMIC_ROUTE_ISOLATION .*status=(PASS|FAIL)",text)
    repr_rows=re.findall(r"control_representation flat_and_effective_repack=(PASS|FAIL)",text)
    all_pages=page_policy(text,"ggml")
    aggregate=[]
    for route in ("flat","repack"):
        expected={(f,m) for f in FIXTURES for m in MS}
        cell_ok=cells[route]==expected
        # Every expected row must independently pass; duplicates are rejected too.
        rawrows=re.findall(rf"CELL fixture=(\S+) m=(\d+) route={route} .*?finite=(\d+) S12_pass=(\d+) S12_failures=(\d+)",text)
        cell_ok &= len(rawrows)==40 and all(a=="1" and b=="1" and c=="0" for _,_,a,b,c in rawrows)
        sw=sweep.get(route,{})
        sweep_raw=re.findall(r"SINGLE_SWEEP M=(\d+) positions=(\d+) batches=(\d+) flat_failures=(\d+) flat_max_error_over_B=\S+ repack_failures=(\d+) repack_max_error_over_B=\S+ exact_integer_outputs=(PASS|FAIL)",text)
        sweep_ok=len(sweep_raw)==4 and {int(x[0]) for x in sweep_raw}==set(MS) and all(int(x[1])==512 and int(x[3])==0 and int(x[4])==0 and x[5]=="PASS" for x in sweep_raw) and set(sw)==set(MS)
        group=groups.get(route); group_ok=group==(2375680,0,"PASS")
        cancel_ok=all(cancels.get((route,m))==(m*1024,0,"PASS") for m in MS) and len([k for k in cancels if k[0]==route])==4
        ctrl_ok=len(re.findall(r"STATIC_ROUTE_ISOLATION .*status=(?:PASS|FAIL)",text))==1 and static is not None and static.group(1)=="PASS" and len(re.findall(r"DYNAMIC_ROUTE_ISOLATION .*status=(?:PASS|FAIL)",text))==1 and dynamic is not None and dynamic.group(1)=="PASS" and len(repr_rows)==40 and all(x=="PASS" for x in repr_rows)
        numerical="PASS" if cell_ok and sweep_ok and group_ok and cancel_ok and ctrl_ok and "MANDATORY_GATE=PASS" in text else "FAIL"
        # Runtime callback is sufficient. Otherwise require explicit effective matrix-route proof per M.
        dispatch_rows={int(m):(rt,ev) for m,rt,ev in re.findall(r"DISPATCH m=(\d+) route=(\S+).*?evidence=(\S+)",text)}
        callback={int(m):v for m,v in re.findall(r"S14_FLAT_CALLBACK m=(\d+) reached=(true|false)",text)}
        if route=="flat":
            dispatch_ok=all(callback.get(m)=="true" or (m in dispatch_rows and dispatch_rows[m][0] in {"llamafile_sgemm","ggml_gemm","ggml_vec_dot_q4_0_q8_0"} and dispatch_rows[m][1]!="unverified") for m in MS)
        else:
            route_lines=re.findall(r"CELL fixture=\S+ m=(\d+) route=repack executed=(ggml_gemv_q4_0_8x8_q8_0|ggml_gemm_q4_0_8x8_q8_0)",text)
            dispatch_ok=len(route_lines)==40 and all((int(m)==1 and fn.endswith("gemv_q4_0_8x8_q8_0")) or (int(m)>1 and fn.endswith("gemm_q4_0_8x8_q8_0")) for m,fn in route_lines) and "repack_object_vpdpbusd=218" in text
        dispatch="PASS" if dispatch_ok else "FAIL"
        print(f"candidate=ggml_{route} coverage_cells={sum(1 for _ in rawrows)}/40 sweep={'PASS' if sweep_ok else 'FAIL'} exact_group={'PASS' if group_ok else 'FAIL'} cancellation={'PASS' if cancel_ok else 'FAIL'} controls={'PASS' if ctrl_ok else 'FAIL'}")
        pages=page_policy(text,"ggml","ggml_"+route)
        aggregate.append(state_line("ggml_"+route,numerical,dispatch,pages))
    return "FAIL" if all_pages!="PASS" or "FAIL" in aggregate else "HOLD" if "HOLD" in aggregate else "PASS"

def gate_torch(path):
    text=read_log(path); overall=True
    candidates=("mm","matmul","linear","mv")
    rows={n:[] for n in candidates}
    current=None
    for line in text.splitlines():
        cm=re.search(r"cell M=(\d+) fixture=(\S+)",line)
        if cm: current=(int(cm.group(1)),cm.group(2))
        cm=re.search(r"candidate=(mm|matmul|linear|mv) status=(AVAILABLE_PASS|NOT_COMPARABLE|UNAVAILABLE).*?S12_failures=(\d+).*?exact_cancellation_zero=(\d+).*?representation=(\d+)",line)
        if cm and current: rows[cm.group(1)].append((current,cm.group(2),int(cm.group(3)),int(cm.group(4)),int(cm.group(5))))
    groups={n:(int(c),int(f),s) for n,c,f,s in re.findall(r"EXACT_GROUP_DOT_ISOLATION route=torch_(mm|matmul|linear|mv) checks=(\d+) failures=(\d+) status=(PASS|FAIL)",text)}
    sweeps={}
    for n,m,pos,batches,ef,sf in re.findall(r"SINGLE_SWEEP candidate=(mm|matmul|linear|mv) M=(\d+) positions=(\d+) batches=(\d+) exact_failures=(\d+) S12_failures=(\d+)",text): sweeps[(n,int(m))]=(int(pos),int(ef),int(sf))
    all_pages=page_policy(text,"torch")
    aggregate=[]
    for name in candidates:
        expected={(m,f) for m in MS for f in FIXTURES if name!="mv" or m==1}
        got={r[0] for r in rows[name]}
        numeric_rows=(len(rows[name])==len(expected) and got==expected and all(s=="AVAILABLE_PASS" and f==0 and c==1 and r==1 for _,s,f,c,r in rows[name]))
        group_expected=81920 if name=="mv" else 2375680
        group_ok=groups.get(name)==(group_expected,0,"PASS")
        required_ms=(1,) if name=="mv" else MS
        sweep_ok=all(sweeps.get((name,m))==(512,0,0) for m in required_ms) and len([k for k in sweeps if k[0]==name])==len(required_ms)
        numerical="PASS" if numeric_rows and group_ok and sweep_ok and "overall_numerical_equivalence=PASS" in text else "FAIL"
        out_fn={"mm":"at::mm_out","matmul":"at::matmul_out","linear":"at::linear_out","mv":"at::mv_out"}[name]
        out_evidence=any(ptr==storage and int(size)>0 for ptr,storage,size in re.findall(rf"OUT_WRITE candidate=torch_{name} function={re.escape(out_fn)} data_ptr=(0x[0-9a-fA-F]+) storage_ptr=(0x[0-9a-fA-F]+) bytes=(\d+) shape_preserved=true storage_preserved=true status=PASS",text))
        dispatch="PASS" if numeric_rows and out_evidence and re.search(rf"dispatch_candidate={name} observed_ops=.*aten::",text) else "FAIL"
        pages=page_policy(text,"torch","torch_"+name)
        aggregate.append(state_line("torch_"+name,numerical,dispatch,pages))
    return "FAIL" if all_pages!="PASS" or "FAIL" in aggregate else "HOLD" if "HOLD" in aggregate else "PASS"

if __name__=="__main__":
    if len(sys.argv)!=3 or sys.argv[1] not in {"ggml","torch"}:
        print("usage: qa_gate.py ggml|torch <log>",file=sys.stderr);sys.exit(2)
    result=gate_ggml(sys.argv[2]) if sys.argv[1]=="ggml" else gate_torch(sys.argv[2])
    print(f"FINAL_VERDICT={result}")
    sys.exit(0 if result=="PASS" else 2 if result=="HOLD" else 1)









