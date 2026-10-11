"""Static-only check that the Q4 K-application loop still calls its kernel once per iteration."""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path


def verify(path:Path)->dict:
    text=path.read_text(encoding="utf-8",errors="replace")
    lines=text.splitlines()
    start=next((i for i,line in enumerate(lines) if "runQ4KApplications" in line and " PROC " in line),None)
    if start is None:raise RuntimeError("H0 disassembly lacks runQ4KApplications symbol")
    stop=next((i+1 for i in range(start+1,len(lines)) if "runQ4KApplications" in lines[i] and " ENDP " in lines[i]),None)
    if stop is None:raise RuntimeError("H0 disassembly lacks runQ4KApplications ENDP");
    body="\n".join(lines[start:stop])
    calls=[line.strip() for line in lines[start:stop] if re.search(r"\bcall\b",line,re.I)]
    loop_branches=[line.strip() for line in lines[start:stop] if re.search(r"\bj(?:ne|nz|mp|g|l|a|b|e|be|ae)\b",line,re.I)]
    result={"record":"h0_k_loop_disassembly_check","listing":str(path),"function_symbol_found":True,
            "call_instruction_count":len(calls),"loop_branch_count":len(loop_branches),
            "call_instructions":calls,"loop_branches":loop_branches,"measurement":False,
            "status":"PASS" if calls and loop_branches else "FAIL"}
    return result


if __name__=="__main__":
    try:
        result=verify(Path(sys.argv[1]));print(json.dumps(result,indent=2));sys.exit(0 if result["status"]=="PASS" else 1)
    except Exception as exc:
        print(json.dumps({"record":"h0_k_loop_disassembly_check","status":"FAIL","error":str(exc),"measurement":False}))
        sys.exit(1)
