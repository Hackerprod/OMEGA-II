"""Negative coverage tests for both frozen S-15 page policies."""
import json
from pathlib import Path
import qa_gate

EXPECTED=json.loads(Path(__file__).with_name("PAGE_EXPECTED_S15.json").read_text(encoding="utf-8"))["references"]

def log_for(rows):
    aliases={}
    lines=["LARGE_PAGE_PRIVILEGE=AVAILABLE"]
    for row in rows:
        alias=row["alias"]
        address=aliases.setdefault(alias,hex(0x10000000+len(aliases)*0x200000))
        lines.append("PAGE fixture={fixture} m={m} candidate={candidate} buffer={buffer} function={function} alias={alias} address={address} bytes={bytes} queried_pages=1 valid_pages=1 large_pages=1 QueryWorkingSetEx=PASS".format(**row,address=address))
    return "\n".join(lines)+"\n"

for reference in ("ggml","torch"):
    rows=EXPECTED[reference]
    baseline=log_for(rows)
    assert qa_gate.page_policy(baseline,reference)=="PASS",reference
    assert qa_gate.page_policy(log_for(rows[1:]),reference)=="FAIL",f"{reference}: missing required region accepted"
    assert qa_gate.page_policy(log_for(rows[:1]),reference)=="FAIL",f"{reference}: one-row log accepted"
    contradicted=baseline+log_for([dict(rows[0],bytes=rows[0]["bytes"]+4)]).splitlines()[1]+"\n"
    assert qa_gate.page_policy(contradicted,reference)=="FAIL",f"{reference}: contradictory duplicate accepted"
    zero=baseline.replace("queried_pages=1 valid_pages=1 large_pages=1","queried_pages=0 valid_pages=0 large_pages=0",1)
    assert qa_gate.page_policy(zero,reference)=="FAIL",f"{reference}: zero pages accepted"
    # Sol counterexample shape: retain one out of the first 120 records and omit 119.
    retained=rows[:1]+rows[120:]
    assert len(rows[:120])-len(rows[:1])==119
    assert qa_gate.page_policy(log_for(retained),reference)=="FAIL",f"{reference}: 119/120 deletion accepted"
    not_valid=baseline.replace("valid_pages=1 large_pages=1","valid_pages=0 large_pages=1",1)
    assert qa_gate.page_policy(not_valid,reference)=="FAIL",f"{reference}: nonvalid page accepted"
    print(f"page_policy_negative_tests reference={reference} baseline=PASS missing=REJECTED one_row=REJECTED duplicate_conflict=REJECTED zero_pages=REJECTED sol_119_of_120=REJECTED invalid_page=REJECTED")
print("qa_gate_negative_tests=PASS")
