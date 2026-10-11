"""Compile Phase 1 production-timed and QA builds only; never runs a timed mode."""
from __future__ import annotations

import hashlib
import json
import subprocess
import sys
from datetime import datetime,timezone
from pathlib import Path

T0=Path(__file__).resolve().parent.parent
LOGS=T0/"logs"
CMD=Path(r"C:\Windows\System32\cmd.exe")
manifest_path=LOGS/"COMMANDS.json"
manifest=json.loads(manifest_path.read_text(encoding="utf-8"))
if sys.argv[1:]==["--qa-only"]:
    jobs=[("phase1_build_qa_correctness_attempt42",T0/"src"/"build"/"build_phase1_correctness.cmd")]
elif not sys.argv[1:]:
    jobs=[("phase1_build_timed_kernel_attempt42",T0/"src"/"build"/"build_phase1_measure_harness.cmd"),
          ("phase1_build_qa_correctness_attempt42",T0/"src"/"build"/"build_phase1_correctness.cmd")]
else:
    raise SystemExit("usage: capture_phase1_builds.py [--qa-only]")
failed=[]
for name,script in jobs:
    argv=[str(CMD),"/c","call",str(script)]
    command=subprocess.list2cmdline(argv)
    started=datetime.now(timezone.utc).isoformat()
    result=subprocess.run(argv,cwd=LOGS,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=False)
    (LOGS/f"{name}.command.txt").write_text(command+"\n",encoding="utf-8")
    (LOGS/f"{name}.stdout.txt").write_bytes(result.stdout)
    (LOGS/f"{name}.stderr.txt").write_bytes(result.stderr)
    manifest.append({"name":name,"started_utc":started,"completed_utc":datetime.now(timezone.utc).isoformat(),
                     "command":command,"exit_code":result.returncode,"measurement":False,"program_executed":False,
                     "stdout_file":f"{name}.stdout.txt","stdout_sha256":hashlib.sha256(result.stdout).hexdigest().upper(),
                     "stderr_file":f"{name}.stderr.txt","stderr_sha256":hashlib.sha256(result.stderr).hexdigest().upper()})
    manifest_path.write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")
    print(f"{name}: exit={result.returncode}")
    if result.returncode!=0: failed.append(name);break

sums=T0/"SHA256SUMS.txt"
entries={}
for line in sums.read_text(encoding="utf-8").splitlines():
    parts=line.split(None,1)
    if len(parts)==2:entries[parts[1].strip()]=parts[0]
for relative in ("src/capture_phase1_builds.py","src/capture_phase1_correctness.py","src/capture_phase1_measurements.py","src/summarize_phase1.py","src/test_summarize_phase1_synthetic.py",
                 "src/q4_kernel.cpp","src/q4_kernel.hpp","src/q4_kernel_preopt.cpp","src/q4_kernel_preopt.hpp","src/phase1_measure_harness.cpp","src/phase1_instr_x64.asm",
                  "src/phase1_correctness.cpp","src/build/build_phase1_measure_harness.cmd","src/build/build_phase1_correctness.cmd",
                  "build/build_q4_correctness.cmd","results/T0.0/phase1_measurement_plan.json","results/T0.0/phase1_timed_build_command.txt","results/T0.0/phase1_disassembly_evidence.md","results/T0.0/phase1_session_launch.md","results/T0.0/phase1_session_report_template.md","results/T0.0/phase1_raw/p_core_m1_order.json",
                  "results/T0.0/a_inventario.md","results/T0.0/i_condiciones.md","results/T0.0/legacy_labels_manifest.md","results/T0.0/INCIDENT_preflight_segment2_overwritten.md","results/T0.0/phase1_T0.0_final_table.csv","results/T0.0/phase1_session_report.md","results/T0.0/phase1_T0.1_handoff.md","../T0.md"):
    entries[relative]=""
for name,_ in jobs:
    for suffix in ("command.txt","stdout.txt","stderr.txt"):entries[f"logs/{name}.{suffix}"]=""
for path in (T0/"src"/"build").rglob("*"):
    if path.is_file():entries[path.relative_to(T0).as_posix()]=""
for relative in sorted(entries):
    path=T0/relative
    if path.is_file():entries[relative]=hashlib.sha256(path.read_bytes()).hexdigest().upper()
    else:del entries[relative]
sums.write_text("".join(f"{entries[p]}  {p}\n" for p in sorted(entries)),encoding="utf-8")
if failed:raise SystemExit(1)
