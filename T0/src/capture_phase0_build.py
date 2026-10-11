"""Build-only capture for Phase 0 harness; the measurement modes are never invoked here."""
from __future__ import annotations

import hashlib
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path

T0=Path(__file__).resolve().parent.parent
LOGS=T0/"logs"
BUILD=T0/"src"/"build"/"build_phase0_bench.cmd"
CMD=Path(r"C:\Windows\System32\cmd.exe")
NAME="phase0_bench_build_attempt5"
manifest_path=LOGS/"COMMANDS.json"
manifest=json.loads(manifest_path.read_text(encoding="utf-8"))
argv=[str(CMD),"/c","call",str(BUILD)]
command=subprocess.list2cmdline(argv)
started=datetime.now(timezone.utc).isoformat()
result=subprocess.run(argv,cwd=LOGS,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=False)
(LOGS/f"{NAME}.command.txt").write_text(command+"\n",encoding="utf-8")
(LOGS/f"{NAME}.stdout.txt").write_bytes(result.stdout)
(LOGS/f"{NAME}.stderr.txt").write_bytes(result.stderr)
products=[T0/"src"/"build"/"phase0_bench.exe",T0/"src"/"build"/"phase0_bench.obj",T0/"src"/"build"/"phase0_q4_kernel.obj"]
missing=[str(p.relative_to(T0)) for p in products if not p.is_file()]
manifest.append({"name":NAME,"started_utc":started,"command":command,"exit_code":result.returncode,
                 "stdout_file":f"{NAME}.stdout.txt","stdout_sha256":hashlib.sha256(result.stdout).hexdigest().upper(),
                 "stderr_file":f"{NAME}.stderr.txt","stderr_sha256":hashlib.sha256(result.stderr).hexdigest().upper(),
                 "program_executed":False,"missing_compile_outputs":missing})
manifest_path.write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")

sums=T0/"SHA256SUMS.txt"
entries={}
for line in sums.read_text(encoding="utf-8").splitlines():
    digest,relative=line.split(None,1);entries[relative.strip()]=digest
for relative in ("src/phase0_bench.cpp","src/build/build_phase0_bench.cmd","src/capture_phase0_build.py","src/capture_phase0_measurements.py",
                 "results/T0.0/phase0_parameters.json",f"logs/{NAME}.command.txt",f"logs/{NAME}.stdout.txt",f"logs/{NAME}.stderr.txt"):
    entries[relative]=""
for path in (T0/"src"/"build").rglob("*"):
    if path.is_file():entries[path.relative_to(T0).as_posix()]=""
for relative in sorted(entries):
    path=T0/relative
    if path.is_file():entries[relative]=hashlib.sha256(path.read_bytes()).hexdigest().upper()
sums.write_text("".join(f"{entries[p]}  {p}\n" for p in sorted(entries)),encoding="utf-8")
print(json.dumps({"exit_code":result.returncode,"missing_compile_outputs":missing,"program_executed":False}))
if result.returncode!=0 or missing: raise SystemExit(result.returncode or 1)
