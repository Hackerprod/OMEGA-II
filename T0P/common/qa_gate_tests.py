"""Negative page coverage tests for the frozen S-16 policies."""
import contextlib
import io
import re
from pathlib import Path
import qa_gate

EXPECTED={name:qa_gate.expected_page_rows(name) for name in ("ggml","torch")}
P=qa_gate.page_query_step()

def q_expected(address,size):
    return ((address%P)+size+P-1)//P

def log_for(rows):
    aliases={}
    lines=["LARGE_PAGE_PRIVILEGE=AVAILABLE"]
    for row in rows:
        alias=row["alias"]
        address=aliases.setdefault(alias,0x10000000+len(aliases)*0x200000)
        size=row["bytes"]
        q=q_expected(address,size)
        lines.append("PAGE fixture={fixture} m={m} candidate={candidate} buffer={buffer} function={function} alias={alias} address={address:#x} bytes={bytes} queried_pages={q} valid_pages={q} large_pages={q} QueryWorkingSetEx=PASS".format(**row,address=address,q=q))
    return "\n".join(lines)+"\n"

def change_row(text, selector, **changes):
    pattern=(r"PAGE fixture=(?P<fixture>\S+) m=(?P<m>\d+) candidate=(?P<candidate>\S+) buffer=(?P<buffer>\S+) function=(?P<function>\S+) alias=(?P<alias>\S+) address=(?P<address>0x[0-9a-fA-F]+) bytes=(?P<bytes>\d+) queried_pages=(?P<q>\d+) valid_pages=(?P<v>\d+) large_pages=(?P<l>\d+) QueryWorkingSetEx=(?P<status>PASS|FAIL)")
    for match in re.finditer(pattern,text):
        row=match.groupdict()
        if all(row[k]==str(v) for k,v in selector.items()):
            row.update({k:str(v) for k,v in changes.items()})
            new=("PAGE fixture={fixture} m={m} candidate={candidate} buffer={buffer} function={function} alias={alias} address={address} bytes={bytes} queried_pages={q} valid_pages={v} large_pages={l} QueryWorkingSetEx={status}").format(**row)
            return text[:match.start()]+new+text[match.end():]
    raise AssertionError(f"record not found: {selector}")

def assert_complete_failure(reference,text,candidate):
    out=io.StringIO()
    with contextlib.redirect_stdout(out):
        verdict=(qa_gate.gate_ggml_text(text) if reference=="ggml" else qa_gate.gate_torch_text(text))
    rendered=out.getvalue()
    assert verdict=="FAIL",f"{reference}: complete gate verdict was {verdict}"
    assert f"candidate={candidate} " in rendered and "PAGE_POLICY=FAIL" in rendered and "REFERENCE_READY=FAIL" in rendered, rendered

for reference in ("ggml","torch"):
    rows=qa_gate.expected_page_rows(reference)
    baseline=log_for(rows)
    assert qa_gate.page_policy(baseline,reference)=="PASS",reference
    assert qa_gate.page_policy(log_for(rows[1:]),reference)=="FAIL",f"{reference}: missing required region accepted"
    assert qa_gate.page_policy(log_for(rows[:1]),reference)=="FAIL",f"{reference}: one-row log accepted"
    first=rows[0]
    duplicate=baseline+log_for([first]).splitlines()[1]+"\n"
    assert qa_gate.page_policy(duplicate,reference)=="FAIL",f"{reference}: duplicate accepted"
    zero=change_row(baseline,{"fixture":first["fixture"],"m":first["m"],"candidate":first["candidate"],"buffer":first["buffer"]},q=0,v=0,l=0)
    assert qa_gate.page_policy(zero,reference)=="FAIL",f"{reference}: zero pages accepted"
    retained=rows[:1]+rows[120:]
    assert len(rows[:120])-len(rows[:1])==119
    assert qa_gate.page_policy(log_for(retained),reference)=="FAIL",f"{reference}: 119/120 deletion accepted"
    not_valid=change_row(baseline,{"fixture":first["fixture"],"m":first["m"],"candidate":first["candidate"],"buffer":first["buffer"]},v=0)
    assert qa_gate.page_policy(not_valid,reference)=="FAIL",f"{reference}: invalid page accepted"
    print(f"page_policy_tests reference={reference} baseline=PASS missing=REJECTED one_row=REJECTED duplicate=REJECTED zero_pages=REJECTED sol_119_of_120=REJECTED valid_pages_mismatch=REJECTED")

# Per-record mutations against the coherent synthetic baseline.
base=log_for(EXPECTED["ggml"])
row=EXPECTED["ggml"][0]
selector={"fixture":row["fixture"],"m":row["m"],"candidate":row["candidate"],"buffer":row["buffer"],"function":row["function"]}
match=re.search(rf"PAGE fixture={row['fixture']} m={row['m']} candidate={row['candidate']} buffer={row['buffer']} function={row['function']} alias=\S+ address=(0x[0-9a-fA-F]+) bytes=(\d+) queried_pages=(\d+)",base)
a,n,q=map(lambda x:int(x,16) if x.startswith("0x") else int(x),match.groups())
assert qa_gate.page_policy(change_row(base,selector,q=q+1,v=q+1,l=q+1),"ggml")=="FAIL", "q>expected accepted"
assert qa_gate.page_policy(change_row(base,selector,v=q-1),"ggml")=="FAIL", "v<q accepted"
assert qa_gate.page_policy(change_row(base,selector,l=q-1),"ggml")=="FAIL", "l<q accepted"
misaligned=a+1
qm=q_expected(misaligned,n)
assert qm!=q
assert qa_gate.page_policy(change_row(base,selector,address=hex(misaligned),q=qm,v=qm,l=qm),"ggml")=="PASS", "coherent misaligned address rejected"
assert qa_gate.page_policy(change_row(base,selector,address=hex(misaligned)),"ggml")=="FAIL", "misaligned address with stale count accepted"
assert qa_gate.page_policy(change_row(base,selector,bytes=0),"ggml")=="FAIL", "n=0 accepted"
print("record_mutations q_greater=REJECTED valid_less_than_q=REJECTED large_less_than_q=REJECTED misaligned_address_recount=PASS misaligned_stale_count=REJECTED n_zero=REJECTED")

# Sol's governing-log counterexamples, mutated only as in-memory strings.
cases=(
    ("ggml","ggml_flat","uniform","1","weights","294912"),
    ("torch","torch_mm","uniform","1","Wt","2097152"),
)
for reference,candidate,fixture,m,buffer,size in cases:
    path=Path(__file__).parents[1]/f"ref_{reference}"/"adapter"/"qa_s15_governing.txt"
    original=qa_gate.read_log(path)
    selector={"fixture":fixture,"m":m,"candidate":candidate,"buffer":buffer,"bytes":size}
    mm=re.search(rf"PAGE fixture={fixture} m={m} candidate={candidate} buffer={buffer} function=\S+ alias=\S+ address=(0x[0-9a-fA-F]+) bytes={size} queried_pages=(\d+)",original)
    assert mm and int(mm.group(2))==(72 if reference=="ggml" else 512)
    mutated=change_row(original,selector,q=1,v=1,l=1)
    assert_complete_failure(reference,mutated,candidate)
    print(f"sol_counterexample reference={reference} candidate={candidate} {buffer} bytes={size} original_q={mm.group(2)} q=v=l=1 PAGE_POLICY=FAIL REFERENCE_READY=FAIL")

print("qa_gate_s16_tests=PASS")
