"""Authorized Phase 0 timing launcher. Requires the explicit AUTORIZADO token and runs serially."""
from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

if sys.argv[1:] != ["AUTORIZADO"]:
    raise SystemExit("No timing started: rerun this launcher only after explicit AUTORIZADO.")

T0=Path(__file__).resolve().parent.parent
RESULTS=T0/"results"/"T0.0"
RAW=RESULTS/"phase0_raw"
LOGS=T0/"logs"
EXE=T0/"src"/"build"/"phase0_bench.exe"
TOPOLOGY=RESULTS/"b_topologia.csv"
PS=Path(r"C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe")

params=json.loads((RESULTS/"phase0_parameters.json").read_text(encoding="utf-8"))
if params.get("state")!="PREDECLARED_BEFORE_ANY_TIMED_RUN" or params.get("protocol")!={
    "N":31,"warmup_count":5,"center":"median","dispersion":["IQR","MAD"],"outlier_discard_count":0
}:
    raise SystemExit("Frozen Phase 0 parameters do not match this launcher.")
if params["p_instr"]["outer_loop_iterations_per_sample"]*params["p_instr"]["static_unroll_factor"] != 1000000:
    raise SystemExit("P_instr iteration count differs from the frozen parameter record.")
if params["p_q4"]["calls_per_sample"] != 32 or params["p_q4"]["m_sweep"] != [1,2,4,8,16,32]:
    raise SystemExit("P_Q4 parameter record differs from the frozen sweep.")
if not EXE.is_file() or not TOPOLOGY.is_file():
    raise SystemExit("Phase 0 executable or topology CSV is missing.")
for name in ("p_instr_raw.csv","p_instr_summary.csv","p_q4_m_raw.csv","p_q4_m_summary.csv"):
    if (RAW/name).exists():
        raise SystemExit(f"Refusing to overwrite existing series output: {RAW/name}")
RAW.mkdir(exist_ok=True)

manifest_path=LOGS/"COMMANDS.json"
manifest=json.loads(manifest_path.read_text(encoding="utf-8"))

def refresh_hashes():
    sums=T0/"SHA256SUMS.txt"
    entries={}
    for line in sums.read_text(encoding="utf-8").splitlines():
        digest,relative=line.split(None,1);entries[relative.strip()]=digest
    entries["src/capture_phase0_measurements.py"]=""
    if (RESULTS/"phase0_execution_status.json").is_file(): entries["results/T0.0/phase0_execution_status.json"]=""
    for entry in manifest:
        if entry["name"].startswith("phase0_run_") or entry["name"].startswith("phase0_background_"):
            for suffix in ("command.txt","stdout.txt","stderr.txt"): entries[f"logs/{entry['name']}.{suffix}"]=""
    for path in RAW.rglob("*"):
        if path.is_file(): entries[path.relative_to(T0).as_posix()]=""
    for relative in sorted(entries):
        path=T0/relative
        if path.is_file(): entries[relative]=hashlib.sha256(path.read_bytes()).hexdigest().upper()
    sums.write_text("".join(f"{entries[p]}  {p}\n" for p in sorted(entries)),encoding="utf-8")

background_ps=r"""
$ErrorActionPreference = 'Stop'
$processes = @(Get-CimInstance Win32_Process | Select-Object ProcessId,ParentProcessId,Name,CommandLine)
$cpu = @()
for ($i=0; $i -lt 11; $i++) {
    $row = Get-CimInstance -ClassName Win32_PerfFormattedData_PerfOS_Processor -Filter "Name='_Total'"
    $cpu += [pscustomobject]@{ Timestamp=(Get-Date).ToUniversalTime().ToString('o'); InstanceName=$row.Name; PercentProcessorTime=$row.PercentProcessorTime; PercentIdleTime=$row.PercentIdleTime; PercentUserTime=$row.PercentUserTime; PercentPrivilegedTime=$row.PercentPrivilegedTime }
    if ($i -lt 10) { Start-Sleep -Seconds 1 }
}
[ordered]@{ captured_utc=(Get-Date).ToUniversalTime().ToString('o'); cpu_counter='CIM Win32_PerfFormattedData_PerfOS_Processor(_Total).PercentProcessorTime'; sample_interval_seconds=1; expected_samples=11; cpu_samples=$cpu; processes=$processes } | ConvertTo-Json -Depth 6
"""

