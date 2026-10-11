import contextlib, io, tempfile
from pathlib import Path
import qa_gate

source=Path(r"C:\Projects\OMEGA\Ejercise\T0P\ref_ggml\adapter\qa_output.txt").read_text(encoding="utf-16" if Path(r"C:\Projects\OMEGA\Ejercise\T0P\ref_ggml\adapter\qa_output.txt").read_bytes().startswith(b"\xff\xfe") else "utf-8",errors="replace")

def check(mutated, expected):
    with tempfile.NamedTemporaryFile("w",encoding="utf-8",dir=Path(__file__).parent,delete=False) as f:
        f.write(mutated); name=f.name
    try:
        out=io.StringIO()
        with contextlib.redirect_stdout(out): qa_gate.gate_ggml(name)
        assert expected in out.getvalue(), out.getvalue()
    finally:
        Path(name).unlink()

check(source.replace("flat_failures=0","flat_failures=1",1),"sweep=FAIL")
check(source.replace("STATIC_ROUTE_ISOLATION flat=PASS repack=PASS status=PASS\n",""),"controls=FAIL")
large_page_text=source.replace("LARGE_PAGE_PRIVILEGE=HOLD","LARGE_PAGE_PRIVILEGE=AVAILABLE",1)
assert qa_gate.page_policy(large_page_text)=="FAIL"
print("qa_gate_negative_tests=PASS injected_sweep_failure=REJECTED missing_control=REJECTED privilege_present_nonlarge=FAIL")
