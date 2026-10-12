#!/usr/bin/env python3
"""QA gate for the single variable-O Q4 binary; checks S-16 page geometry."""
import json, re, sys
from pathlib import Path

MS=(1,4,8,16)
OUTPUTS=(912,1024,1824,2728,3640,4552,5464,6368,7280,9104,10920,14560,21848,29128,43688,58256)
FIXTURES=("uniform","mixed_cancellation","varied_scales","sub127","extremes","zero_input","single_coordinate","cancellation_high","cancellation_extreme","cancellation_exact_groups")
GROUP_FIXTURES=FIXTURES[:5]
BUFFERS={"weights":"packed_q4_and_fp16_scales","activations":"prepared_i8_scales_and_correction","Y":"kernel_output"}
U64_MAX=(1<<64)-1

def expected_bytes(buf,m,o):
    return {"weights":288*o,"activations":672*m,"Y":4*m*o,"dots":64*m*o}.get(buf,0)

def page_rows(path):
    obj=json.loads(Path(path).read_text(encoding="utf-8-sig")); sizes=obj["expected_bytes"]; gov=obj["governing_sizes"]
    if sizes!={"weights":"288*O","activations":"672*m","Y":"4*m*O","dots":"64*m*O"}: raise ValueError("expected_bytes PAGE contractuales incorrectos")
    if gov!={"n_W":"288*O","n_X":"672*m","n_Y":"4*m*O","n_dots_QA":"64*m*O"}: raise ValueError("governing_sizes PAGE contractuales incorrectos")
    if obj.get("outputs")!=list(OUTPUTS) or obj.get("m")!=list(MS): raise ValueError("geometria O/m esperada incorrecta")
    page=obj["dwPageSize"]
    if page != 4096: raise ValueError("dwPageSize contractual debe ser 4096")
    return page

def record_valid(b,m,o,a,n,q,v,l,status,lp,page):
    if m not in MS or o not in OUTPUTS or n!=expected_bytes(b,m,o): return False
    if not (0<a<=U64_MAX and 0<n<=U64_MAX and n<=U64_MAX-a and 0<page<=U64_MAX): return False
    span=a%page+n
    if span>U64_MAX-(page-1): return False
    qe=(span+page-1)//page
    return q==qe and q>0 and q==v==l and status=="PASS" and lp=="true"

def self_test():
    p=4096
    for b,m,o in (("weights",1,912),("activations",4,1024),("Y",8,1824),("dots",16,58256)):
        n=expected_bytes(b,m,o)
        if record_valid(b,m,o,0x1000,n,0,0,0,"PASS","true",p): raise AssertionError("counters incoherent accepted")
        if record_valid(b,m,o,0x1000,n-1,1,1,1,"PASS","true",p): raise AssertionError("wrong size accepted")
    if record_valid("weights",1,912,0x1000,1,1,1,1,"PASS","true",p): raise AssertionError("bytes=1 accepted")
    if record_valid("weights",1,1024,0x1000,expected_bytes("weights",1,912),1,1,1,"PASS","true",p): raise AssertionError("wrong O accepted")
    wanted={(o,m,f,b) for o in OUTPUTS for m in MS for f in FIXTURES for b in BUFFERS}
    if len(wanted-({(OUTPUTS[0],1,"uniform","weights")}))==len(wanted): raise AssertionError("missing key not detected")
    duplicate=[(OUTPUTS[0],1,"uniform","weights")]*2
    if len(duplicate)==len(set(duplicate)): raise AssertionError("duplicate not detected")
    print("KERNEL_GATE_SELF_TEST=PASS cases=dynamic_sizes,wrong_O,bytes1,missing,duplicate,incoherent_counters")
    return 0

