"""Single authorized Phase 1 official session runner; all conditions checked before any timed series."""
from __future__ import annotations

import hashlib
import csv
import json
import math
import io
import re
import statistics
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

resume_e_core=sys.argv[1:]==["AUTORIZADO","--resume-e-core"]
resume_pcore_m1=sys.argv[1:]==["AUTORIZADO","--resume-pcore-m1"]
resume_mode=resume_e_core or resume_pcore_m1
session_segment=("segment_2_e_core" if resume_e_core else ("segment_3_pcore_m1" if resume_pcore_m1 else "segment_1_initial"))
if sys.argv[1:] not in (["AUTORIZADO"],["AUTORIZADO","--resume-e-core"],["AUTORIZADO","--resume-pcore-m1"]):
    raise SystemExit("SESSION=HOLD; exact launch token required: AUTORIZADO [--resume-e-core|--resume-pcore-m1]")

T0=Path(__file__).resolve().parent.parent
RESULTS=T0/"results"/"T0.0"
RAW=RESULTS/"phase1_raw"
LOGS=T0/"logs"
EXE=T0/"src"/"build"/"phase1_measure_harness.exe"
SEP=Path(r"C:\Program Files (x86)\Intel\oneAPI\vtune\2026.4\bin64\sep.exe")
VTUNE=Path(r"C:\Program Files (x86)\Intel\oneAPI\vtune\2026.4\bin64\vtune.exe")
TOPOLOGY=RESULTS/"b_topologia.csv"
PS=Path(r"C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe")
manifest_path=LOGS/"COMMANDS.json"
manifest=json.loads(manifest_path.read_text(encoding="utf-8"))
plan=json.loads((RESULTS/"phase1_measurement_plan.json").read_text(encoding="utf-8"))

if plan["state"]!="PREDECLARED_NOT_MEASURED_WAITING_FOR_AUTORIZADO":
    raise SystemExit("Phase 1 plan is not in its frozen premeasurement state.")
if plan["statistics"]!={"N":31,"warmup_count":5,"center":"median","dispersion":["IQR","MAD"],"outlier_discard_count":0}:
    raise SystemExit("Frozen statistical parameters differ from the approved plan.")
if plan.get("TIMED_REGION_MIN_NS")!=100000000 or plan.get("calibration",{}).get("target_duration_ns")!=150000000:
    raise SystemExit("Frozen MD/27 timer/calibration parameters differ from approval.")
if not EXE.is_file() or not TOPOLOGY.is_file():
    raise SystemExit("Phase 1 harness executable or topology CSV is missing.")
raw_occupied=RAW.exists() and any(p.is_file() for p in RAW.rglob("*"))

