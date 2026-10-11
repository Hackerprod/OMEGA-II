"""Record the judge-reported Chrome reopen incident; stop only a still-running Phase 0 launcher/benchmark."""
from __future__ import annotations

import hashlib
import json
import statistics
import subprocess
from datetime import datetime, timezone
from pathlib import Path

T0=Path(__file__).resolve().parent.parent
LOGS=T0/"logs"
RESULTS=T0/"results"/"T0.0"
RAW=RESULTS/"phase0_raw"
PS=Path(r"C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe")
NAME="phase0_incident_background_cim"
script=r"""
$ErrorActionPreference='Stop'
$stopped=@()
$targets=@(Get-CimInstance Win32_Process | Where-Object { $_.Name -eq 'phase0_bench.exe' -or (($_.Name -like 'python*.exe') -and $_.CommandLine -match 'capture_phase0_measurements\.py') })
foreach($p in $targets){ Stop-Process -Id $p.ProcessId -Force; $stopped += [pscustomobject]@{ProcessId=$p.ProcessId;Name=$p.Name;CommandLine=$p.CommandLine} }
$processes=@(Get-CimInstance Win32_Process | Select-Object ProcessId,ParentProcessId,Name,CommandLine)
$cpu=@()
for($i=0;$i -lt 11;$i++){
  $row=Get-CimInstance -ClassName Win32_PerfFormattedData_PerfOS_Processor -Filter "Name='_Total'"
  $cpu += [pscustomobject]@{Timestamp=(Get-Date).ToUniversalTime().ToString('o');InstanceName=$row.Name;PercentProcessorTime=$row.PercentProcessorTime;PercentIdleTime=$row.PercentIdleTime;PercentUserTime=$row.PercentUserTime;PercentPrivilegedTime=$row.PercentPrivilegedTime}
  if($i -lt 10){Start-Sleep -Seconds 1}
}
[ordered]@{captured_utc=(Get-Date).ToUniversalTime().ToString('o');cpu_counter='CIM Win32_PerfFormattedData_PerfOS_Processor(_Total).PercentProcessorTime';sample_interval_seconds=1;expected_samples=11;stopped_phase0_processes=$stopped;cpu_samples=$cpu;processes=$processes}|ConvertTo-Json -Depth 6
"""
argv=[str(PS),"-NoProfile","-NonInteractive","-Command",script]
command=subprocess.list2cmdline(argv)
started=datetime.now(timezone.utc).isoformat()
result=subprocess.run(argv,cwd=LOGS,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=False)
(LOGS/f"{NAME}.command.txt").write_text(command+"\n",encoding="utf-8")
(LOGS/f"{NAME}.stdout.txt").write_bytes(result.stdout)
(LOGS/f"{NAME}.stderr.txt").write_bytes(result.stderr)

manifest_path=LOGS/"COMMANDS.json"
manifest=json.loads(manifest_path.read_text(encoding="utf-8"))
manifest.append({"name":NAME,"started_utc":started,"completed_utc":datetime.now(timezone.utc).isoformat(),
                 "command":command,"exit_code":result.returncode,"measurement":False,
                 "stdout_file":f"{NAME}.stdout.txt","stdout_sha256":hashlib.sha256(result.stdout).hexdigest().upper(),
                 "stderr_file":f"{NAME}.stderr.txt","stderr_sha256":hashlib.sha256(result.stderr).hexdigest().upper()})
manifest_path.write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")

status_path=RESULTS/"phase0_execution_status.json"
status=json.loads(status_path.read_text(encoding="utf-8"))
prior_status=status.get("status")
status.update({"status":"INVALID_CONDITIONS_CHROME_REOPENED",
               "prior_launcher_status":prior_status,
               "invalidated_at_utc":datetime.now(timezone.utc).isoformat(),
               "invalidating_event":"Judge reported Chrome reopened during Phase 0 timing; 12 Chrome processes reported.",
               "reported_chrome_process_count":12,
               "invalidated_series":["phase0_run_p_instr","phase0_run_p_q4_m"],
               "completed_modes_before_invalidation_notice":["P_instr","P_Q4(m)"],
               "series_outputs_preserved":True,
               "no_series_repeated":True,
               "run2_output_directory":"results/T0.0/phase0_raw_run2",
               "incident_background_command":NAME,
               "incident_background_capture_exit_code":result.returncode})
status["phase0_raw_files_preserved"]=[p.name for p in sorted(RAW.glob("*")) if p.is_file()]
if result.returncode==0:
    snapshot=json.loads(result.stdout.decode("utf-8"))
    vals=[float(r["PercentProcessorTime"]) for r in snapshot["cpu_samples"]]
    chrome=[p for p in snapshot["processes"] if "chrome.exe" in (p.get("Name") or "").lower()]
    phase0=[p for p in snapshot["processes"] if (p.get("Name")=="phase0_bench.exe") or (p.get("Name","").lower().startswith("python") and "capture_phase0_measurements.py" in (p.get("CommandLine") or ""))]
    status["incident_background"]={"sample_count":len(vals),"mean_cpu_percent":statistics.mean(vals),
        "min_cpu_percent":min(vals),"max_cpu_percent":max(vals),"active_process_count":len(snapshot["processes"]),
        "chrome_process_count_in_cim_snapshot":len(chrome),"phase0_process_count_after_stop":len(phase0),
        "stopped_phase0_processes":snapshot["stopped_phase0_processes"]}
status_path.write_text(json.dumps(status,indent=2)+"\n",encoding="utf-8")

sums=T0/"SHA256SUMS.txt"
entries={}
for line in sums.read_text(encoding="utf-8").splitlines():
    digest,relative=line.split(None,1);entries[relative.strip()]=digest
for relative in ("src/capture_phase0_incident.py",f"logs/{NAME}.command.txt",f"logs/{NAME}.stdout.txt",f"logs/{NAME}.stderr.txt"):
    entries[relative]=""
entries["results/T0.0/phase0_execution_status.json"]=""
entries["logs/COMMANDS.json"]=""
for path in RAW.rglob("*"):
    if path.is_file():entries[path.relative_to(T0).as_posix()]=""
for relative in sorted(entries):
    path=T0/relative
    if path.is_file():entries[relative]=hashlib.sha256(path.read_bytes()).hexdigest().upper()
sums.write_text("".join(f"{entries[p]}  {p}\n" for p in sorted(entries)),encoding="utf-8")
print(json.dumps({"background_exit_code":result.returncode,"status":status["status"],
                  "incident_background":status.get("incident_background"),
                  "preserved_outputs":status.get("series_outputs_preserved")}))
if result.returncode!=0: raise SystemExit(result.returncode)
