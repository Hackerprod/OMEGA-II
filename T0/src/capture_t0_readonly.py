"""Read-only T0.0 enumeration plus CPU correctness capture. No performance collector/workload or process control."""
from __future__ import annotations

import hashlib
import json
import os
import subprocess
from datetime import datetime, timezone
from pathlib import Path

T0 = Path(__file__).resolve().parent.parent
SRC = T0 / "src"
BUILD = T0 / "build"
OUT = T0 / "logs"
RESULTS = T0 / "results" / "T0.0"
EJ = Path(r"C:\Projects\OMEGA\Ejercise")
PROBE = BUILD / "out" / "t0_probe.exe"
Q4_CORRECTNESS = BUILD / "out" / "q4_correctness.exe"
MEASURE_HARNESS = BUILD / "out" / "t0_measure_harness.exe"
Q4_CORRECTNESS = BUILD / "out" / "q4_correctness.exe"
VTUNE = Path(r"C:\Program Files (x86)\Intel\oneAPI\vtune\2026.4\bin64\vtune.exe")
SEP = VTUNE.with_name("sep.exe")
PS = Path(r"C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe")
CMD = Path(r"C:\Windows\System32\cmd.exe")
SYSTEMINFO = Path(r"C:\Windows\System32\systeminfo.exe")

ps_processes = r"Get-CimInstance Win32_Process | Sort-Object ProcessId | Select-Object ProcessId,Name,ExecutablePath | ConvertTo-Json -Depth 4"
ps_inventory = r"'### COMPUTER'; Get-CimInstance Win32_ComputerSystem | Select-Object Manufacturer,Model,HypervisorPresent,NumberOfLogicalProcessors,NumberOfProcessors,TotalPhysicalMemory | ConvertTo-Json -Depth 4; '### PROCESSOR'; Get-CimInstance Win32_Processor | Select-Object Name,Manufacturer,Description,NumberOfCores,NumberOfLogicalProcessors,ProcessorId,Family,VirtualizationFirmwareEnabled,L2CacheSize,L3CacheSize | ConvertTo-Json -Depth 4; '### PHYSICAL MEMORY'; Get-CimInstance Win32_PhysicalMemory | Select-Object DeviceLocator,Capacity,Speed,ConfiguredClockSpeed,Manufacturer,PartNumber,SMBIOSMemoryType | ConvertTo-Json -Depth 4; '### MEMORY ARRAY'; Get-CimInstance Win32_PhysicalMemoryArray | Select-Object MemoryDevices,MaxCapacity,Location,Use | ConvertTo-Json -Depth 4; '### OS'; Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber | ConvertTo-Json -Depth 4; '### POWER PLAN'; powercfg.exe /getactivescheme; '### PROCESSOR POWER SETTINGS'; powercfg.exe /qh SCHEME_CURRENT SUB_PROCESSOR; '### GPU INVENTORY ONLY'; Get-CimInstance Win32_VideoController | Select-Object Name,AdapterRAM,DriverVersion | ConvertTo-Json -Depth 4"
ps_memory_full = r"Get-CimInstance Win32_PhysicalMemory | Sort-Object DeviceLocator | Format-List *"
ps_memory_array_full = r"Get-CimInstance Win32_PhysicalMemoryArray | Sort-Object MemoryArrayLocation | Format-List *"
ps_pdh_counters = r"Get-Counter -ListSet * -ErrorAction SilentlyContinue | Where-Object {$_.CounterSetName -match 'Processor Information|Processor'} | Select-Object CounterSetName,Counter | ConvertTo-Json -Depth 4"
ps_counter_tools = r"'### VTUNE COMMAND'; Get-Command vtune.exe -ErrorAction SilentlyContinue | Select-Object Name,Source,Path | Format-List; '### SEP COMMAND'; Get-Command sep.exe -ErrorAction SilentlyContinue | Select-Object Name,Source,Path | Format-List; '### PCM COMMANDS'; Get-Command pcm-memory.exe,pcm-raw.exe,pcm.exe -ErrorAction SilentlyContinue | Select-Object Name,Source,Path | Format-List; '### WHERE PCM IN PATH'; where.exe pcm-memory.exe; where.exe pcm-raw.exe; '### PCM FILES IN STANDARD C INSTALL ROOTS'; where.exe /r 'C:\Program Files' pcm*.exe; where.exe /r 'C:\Program Files (x86)' pcm*.exe; where.exe /r 'C:\ProgramData' pcm*.exe; '### PCM UNINSTALL'; $roots=@('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*','HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*'); Get-ItemProperty $roots -ErrorAction SilentlyContinue | Where-Object {$_.DisplayName -match 'Intel.*PCM|Performance Counter Monitor'} | Select-Object DisplayName,DisplayVersion,Publisher,InstallLocation | Format-List"

records: list[dict] = []