expected_power_guid="8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c"
expected_cpu="13th Gen Intel(R) Core(TM) i7-13700F"
expected_topology_sha256="8E5CDA8410873852C3DD266E2CC1535376DEEA3705328173BC879117F4574CAF"
background_ps=rf"""
$ErrorActionPreference = 'Stop'
$expectedPower = '{expected_power_guid}'
$powerText = (& powercfg.exe /getactivescheme 2>&1 | Out-String)
$powerMatch = [regex]::Match($powerText, '[0-9a-fA-F]{{8}}-[0-9a-fA-F]{{4}}-[0-9a-fA-F]{{4}}-[0-9a-fA-F]{{4}}-[0-9a-fA-F]{{12}}')
$powerGuid = if ($powerMatch.Success) {{ $powerMatch.Value.ToLowerInvariant() }} else {{ '' }}
$cpuInfo = @(Get-CimInstance Win32_Processor | Select-Object -ExpandProperty Name)
$logicalCount = (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
$chromeBefore = @(Get-Process chrome -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)
$procWatch = [System.Diagnostics.Stopwatch]::StartNew()
$proc0 = @{{}}
Get-Process -ErrorAction SilentlyContinue | ForEach-Object {{ $id=[int]$_.Id; $cpuValue=$null; if ($null -ne $_.CPU) {{ $cpuValue=[double]$_.CPU }}; $proc0[$id] = [pscustomobject]@{{ Name=$_.ProcessName; Cpu=$cpuValue }} }}
$cpu = @()
for ($i=0; $i -lt 11; $i++) {{
    $row = Get-CimInstance -ClassName Win32_PerfFormattedData_PerfOS_Processor -Filter "Name='_Total'"
    $cpu += [pscustomobject]@{{ Timestamp=(Get-Date).ToUniversalTime().ToString('o'); InstanceName=$row.Name; PercentProcessorTime=$row.PercentProcessorTime; PercentIdleTime=$row.PercentIdleTime; PercentUserTime=$row.PercentUserTime; PercentPrivilegedTime=$row.PercentPrivilegedTime }}
    if ($i -lt 10) {{ Start-Sleep -Seconds 1 }}
}}
$proc1 = @{{}}
Get-Process -ErrorAction SilentlyContinue | ForEach-Object {{ $id=[int]$_.Id; $cpuValue=$null; if ($null -ne $_.CPU) {{ $cpuValue=[double]$_.CPU }}; $proc1[$id] = [pscustomobject]@{{ Name=$_.ProcessName; Cpu=$cpuValue }} }}
$procWatch.Stop()
$processSeconds = [math]::Max(1.0, $procWatch.Elapsed.TotalSeconds)
$activeProcessCpu = @()
foreach ($id in $proc1.Keys) {{
    $pct = $null
    $baselineAvailable = $false
    if ($proc0.ContainsKey($id) -and $null -ne $proc0[$id].Cpu -and $null -ne $proc1[$id].Cpu) {{
        $baselineAvailable = $true
        $delta = [math]::Max(0, $proc1[$id].Cpu - $proc0[$id].Cpu)
        $pct = $delta / $processSeconds / [double]$logicalCount * 100.0
    }}
    $activeProcessCpu += [pscustomobject]@{{ ProcessId=$id; Name=$proc1[$id].Name; CpuPercentTotal=$pct; BaselineAvailable=$baselineAvailable }}
}}
$activeProcessCpu = @($activeProcessCpu | Sort-Object -Property CpuPercentTotal -Descending)
$chromeAfter = @(Get-Process chrome -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)
$meanCpu = ($cpu | Measure-Object -Property PercentProcessorTime -Average).Average
$maxCpu = ($cpu | Measure-Object -Property PercentProcessorTime -Maximum).Maximum
[ordered]@{{ captured_utc=(Get-Date).ToUniversalTime().ToString('o'); power_plan_guid=$powerGuid; expected_power_plan_guid=$expectedPower; cpu_names=$cpuInfo; logical_processor_count=$logicalCount; expected_cpu_name='{expected_cpu}'; chrome_pids_before=$chromeBefore; chrome_pids_after=$chromeAfter; cpu_samples=$cpu; mean_cpu_percent=$meanCpu; max_cpu_percent=$maxCpu; active_process_count=$activeProcessCpu.Count; active_processes_cpu=$activeProcessCpu; process_sample_seconds=$processSeconds; cpu_sample_interval_seconds=1; cpu_sample_count=$cpu.Count; cpu_load_is_observation_only=$true }} | ConvertTo-Json -Depth 6 -Compress
"""


def capture(name: str, argv: list[str], measurement: bool=False, timeout_sec:float|None=None) -> tuple[int,float]:
    started=datetime.now(timezone.utc).isoformat()
    command=subprocess.list2cmdline(argv)
    t0=time.monotonic()
    try:
        result=subprocess.run(argv,cwd=LOGS,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=False,timeout=timeout_sec)
    except subprocess.TimeoutExpired as exc:
        stdout=exc.stdout or b"";stderr=exc.stderr or b""
        if isinstance(stdout,str):stdout=stdout.encode("utf-8",errors="replace")
        if isinstance(stderr,str):stderr=stderr.encode("utf-8",errors="replace")
        result=subprocess.CompletedProcess(argv,124,stdout,stderr+b"\nTIMEOUT_EXPIRED\n")
    elapsed=time.monotonic()-t0
    (LOGS/f"{name}.command.txt").write_text(command+"\n",encoding="utf-8")
    (LOGS/f"{name}.stdout.txt").write_bytes(result.stdout)
    (LOGS/f"{name}.stderr.txt").write_bytes(result.stderr)
    manifest.append({"name":name,"started_utc":started,"completed_utc":datetime.now(timezone.utc).isoformat(),
                     "elapsed_seconds":round(elapsed,3),"command":command,"exit_code":result.returncode,
                     "stdout_file":f"{name}.stdout.txt","stdout_sha256":hashlib.sha256(result.stdout).hexdigest().upper(),
                     "stderr_file":f"{name}.stderr.txt","stderr_sha256":hashlib.sha256(result.stderr).hexdigest().upper(),
                     "measurement":measurement,"authorization_token_required":measurement})
    manifest_path.write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")
    print(f"{name}: exit={result.returncode}, elapsed={elapsed:.1f}s")
    return result.returncode,elapsed