def capture(name,argv,measurement=False):
    started=datetime.now(timezone.utc).isoformat()
    command=subprocess.list2cmdline(argv)
    t0=time.monotonic()
    result=subprocess.run(argv,cwd=LOGS,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=False)
    elapsed=time.monotonic()-t0
    (LOGS/f"{name}.command.txt").write_text(command+"\n",encoding="utf-8")
    (LOGS/f"{name}.stdout.txt").write_bytes(result.stdout)
    (LOGS/f"{name}.stderr.txt").write_bytes(result.stderr)
    manifest.append({"name":name,"started_utc":started,"completed_utc":datetime.now(timezone.utc).isoformat(),
                     "elapsed_seconds":round(elapsed,3),"command":command,"exit_code":result.returncode,
                     "stdout_file":f"{name}.stdout.txt","stdout_sha256":hashlib.sha256(result.stdout).hexdigest().upper(),
                     "stderr_file":f"{name}.stderr.txt","stderr_sha256":hashlib.sha256(result.stderr).hexdigest().upper(),
                     "measurement":measurement,"authorized_token_required":measurement})
    manifest_path.write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")
    print(f"{name}: exit={result.returncode}, elapsed={elapsed:.1f}s")
    return result.returncode,elapsed

before_code,before_seconds=capture("phase0_background_before_cim",[str(PS),"-NoProfile","-NonInteractive","-Command",background_ps])
run_status=[]
if before_code!=0:
    run_status.append({"name":"pre_background","exit_code":before_code,"elapsed_seconds":round(before_seconds,3)})
    status={"status":"NOT_STARTED_PRE_BACKGROUND_FAILED","authorized_token":"AUTORIZADO","series":run_status,"no_series_repeated":True}
    (RESULTS/"phase0_execution_status.json").write_text(json.dumps(status,indent=2)+"\n",encoding="utf-8")
    refresh_hashes()
    raise SystemExit("pre-measurement background capture failed; no series started")

commands=[
    ("phase0_run_p_instr",[str(EXE),"--run-instr",str(TOPOLOGY),str(RAW)]),
    ("phase0_run_p_q4_m",[str(EXE),"--run-q4-m",str(TOPOLOGY),str(RAW)]),
]
for name,argv in commands:
    code,elapsed=capture(name,argv,measurement=True)
    run_status.append({"name":name,"exit_code":code,"elapsed_seconds":round(elapsed,3)})
    if code!=0: break

after_code,after_seconds=capture("phase0_background_after_cim",[str(PS),"-NoProfile","-NonInteractive","-Command",background_ps])

measurement_complete=len(run_status)==2 and all(r["exit_code"]==0 for r in run_status)
complete=measurement_complete and after_code==0
status={"status":"COMPLETE" if complete else ("MEASUREMENTS_COMPLETE_POST_BACKGROUND_CAPTURE_FAILED" if measurement_complete else "STOPPED_AFTER_NONZERO_SERIES"),
        "authorized_token":"AUTORIZADO","series":run_status,
        "pre_background_exit_code":before_code,"pre_background_elapsed_seconds":round(before_seconds,3),
        "post_background_exit_code":after_code,"post_background_elapsed_seconds":round(after_seconds,3),
        "no_series_repeated":True,"parameters_file":"results/T0.0/phase0_parameters.json"}
(RESULTS/"phase0_execution_status.json").write_text(json.dumps(status,indent=2)+"\n",encoding="utf-8")
refresh_hashes()
print(json.dumps(status))
if not complete: raise SystemExit(3)
