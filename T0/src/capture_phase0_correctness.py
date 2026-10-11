"""Run correctness-only checks for Phase 0; no performance/timing modes are invoked."""
from __future__ import annotations

import hashlib
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path

T0=Path(__file__).resolve().parent.parent
LOGS=T0/"logs"
manifest_path=LOGS/"COMMANDS.json"
manifest=json.loads(manifest_path.read_text(encoding="utf-8"))
commands=[
    ("phase0_existing_q4_correctness_attempt3",[str(T0/"build"/"out"/"q4_correctness.exe")]),
    ("phase0_new_matrixization_correctness_attempt3",[str(T0/"src"/"build"/"phase0_bench.exe"),"--correctness"]),
]
failed=[]
for name,argv in commands:
    started=datetime.now(timezone.utc).isoformat()
    command=subprocess.list2cmdline(argv)
    result=subprocess.run(argv,cwd=LOGS,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=False)
    (LOGS/f"{name}.command.txt").write_text(command+"\n",encoding="utf-8")
    (LOGS/f"{name}.stdout.txt").write_bytes(result.stdout)
    (LOGS/f"{name}.stderr.txt").write_bytes(result.stderr)
    stdout=result.stdout.decode("utf-8",errors="replace")
    passed=result.returncode==0 and ("status=PASS" in stdout or '"status":"PASS"' in stdout)
    manifest.append({"name":name,"started_utc":started,"command":command,"exit_code":result.returncode,
                     "program_mode":"correctness_only_no_timing","stdout_file":f"{name}.stdout.txt",
                     "stdout_sha256":hashlib.sha256(result.stdout).hexdigest().upper(),
                     "stderr_file":f"{name}.stderr.txt","stderr_sha256":hashlib.sha256(result.stderr).hexdigest().upper(),
                     "summary_pass":passed,"measurement":False})
    manifest_path.write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")
    print(f"{name}: exit={result.returncode}, pass={passed}")
    if not passed: failed.append(name)

sums=T0/"SHA256SUMS.txt"
entries={}
for line in sums.read_text(encoding="utf-8").splitlines():
    digest,relative=line.split(None,1);entries[relative.strip()]=digest
for relative in ("src/capture_phase0_correctness.py",): entries[relative]=""
for name,_ in commands:
    for suffix in ("command.txt","stdout.txt","stderr.txt"): entries[f"logs/{name}.{suffix}"]=""
for relative in sorted(entries):
    path=T0/relative
    if path.is_file(): entries[relative]=hashlib.sha256(path.read_bytes()).hexdigest().upper()
sums.write_text("".join(f"{entries[p]}  {p}\n" for p in sorted(entries)),encoding="utf-8")
if failed: raise SystemExit(1)