def refresh_hashes() -> None:
    sums=T0/"SHA256SUMS.txt"
    entries={}
    for line in sums.read_text(encoding="utf-8").splitlines():
        parts=line.split(None,1)
        if len(parts)==2: entries[parts[1].strip()]=parts[0]
    for rel in ("src/capture_phase1_measurements.py","results/T0.0/phase1_measurement_plan.json",
                "results/T0.0/phase1_execution_status.json","results/T0.0/phase1_session_summary.json","logs/COMMANDS.json"):
        entries[rel]=""
    for entry in manifest:
        if entry["name"].startswith("phase1_"):
            for suffix in ("command.txt","stdout.txt","stderr.txt"):entries[f"logs/{entry['name']}.{suffix}"]=""
    for path in RAW.rglob("*"):
        if path.is_file():entries[path.relative_to(T0).as_posix()]=""
    for path in RESULTS.glob("phase1_*"):
        if path.is_file():entries[path.relative_to(T0).as_posix()]=""
    for rel in list(entries):
        path=T0/rel
        if path.is_file():entries[rel]=hashlib.sha256(path.read_bytes()).hexdigest().upper()
        else:del entries[rel]
    sums.write_text("".join(f"{entries[rel]}  {rel}\n" for rel in sorted(entries)),encoding="utf-8")


def write_hold(reason:str,preflight:dict|None=None)->None:
    status={"SESSION":"HOLD","status":"SESSION=HOLD","reason":reason,"authorization_token":"AUTORIZADO","session_segment":session_segment,
            "performance_series_started":False,"preflight":preflight,"parameters_file":"results/T0.0/phase1_measurement_plan.json"}
    (RESULTS/f"phase1_execution_status_{session_segment}.json").write_text(json.dumps(status,indent=2)+"\n",encoding="utf-8")
    (RESULTS/f"phase1_session_summary_{session_segment}.json").write_text(json.dumps(status,indent=2)+"\n",encoding="utf-8")
    refresh_hashes()


def read_prior_series_status()->tuple[dict[str,list[str]]|None,str]:
    if not (RAW/"series_status.csv").is_file():return None,"series_status.csv is missing"
    with (RAW/"series_status.csv").open(newline="",encoding="utf-8-sig") as f:status_rows=list(csv.DictReader(f))
    by_series={}
    for row in status_rows:by_series.setdefault(row["series"],[]).append(row["status"])
    if any(statuses[-1] not in ("SERIES_COMPLETE","SERIES_RECOVERED_TECHNICAL_RETRY") for statuses in by_series.values()):
        return None,"resume found an unresolved/held prior series"
    return by_series,""


