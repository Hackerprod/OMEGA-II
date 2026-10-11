#!/usr/bin/env python3
"""Parse QA logs into a machine verdict. Timing/performance fields are ignored."""
import re, sys
from pathlib import Path

def read_log(path):
    raw=Path(path).read_bytes()
    if raw.startswith(b"\xff\xfe"): return raw.decode("utf-16le",errors="replace")
    if raw.startswith(b"\xfe\xff"): return raw.decode("utf-16be",errors="replace")
    if raw.startswith(b"\xef\xbb\xbf"): return raw.decode("utf-8-sig",errors="replace")
    return raw.decode("utf-8",errors="replace")

def gate_ggml(path):
    text=read_log(path); cells=[]
    for line in text.splitlines():
        cm=re.search(r"CELL fixture=(\S+) m=(\d+) route=(flat|repack).*?finite=(\d+) S12_pass=(\d+) S12_failures=(\d+)",line)
        if cm:
            fixture,m,route,finite,s12,failures=cm.groups(); passed=finite=="1" and s12=="1" and failures=="0"
            cells.append((fixture,m,route,passed)); print(f"candidate=ggml_{route} cell={fixture}/m={m} verdict={'PASS' if passed else 'FAIL'}")
    groups=re.findall(r"EXACT_GROUP_DOT_ISOLATION route=ggml_(flat|repack) checks=(\d+) failures=(\d+) status=(PASS|FAIL)",text)
    group_ok=len(groups)==2
    for route,count,failures,status in groups:
        passed=status=="PASS" and failures=="0" and int(count)==2375680
        group_ok &= passed; print(f"candidate=ggml_{route} cell=EXACT_GROUP_DOT_ISOLATION verdict={'PASS' if passed else 'FAIL'} checks={count} failures={failures}")
    cancels=re.findall(r"EXACT_CANCELLATION route=(flat|repack) m=(\d+) checks=(\d+) failures=(\d+) status=(PASS|FAIL)",text)
    cancel_ok=len(cancels)==8 and all(x[3]=="0" and x[4]=="PASS" for x in cancels)
    for route,m,count,failures,status in cancels:
        print(f"candidate=ggml_{route} cell=EXACT_CANCELLATION/m={m} verdict={'PASS' if failures=='0' and status=='PASS' else 'FAIL'} checks={count} failures={failures}")
    callbacks=all(f"S13D_FLAT_CALLBACK m={m} reached=true" in text for m in (1,4,8,16))
    static_iso=re.search(r"STATIC_ROUTE_ISOLATION .*status=PASS",text) is not None and re.search(r"DYNAMIC_ROUTE_ISOLATION .*status=PASS",text) is not None
    page_rows=re.findall(r"queried_pages=(\d+) valid_pages=(\d+).*QueryWorkingSetEx=(PASS|FAIL)",text)
    pages=bool(page_rows) and all(a==b and status=="PASS" for a,b,status in page_rows) and "LARGE_PAGE_PRIVILEGE=HOLD" in text
    passed=len(cells)==80 and all(c[3] for c in cells) and group_ok and cancel_ok and callbacks and static_iso and pages and "MANDATORY_GATE=PASS" in text
    print(f"candidate=ggml_all cells={len(cells)}/80 callbacks={'PASS' if callbacks else 'FAIL'} route_isolation={'PASS' if static_iso else 'FAIL'} pages={'PASS' if pages else 'FAIL'} verdict={'PASS' if passed else 'FAIL'}")
    return passed

def gate_torch(path):
    text=read_log(path); current="unknown"; rows=[]
    for line in text.splitlines():
        cm=re.search(r"cell M=(\d+) fixture=(\S+)",line)
        if cm: current=f"{cm.group(2)}/m={cm.group(1)}"
        cm=re.search(r"candidate=(mm|matmul|linear|mv) status=(AVAILABLE_PASS|NOT_COMPARABLE|UNAVAILABLE).*?S12_failures=(\d+).*?exact_cancellation_zero=(\d+).*?representation=(\d+)",line)
        if cm:
            name,status,failures,cancel,representation=cm.groups(); passed=status=="AVAILABLE_PASS" and failures=="0" and cancel=="1" and representation=="1"
            rows.append((name,current,passed)); print(f"candidate=torch_{name} cell={current} verdict={'PASS' if passed else 'FAIL'}")
    groups=re.findall(r"EXACT_GROUP_DOT_ISOLATION route=torch_(mm|matmul|linear|mv) checks=(\d+) failures=(\d+) status=(PASS|FAIL)",text)
    group_ok=len(groups)==4
    for name,count,failures,status in groups:
        expected=81920 if name=="mv" else 2375680
        passed=status=="PASS" and failures=="0" and int(count)==expected
        group_ok &= passed; print(f"candidate=torch_{name} cell=EXACT_GROUP_DOT_ISOLATION verdict={'PASS' if passed else 'FAIL'} checks={count} failures={failures}")
    page_rows=re.findall(r"queried_pages=(\d+) valid_pages=(\d+).*QueryWorkingSetEx=(PASS|FAIL)",text)
    pages=bool(page_rows) and all(a==b and status=="PASS" for a,b,status in page_rows) and "LARGE_PAGE_PRIVILEGE=HOLD" in text
    passed=len(rows)==130 and all(r[2] for r in rows) and group_ok and pages and "overall_numerical_equivalence=PASS" in text
    print(f"candidate=torch_all cells={len(rows)}/130 group_dots={'PASS' if group_ok else 'FAIL'} pages={'PASS' if pages else 'FAIL'} verdict={'PASS' if passed else 'FAIL'}")
    return passed

if __name__=="__main__":
    if len(sys.argv)!=3 or sys.argv[1] not in {"ggml","torch"}:
        print("usage: qa_gate.py ggml|torch <log>",file=sys.stderr);sys.exit(2)
    result=gate_ggml(sys.argv[2]) if sys.argv[1]=="ggml" else gate_torch(sys.argv[2])
    print(f"FINAL_VERDICT={'PASS' if result else 'FAIL'}")
    sys.exit(0 if result else 1)