def capture(name: str, argv: list[str], env_override: dict[str, str] | None = None) -> bytes:
    started = datetime.now(timezone.utc).isoformat()
    command_text = subprocess.list2cmdline(argv)
    env = os.environ.copy()
    if env_override:
        env.update(env_override)
    result = subprocess.run(argv, cwd=OUT, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    (OUT / f"{name}.command.txt").write_text(command_text + "\n", encoding="utf-8")
    (OUT / f"{name}.stdout.txt").write_bytes(result.stdout)
    (OUT / f"{name}.stderr.txt").write_bytes(result.stderr)
    records.append({"name": name, "started_utc": started, "command": command_text,
                    "environment_overrides": env_override or {}, "exit_code": result.returncode,
                    "stdout_file": f"{name}.stdout.txt", "stdout_sha256": hashlib.sha256(result.stdout).hexdigest().upper(),
                    "stderr_file": f"{name}.stderr.txt", "stderr_sha256": hashlib.sha256(result.stderr).hexdigest().upper()})
    return result.stdout


capture("build_probe", [str(CMD), "/c", "call", str(BUILD / "build_probe.cmd")])
capture("build_q4_correctness", [str(CMD), "/c", "call", str(BUILD / "build_q4_correctness.cmd")])
capture("build_measurement_harness", [str(CMD), "/c", "call", str(BUILD / "build_measure_harness.cmd")])
topology_raw = capture("topology_api", [str(PROBE), "--topology"])
capture("isa_cpuid_xgetbv", [str(PROBE), "--isa"])
csv_start = topology_raw.find(b"processor_group,logical_processor,")
if csv_start < 0:
    raise RuntimeError("topology helper did not emit its CSV header")
(RESULTS / "b_topologia.csv").write_bytes(topology_raw[csv_start:])
capture("measurement_harness_plan_only", [str(MEASURE_HARNESS), "--plan", str(RESULTS / "b_topologia.csv")])
csv_lines = topology_raw.decode("utf-8", errors="replace").splitlines()
header_index = next(i for i, line in enumerate(csv_lines) if line.startswith("processor_group,logical_processor,"))
headers = csv_lines[header_index].split(",")
timer_workers = []
seen_physical_cores = set()
for line in csv_lines[header_index + 1:]:
    cells = line.split(",")
    row = dict(zip(headers, cells))
    physical_key = row.get("physical_core_api_key")
    if physical_key not in seen_physical_cores and row.get("core_type") in ("P-core", "E-core"):
        timer_workers.append(row)
        seen_physical_cores.add(physical_key)
if not timer_workers:
    timer_out = b"No physical core classified by live topology/API crosswalk; timer helper not launched.\n"
    (OUT / "timer_affinity_resolution.blocked.txt").write_bytes(timer_out)
    records.append({"name":"timer_affinity_resolution","started_utc":datetime.now(timezone.utc).isoformat(),
                    "command":"derived from topology_api stdout; no timer command run", "exit_code":None,
                    "stdout_file":"timer_affinity_resolution.blocked.txt",
                    "stdout_sha256":hashlib.sha256(timer_out).hexdigest().upper(),
                    "stderr_file":None,"stderr_sha256":hashlib.sha256(b"").hexdigest().upper()})
else:
    for row in timer_workers:
        core_suffix = row["physical_core_api_key"].replace(":", "_").replace("/", "_")
        timer_name = f"timer_{core_suffix}_{row['core_type'].replace('-', '')}"
        capture(timer_name, [str(PROBE), "--timer", row["processor_group"], row["logical_processor"]])

capture("powershell_process_image_snapshot", [str(PS), "-NoProfile", "-NonInteractive", "-Command", ps_processes])
capture("powershell_platform_static_inventory", [str(PS), "-NoProfile", "-NonInteractive", "-Command", ps_inventory])
capture("powershell_win32_physical_memory_full", [str(PS), "-NoProfile", "-NonInteractive", "-Command", ps_memory_full])
capture("powershell_win32_physical_memory_array_full", [str(PS), "-NoProfile", "-NonInteractive", "-Command", ps_memory_array_full])
capture("powershell_pdh_processor_counter_catalog", [str(PS), "-NoProfile", "-NonInteractive", "-Command", ps_pdh_counters])
capture("systeminfo_bare_metal_readonly", [str(SYSTEMINFO)])
capture("powershell_counter_tools_readonly_availability", [str(PS), "-NoProfile", "-NonInteractive", "-Command", ps_counter_tools])

capture("vtune_version", [str(VTUNE), "-version"])
capture("vtune_memory_access_analysis_help", [str(VTUNE), "-help", "collect", "memory-access"])
capture("vtune_runsa_event_enumeration", [str(VTUNE), "-collect-with", "runsa", "-knob", "event-config=?"])
capture("sep_version", [str(SEP), "-version"])
capture("sep_platform_info", [str(SEP), "-platform-info"])
capture("sep_platform_info_dimms", [str(SEP), "-platform-info"])
capture("sep_pmu_types_available", [str(SEP), "-pmu-types", "available"])
capture("sep_pcore_events", [str(SEP), "-el", "p-core", "-desc"])
capture("sep_platform_events", [str(SEP), "-el", "-desc"])
capture("sep_memory_access_atype", [str(SEP), "-atypelist", "-details", "memory-access"])
capture("sep_analysis_types", [str(SEP), "-atypelist"])
q4_raw = capture("q4_correctness_cpu_only", [str(Q4_CORRECTNESS)])
(RESULTS / "q4_correctness_raw.jsonl").write_bytes(q4_raw)
q4_raw = capture("q4_correctness_cpu_only", [str(Q4_CORRECTNESS)])
(RESULTS / "q4_correctness_raw.jsonl").write_bytes(q4_raw)

(OUT / "COMMANDS.json").write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