def validate_resume_e_core_inputs()->tuple[bool,str]:
    required=("series_status.csv","engineering_calibration_only.csv","p_instr_raw.csv","p_instr_summary.csv",
              "p_q4_m_raw.csv","p_q4_m_summary.csv","p_q4_8p_m1_raw.csv","p_q4_8p_m1_summary.csv",
              "bw_dram_read_8p_raw.csv","bw_dram_read_8p_summary.csv","e_confirm_order.json")
    missing=[name for name in required if not (RAW/name).is_file()]
    if missing:return False,"resume missing required prior-session files: "+", ".join(missing)
    if (RAW/"e_confirm_q4_m1_raw.csv").exists() or (RAW/"e_confirm_q4_m1_summary.csv").exists():
        return False,"resume refuses to overwrite any existing E-core Q4 outputs"
    by_series,error=read_prior_series_status()
    if by_series is None:return False,error
    if len(by_series)!=47:return False,f"resume expected exactly 47 completed prior series; found {len(by_series)}"
    if any(statuses[-1] not in ("SERIES_COMPLETE","SERIES_RECOVERED_TECHNICAL_RETRY") for statuses in by_series.values()):
        return False,"resume found an unresolved/held prior series"
    p_instr=[s for s in by_series if "_P_instr_" in s]
    p_q4=[s for s in by_series if "_P_Q4_" in s and "_P_Q4_8P_m1_" not in s]
    p_8p=[s for s in by_series if s.startswith("P_Q4_8P_m1_")]
    dram=[s for s in by_series if s=="BW_DRAM_read_8P"]
    if (len(p_instr),len(p_q4),len(p_8p),len(dram))!=(32,12,2,1):
        return False,f"resume prior-family count mismatch: P_instr={len(p_instr)}, P_Q4={len(p_q4)}, P_Q4_8P={len(p_8p)}, BW_DRAM={len(dram)}"
    if any("_E_P_Q4_" in s for s in by_series):return False,"resume expected E-core Q4 to be entirely unrun"
    return True,"47 prior complete series and preregistered E-order preserved"


def validate_resume_pcore_m1_inputs()->tuple[bool,str]:
    required=("series_status.csv","engineering_calibration_only.csv","p_instr_raw.csv","p_instr_summary.csv",
              "p_q4_m_raw.csv","p_q4_m_summary.csv","p_q4_8p_m1_raw.csv","p_q4_8p_m1_summary.csv",
              "bw_dram_read_8p_raw.csv","bw_dram_read_8p_summary.csv","e_confirm_order.json",
              "e_confirm_q4_m1_raw.csv","e_confirm_q4_m1_summary.csv")
    missing=[name for name in required if not (RAW/name).is_file()]
    if missing:return False,"resume missing prior-session files: "+", ".join(missing)
    if (RAW/"p_q4_pcore_m1_raw.csv").exists() or (RAW/"p_q4_pcore_m1_summary.csv").exists():
        return False,"resume refuses to overwrite P-core Q4 m1 outputs"
    if (RAW/"effective_frequency_attempt.json").exists() or (RAW/"effective_frequency_attempt1").exists():
        return False,"resume refuses to repeat the effective-frequency attempt"
    by_series,error=read_prior_series_status()
    if by_series is None:return False,error
    if len(by_series)!=63:return False,f"resume expected exactly 63 prior completed series; found {len(by_series)}"
    p_instr=[s for s in by_series if "_P_instr_" in s]
    p_q4=[s for s in by_series if "_P_Q4_" in s and "_P_Q4_8P_m1_" not in s and "_P_Q4_i_m1_" not in s and "_E_P_Q4_" not in s]
    p_8p=[s for s in by_series if s.startswith("P_Q4_8P_m1_")]
    dram=[s for s in by_series if s=="BW_DRAM_read_8P"]
    e_q4=[s for s in by_series if "_E_P_Q4_" in s]
    pcore=[s for s in by_series if "_P_Q4_i_m1_" in s]
    if (len(p_instr),len(p_q4),len(p_8p),len(dram),len(e_q4),len(pcore))!=(32,12,2,1,16,0):
        return False,f"resume family count mismatch: P_instr={len(p_instr)}, P_Q4={len(p_q4)}, P_Q4_8P={len(p_8p)}, BW_DRAM={len(dram)}, E_Q4={len(e_q4)}, Pcore_m1={len(pcore)}"
    if not (RESULTS/"phase1_readiness.json").is_file():return False,"prior tranche derived readiness file is missing"
    return True,"63 prior complete series preserved; only 16 P-core Q4 m1 series remain"


def last_fixed_units(series_key:str)->int:
    with (RAW/"series_status.csv").open(newline="",encoding="utf-8-sig") as f:
        candidates=[r for r in csv.DictReader(f) if r["series"]==series_key and r["status"] in ("SERIES_COMPLETE","SERIES_RECOVERED_TECHNICAL_RETRY")]
    if not candidates:raise RuntimeError(f"no completed P_instr series to source fixed_units: {series_key}")
    return int(candidates[-1]["fixed_units"])


