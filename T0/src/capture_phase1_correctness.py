"""Capture correctness-only Phase 1 suite; no timing-mode command is issued."""
from __future__ import annotations

import hashlib
import json
import subprocess
from datetime import datetime,timezone
from pathlib import Path

T0=Path(__file__).resolve().parent.parent
LOGS=T0/"logs"
EXE=T0/"src"/"build"/"phase1_correctness.exe"
NAME="phase1_correctness_only_attempt24"
manifest_path=LOGS/"COMMANDS.json"
manifest=json.loads(manifest_path.read_text(encoding="utf-8"))
argv=[str(EXE)]
command=subprocess.list2cmdline(argv)
started=datetime.now(timezone.utc).isoformat()
result=subprocess.run(argv,cwd=LOGS,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=False)
(LOGS/f"{NAME}.command.txt").write_text(command+"\n",encoding="utf-8")
(LOGS/f"{NAME}.stdout.txt").write_bytes(result.stdout)
(LOGS/f"{NAME}.stderr.txt").write_bytes(result.stderr)
stdout=result.stdout.decode("utf-8",errors="replace")
guard_rows=[]
for line in stdout.splitlines():
    try:
        row=json.loads(line)
        if row.get("record")=="unsupported_matrixization_guard":guard_rows.append(row)
    except json.JSONDecodeError:pass
passed=(result.returncode==0 and '"status":"PASS"' in stdout and '"measurement":false' in stdout and
        {row.get("M") for row in guard_rows}=={3,6} and all(row.get("rejected_before_kernel_dispatch") is True for row in guard_rows))
manifest.append({"name":NAME,"started_utc":started,"command":command,"exit_code":result.returncode,
                 "program_mode":"correctness_only_no_timing","measurement":False,"summary_pass":passed,
                 "stdout_file":f"{NAME}.stdout.txt","stdout_sha256":hashlib.sha256(result.stdout).hexdigest().upper(),
                 "stderr_file":f"{NAME}.stderr.txt","stderr_sha256":hashlib.sha256(result.stderr).hexdigest().upper()})
manifest_path.write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")

sums=T0/"SHA256SUMS.txt"
entries={}
for line in sums.read_text(encoding="utf-8").splitlines():
    digest,relative=line.split(None,1);entries[relative.strip()]=digest
for relative in ("src/capture_phase1_correctness.py",):entries[relative]=""
for suffix in ("command.txt","stdout.txt","stderr.txt"):entries[f"logs/{NAME}.{suffix}"]=""
entries["logs/COMMANDS.json"]=""
for relative in sorted(entries):
    path=T0/relative
    if path.is_file():entries[relative]=hashlib.sha256(path.read_bytes()).hexdigest().upper()
    else:del entries[relative]
sums.write_text("".join(f"{entries[p]}  {p}\n" for p in sorted(entries)),encoding="utf-8")
print(json.dumps({"exit_code":result.returncode,"summary_pass":passed,"program_mode":"correctness_only_no_timing"}))
if not passed:raise SystemExit(1)