def main(log,expected):
    text=Path(log).read_text(encoding="utf-8",errors="replace"); page=page_rows(expected)
    pat=(r"PAGE fixture=(\S+) m=(\d+) O=(\d+) candidate=omega_q4 buffer=(\S+) function=(\S+) "
         r"alias=(\S+) address=(0x[0-9a-fA-F]+) bytes=(\d+) queried_pages=(\d+) valid_pages=(\d+) large_pages=(\d+) QueryWorkingSetEx=(PASS|FAIL) LargePage=(true|false)")
    found={}
    for f,ms,os,b,fn,alias,ah,n,q,v,l,status,lp in re.findall(pat,text):
        m,o=int(ms),int(os); key=(f,m,o,b,fn,alias)
        if key in found: print(f"PAGE_DUPLICATE key={key}"); return 1
        found[key]=record_valid(b,m,o,int(ah,16),int(n),int(q),int(v),int(l),status,lp,page)
    wanted=set()
    for o in OUTPUTS:
        for f in FIXTURES:
            for m in MS:
                for b,fn in BUFFERS.items(): wanted.add((f,m,o,b,fn,f"cell_{m}_{f}_{b}_o{o}"))
        for m in MS:
            for b,fn in BUFFERS.items(): wanted.add(("single_coordinate_sweep",m,o,b,fn,f"single_coordinate_sweep_{m}_{b}_o{o}"))
            for f in GROUP_FIXTURES:
                for b,fn in {**BUFFERS,"dots":"qa_group_dot_int32"}.items(): wanted.add((f,m,o,b,fn,f"group_{m}_{f}_{b}_o{o}"))
    coverage=set(found)==wanted and all(found.values())
    if not coverage: print(f"PAGE_COVERAGE expected={len(wanted)} found={len(found)} missing={len(wanted-set(found))} extra={len(set(found)-wanted)} bad={sum(not x for x in found.values())}")
    for o in OUTPUTS:
        for m in MS:
            rows=[k for k in found if k[2]==o and k[1]==m]
            good=len(rows)==53 and all(found[k] for k in rows)
            print(f"PAGE_POLICY O={o} m={m} checks={len(rows)} failures={sum(not found[k] for k in rows)} status={'PASS' if good else 'FAIL'}")
    cell_pat=r"CELL fixture=(\S+) m=(\d+) O=(\d+) route=omega_q4 finite=(\d+) S12_pass=(\d+) S12_failures=(\d+).*?representation=(PASS|FAIL) negative_representation=(PASS|FAIL).*?exact_output_failures=(\d+)"
    cells={}
    for f,ms,os,fin,s12,nfail,repr_,neg,exact in re.findall(cell_pat,text): cells[(int(os),int(ms),f)]=(fin,s12,int(nfail),repr_,neg,int(exact))
    sweeps={(int(o),int(m)):(int(pos),int(fail),tag) for o,m,pos,fail,tag in re.findall(r"SINGLE_SWEEP route=omega_q4 O=(\d+) M=(\d+) positions=(\d+) batches=\d+ sampled_output_rows=first8,last8 checks=\d+ failures=(\d+) exact_integer_outputs=(PASS|FAIL)",text)}
    numerical={}
    exact_f={"zero_input","single_coordinate","cancellation_exact_groups"}
    for o in OUTPUTS:
        for m in MS:
            subset=[cells.get((o,m,f)) for f in FIXTURES]
            cellok=len(subset)==10 and all(x is not None for x in subset) and all(x[0]=="1" and x[1]=="1" and x[2]==0 and x[3]=="PASS" and x[4]=="PASS" for x in subset)
            cellok=cellok and all(x[5]==0 for f,x in zip(FIXTURES,subset) if f in exact_f)
            sw=sweeps.get((o,m))==(512,0,"PASS")
            numerical[o,m]=cellok and sw
            print(f"QA_NUMERICAL O={o} m={m} fixtures={sum(1 for f in FIXTURES if (o,m,f) in cells)}/10 failures={sum(cells[(o,m,f)][2] for f in FIXTURES if (o,m,f) in cells)} sweep_checks={(512*m*16 if sw else 0)} status={'PASS' if numerical[o,m] else 'FAIL'}")
    groups={(int(o),int(m)):(int(ch),int(fa),st) for o,m,ch,fa,st in re.findall(r"EXACT_GROUP_DOT_ISOLATION route=omega_q4 O=(\d+) m=(\d+) checks=(\d+) failures=(\d+) status=(PASS|FAIL)",text)}
    groupok=True
    for o in OUTPUTS:
        for m in MS:
            row=groups.get((o,m)); expected_checks=1280*m*o
            good=bool(row and row[0]==expected_checks and row[1]==0 and row[2]=="PASS")
            print(f"EXACT_GROUP_DOT_ISOLATION O={o} m={m} checks={row[0] if row else 0} failures={row[1] if row else 'NA'} status={'PASS' if good else 'FAIL'}")
            groupok &= good
    outputs=re.findall(r"OUTPUT_QA O=(\d+) numerical=(PASS|FAIL) coordinate_sweep=(PASS|FAIL) group_isolation=(PASS|FAIL)",text)
    alloutputs=len(outputs)==len(OUTPUTS) and all(x[1:]==("PASS","PASS","PASS") for x in outputs)
    final=coverage and all(numerical.values()) and groupok and alloutputs and "MANDATORY_GATE=PASS" in text and "PAGE_POLICY=PASS" in text and "BIT_IDENTITY_SUMMARY O=1024 checks=40 failures=0 status=PASS" in text
    print(f"KERNEL_QA_GATE={'PASS' if final else 'FAIL'}")
    return 0 if final else 2

if __name__=="__main__":
    if len(sys.argv)==2 and sys.argv[1]=="--self-test": sys.exit(self_test())
    if len(sys.argv)!=3: print("usage: qa_kernel_gate.py <qa log> <PAGE_EXPECTED_KERNEL.json>",file=sys.stderr); sys.exit(2)
    sys.exit(main(sys.argv[1],sys.argv[2]))