def logical_processor_for_core(core_key:str)->int:
    with TOPOLOGY.open(newline="",encoding="utf-8-sig") as f:
        for row in csv.DictReader(f):
            if row["physical_core_api_key"]==core_key:return int(row["logical_processor"])
    raise RuntimeError(f"core key not in topology CSV: {core_key}")


def read_sep_event_counts(path:Path,worker_name:str)->dict[str,float]:
    text=path.read_text(encoding="utf-8-sig",errors="replace")
    if not text.strip():return {}
    first=text.splitlines()[0];delimiter="\t" if first.count("\t")>first.count(",") else ","
    table=list(csv.DictReader(io.StringIO(text),delimiter=delimiter))
    if not table:return {}
    columns=list(table[0]);process_col=next((c for c in columns if c.strip().lower()=="process"),None)
    selected=[row for row in table if process_col is None or "phase1_measure_harness" in (row.get(process_col) or "").lower()]
    if not selected:selected=table
    result={}
    for metric,needles in {
        "aperf":("APERF",),"mperf":("MPERF",),
        "thread_cycles":("CPU_CLK_UNHALTED.THREAD_P","CPU_CLK_UNHALTED.THREAD","CPU_CLK_UNHALTED.CORE_P","CPU_CLK_UNHALTED.CORE"),
        "reference_cycles":("CPU_CLK_UNHALTED.REF_TSC_P","CPU_CLK_UNHALTED.REF_TSC")}.items():
        matching=[c for c in columns if any(n in c.upper() for n in needles)]
        values=[]
        for row in selected:
            for col in matching:
                try:
                    raw=(row.get(col) or "").strip().replace(",","")
                    if raw:values.append(float(raw))
                except ValueError:pass
        if values:result[metric]=sum(values)
    return result


def run_effective_frequency_attempt()->dict:
    attempt_path=RAW/"effective_frequency_attempt.json"
    if attempt_path.exists():
        raise RuntimeError("frequency attempt artifact already exists; refusing overwrite or second attempt")
    frequency_dir=RAW/"effective_frequency_attempt1";frequency_dir.mkdir(exist_ok=False)
    tsc_hz=None
    static_info=LOGS/"sep_platform_info.stdout.txt"
    if static_info.is_file():
        m=re.search(r"TSC Freq\s+([0-9.]+)\s*MHz",static_info.read_text(encoding="utf-8",errors="replace"))
        if m:tsc_hz=float(m.group(1))*1e6
    target_specs=[("P_ref","CORE_API_00_G0_MASK_0_1"),("E_representative","CORE_API_08_G0_MASK_16")]
    profiles=[]
    overall_sources=[]
    for label,core_key in target_specs:
        profile={"label":label,"worker_key":core_key,"route":"AVX2","collector":"SEP -p-state","status":"UNAVAILABLE_WITH_QUALIFIED_TOOLS"}
        marker=frequency_dir/f"{label}_P_instr_marker.json"
        prefix=frequency_dir/f"sep_pstate_{label}"
        result_dir=frequency_dir/f"vtune_import_{label}"
        hw_csv=frequency_dir/f"hw_events_{label}.csv"
        try:
            fixed=last_fixed_units(core_key+"_P_instr_AVX2")
            profile["fixed_units_from_completed_P_instr_series"]=fixed
            app_args=subprocess.list2cmdline(["--run-frequency-pinstr",str(TOPOLOGY),core_key,str(fixed),str(marker)])
            logical_processor=logical_processor_for_core(core_key)
            profile["logical_processor"] = logical_processor
            sep_args=[str(SEP),"-start","-p-state","-cpu-mask",str(logical_processor),"-app",str(EXE),"-args",app_args,"-out",str(prefix)]
            if not SEP.is_file():
                profile["reason"]="SEP executable unavailable"
            else:
                code,elapsed=capture(f"phase1_frequency_sep_{label}",sep_args,measurement=True,timeout_sec=90)
                profile["sep_exit_code"]=code;profile["sep_elapsed_seconds"]=round(elapsed,3)
                candidates=sorted(frequency_dir.glob(prefix.name+"*.tb7"))
                profile["tb7_files"]=[str(p) for p in candidates]
                if code!=0 or not candidates:
                    profile["reason"]="SEP p-state profile did not produce a successful TB7 file"
                else:
                    import_code,_=capture(f"phase1_frequency_vtune_import_{label}",[str(VTUNE),"-import",str(candidates[0]),"-r",str(result_dir)],measurement=False,timeout_sec=120)
                    profile["vtune_import_exit_code"]=import_code
                    if import_code==0:
                        report_code,_=capture(f"phase1_frequency_hw_events_{label}",[str(VTUNE),"-report","hw-events","-result-dir",str(result_dir),"-format","csv","-report-output",str(hw_csv)],measurement=False,timeout_sec=120)
                        profile["hw_events_report_exit_code"]=report_code
                        if report_code==0 and hw_csv.is_file():
                            counters=read_sep_event_counts(hw_csv,EXE.name)
                            profile["event_counts"]=counters
                            marker_data=json.loads(marker.read_text(encoding="utf-8")) if marker.is_file() else {}
                            profile["marker"]=marker_data
                            aperf=counters.get("aperf");mperf=counters.get("mperf")
                            thread=counters.get("thread_cycles");ref=counters.get("reference_cycles")
                            if aperf is not None and mperf is not None and aperf>0 and mperf>0:
                                profile["status"]="MEASURED";profile["source_event"]="SEP P-state APERF/MPERF hardware counters"
                                profile["effective_frequency_ratio"]=aperf/mperf
                                profile["frequency_unit"]="ratio of actual to reference frequency; not converted to GHz"
                                profile["formula"]="effective_frequency_ratio = summed_APERF_cycles / summed_MPERF_reference_cycles"
                                overall_sources.append("APERF/MPERF")
                            elif thread is not None and ref is not None and thread>0 and ref>0 and tsc_hz:
                                profile["status"]="MEASURED";profile["source_event"]="SEP CPU_CLK_UNHALTED.THREAD + CPU_CLK_UNHALTED.REF_TSC"
                                profile["formula"]="effective_GHz = (THREAD_cycles / REF_TSC_cycles) * SEP_TSC_frequency_GHz"
                                profile["effective_frequency_GHz"]=(thread/ref)*(tsc_hz/1e9)
                                overall_sources.append("CPU_CLK_UNHALTED/REF_TSC")
                            else:
                                profile["reason"]="VTune-imported SEP data did not expose usable APERF/MPERF or THREAD/REF_TSC counts"
                        else:profile["reason"]="VTune hw-events export unavailable"
                    else:profile["reason"]="VTune could not import SEP TB7 data"
        except Exception as exc:
            profile["reason"]=f"frequency attempt error: {type(exc).__name__}: {exc}"
        profiles.append(profile)
    measured=all(p["status"]=="MEASURED" for p in profiles)
    result={"attempt_count":1,"status":"MEASURED" if measured else "UNAVAILABLE_WITH_QUALIFIED_TOOLS",
            "tool":"Intel SEP 5.61.22 -p-state; VTune 2026.4 hw-events import/report",
            "p_state_capability":"SEP help documents APERF/MPERF and fixed unhalted/reference counters",
            "TSC_frequency_hz_from_SEP_static_platform_info":tsc_hz,"measured_sources":overall_sources,"targets":profiles,
            "no_retries_or_tuning":True,"postprocessing_only":True}
    attempt_path.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    return result


if raw_occupied and not resume_mode:
    write_hold(f"Refusing to overwrite existing Phase 1 raw outputs: {RAW}")
    raise SystemExit("SESSION=HOLD; existing raw outputs preserved; no series started.")
if resume_mode:
    resume_ok,resume_reason=(validate_resume_e_core_inputs() if resume_e_core else validate_resume_pcore_m1_inputs())
    if not resume_ok:
        write_hold(resume_reason,{"resume_mode":True})
        raise SystemExit("SESSION=HOLD; resume prerequisites failed; no series started.")

preflight_name=f"phase1_preflight_{session_segment}"
preflight_code,preflight_seconds=capture(preflight_name,[str(PS),"-NoProfile","-NonInteractive","-Command",background_ps])
try:
    preflight=json.loads((LOGS/f"{preflight_name}.stdout.txt").read_text(encoding="utf-8"))
except Exception as exc:
    write_hold("preflight did not return valid JSON",{"exit_code":preflight_code,"error":str(exc)})
    raise SystemExit("SESSION=HOLD; preflight invalid; no series started.")
topology_hash=hashlib.sha256(TOPOLOGY.read_bytes()).hexdigest().upper()
chrome_running=bool(preflight.get("chrome_pids_before") or preflight.get("chrome_pids_after"))
power_ok=preflight.get("power_plan_guid")==expected_power_guid
cpu_ok=preflight.get("cpu_names")==[expected_cpu] and preflight.get("logical_processor_count")==24
topology_ok=topology_hash==expected_topology_sha256
background_recorded=("mean_cpu_percent" in preflight and "max_cpu_percent" in preflight and "active_processes_cpu" in preflight)
preflight_ok=(preflight_code==0 and power_ok and cpu_ok and topology_ok and not chrome_running)
preflight.update({"topology_sha256":topology_hash,"checks":{"chrome_absent":not chrome_running,"power_plan_matches":power_ok,
                  "hardware_matches":cpu_ok,"topology_matches":topology_ok,"background_load_recorded":background_recorded,
                  "background_load_is_observation_only":True},"session_preflight_pass":preflight_ok})
if not preflight_ok:
    write_hold("one or more frozen session preconditions failed",preflight)
    raise SystemExit("SESSION=HOLD; conditions failed; no timed series started.")

order_code=None;order_seconds=0.0;order_check_code=None
if resume_e_core:
    order_check_code,order_check_seconds=capture("phase1_resume_check_e_order",[str(EXE),"--check-e-order",str(TOPOLOGY),str(RAW)],measurement=False)
    try:
        order_check=json.loads((LOGS/"phase1_resume_check_e_order.stdout.txt").read_text(encoding="utf-8"))
        route_pairs=order_check.get("route_pairs",[])
        expected_pairs=[["AVX2","AVX-VNNI-256"] if i%2==0 else ["AVX-VNNI-256","AVX2"] for i in range(8)]
        order_ok=(order_check_code==0 and order_check.get("record")=="preregistered_e_core_order_check" and
                  order_check.get("core_count")==8 and len(order_check.get("worker_order",[]))==8 and route_pairs==expected_pairs)
    except Exception:
        order_ok=False
    if not order_ok:
        write_hold("preregistered E-order verification failed during resume",{"resume_mode":True,"order_check_exit_code":order_check_code})
        raise SystemExit("SESSION=HOLD; E-order check failed; no E-core series started.")
    commands=[("phase1_resume_e_core_confirm",[str(EXE),"--run-e-confirm",str(TOPOLOGY),str(RAW)])]
elif resume_pcore_m1:
    if not (RAW/"p_core_m1_order.json").is_file():
        order_code,order_seconds=capture("phase1_preregister_pcore_m1_order",[str(EXE),"--prepare-pcore-m1-order",str(TOPOLOGY),str(RAW)],measurement=False)
        if order_code!=0 or not (RAW/"p_core_m1_order.json").is_file():
            write_hold("could not preregister P-core m1 order before the first remaining series",{"preflight":preflight,"order_exit_code":order_code})
            raise SystemExit("SESSION=HOLD; P-core m1 order was not preregistered.")
    order_check_code,order_check_seconds=capture("phase1_resume_check_pcore_m1_order",[str(EXE),"--check-pcore-m1-order",str(TOPOLOGY),str(RAW)],measurement=False)
    try:
        order_check=json.loads((LOGS/"phase1_resume_check_pcore_m1_order.stdout.txt").read_text(encoding="utf-8"))
        route_pairs=order_check.get("route_pairs",[])
        expected_pairs=[["AVX2","AVX-VNNI-256"] if i%2==0 else ["AVX-VNNI-256","AVX2"] for i in range(8)]
        order_ok=(order_check_code==0 and order_check.get("record")=="preregistered_pcore_m1_order_check" and
                  order_check.get("core_count")==8 and len(order_check.get("worker_order",[]))==8 and route_pairs==expected_pairs)
    except Exception:
        order_ok=False
    if not order_ok:
        write_hold("preregistered P-core m1 order verification failed",{"resume_mode":True,"order_check_exit_code":order_check_code})
        raise SystemExit("SESSION=HOLD; P-core m1 order check failed; no series started.")
    commands=[("phase1_resume_pcore_m1",[str(EXE),"--run-pcore-m1",str(TOPOLOGY),str(RAW)])]
else:
    RAW.mkdir(exist_ok=True)
    order_code,order_seconds=capture("phase1_preregister_e_core_order",[str(EXE),"--prepare-e-order",str(TOPOLOGY),str(RAW)],measurement=False)
    if order_code!=0 or not (RAW/"e_confirm_order.json").is_file():
        write_hold("could not preregister E-core order before the first series",{"preflight":preflight,"order_exit_code":order_code})
        raise SystemExit("SESSION=HOLD; E-core order was not preregistered.")
    commands=[
        ("phase1_run_p_instr",[str(EXE),"--run-instr",str(TOPOLOGY),str(RAW)]),
        ("phase1_run_p_q4_m",[str(EXE),"--run-q4-m",str(TOPOLOGY),str(RAW)]),
        ("phase1_run_p_q4_8p_m1",[str(EXE),"--run-q4-8p-m1",str(TOPOLOGY),str(RAW)]),
        ("phase1_run_dram_read_8p",[str(EXE),"--run-dram-read-8p",str(TOPOLOGY),str(RAW)]),
        ("phase1_run_e_core_confirm",[str(EXE),"--run-e-confirm",str(TOPOLOGY),str(RAW)]),
    ]
run_status=[]
for name,argv in commands:
    code,elapsed=capture(name,argv,measurement=True)
    run_status.append({"name":name,"exit_code":code,"elapsed_seconds":round(elapsed,3)})
    if code!=0:break

measurements_complete=len(run_status)==len(commands) and all(row["exit_code"]==0 for row in run_status)
frequency_result=None
if measurements_complete and resume_pcore_m1:
    try:
        frequency_result=run_effective_frequency_attempt()
    except Exception as exc:
        frequency_result={"attempt_count":1,"status":"UNAVAILABLE_WITH_QUALIFIED_TOOLS",
                          "reason":f"bounded frequency attempt could not be completed: {type(exc).__name__}: {exc}",
                          "no_retries_or_tuning":True}
        (RAW/"effective_frequency_attempt.json").write_text(json.dumps(frequency_result,indent=2)+"\n",encoding="utf-8")
derive_code=None
derive_seconds=0.0
if measurements_complete:
    derive_code,derive_seconds=capture("phase1_derive_tables",[sys.executable,str(T0/"src"/"summarize_phase1.py")],measurement=False)
complete=measurements_complete and derive_code==0
status={"SESSION":"COMPLETE" if complete else "HOLD","status":"COMPLETE" if complete else ("SERIES_HOLD_OR_COMMAND_FAILED" if not measurements_complete else "DERIVATION_FAILED"),
        "authorization_token":"AUTORIZADO","preflight":preflight,"preflight_elapsed_seconds":round(preflight_seconds,3),
        "session_segment":session_segment,
        "e_order_preregistration_exit_code":order_code,"e_order_preregistration_elapsed_seconds":round(order_seconds,3),
         "resume_e_core_only":resume_e_core,"resume_pcore_m1_only":resume_pcore_m1,
         "prior_completed_series_preserved":47 if resume_e_core else (63 if resume_pcore_m1 else 0),
         "e_order_resume_check_exit_code":order_check_code if resume_e_core else None,
         "pcore_m1_order_resume_check_exit_code":order_check_code if resume_pcore_m1 else None,
         "effective_frequency_attempt":frequency_result,
         "series":run_status,"derive_tables_exit_code":derive_code,"derive_tables_elapsed_seconds":round(derive_seconds,3),
        "no_performance_samples_replaced":True,"parameters_file":"results/T0.0/phase1_measurement_plan.json"}
(RESULTS/f"phase1_execution_status_{session_segment}.json").write_text(json.dumps(status,indent=2)+"\n",encoding="utf-8")
(RESULTS/f"phase1_session_summary_{session_segment}.json").write_text(json.dumps(status,indent=2)+"\n",encoding="utf-8")
refresh_hashes()
print(json.dumps(status))
if not complete:raise SystemExit(3)
