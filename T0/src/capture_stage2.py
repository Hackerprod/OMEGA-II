"""Run authorized T0.0 stage-2 measurements sequentially, preserving every command and raw output."""
from __future__ import annotations

import csv
import hashlib
import json
import re
import statistics
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

T0 = Path(__file__).resolve().parent.parent
LOGS = T0 / "logs"
RESULTS = T0 / "results" / "T0.0"
RAW = RESULTS / "stage2_raw"
EXE = T0 / "build" / "out" / "t0_measure_harness.exe"
TOPOLOGY = RESULTS / "b_topologia.csv"
PS = Path(r"C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe")
WORKERS = {"P": 8, "E": 8, "MIXED": 16}
DURATION_SECONDS = 120
RESUME_I = "--resume-i" in sys.argv
FINALIZE_ONLY = "--finalize-only" in sys.argv

if not RESULTS.is_dir():
    raise SystemExit(f"Missing results directory: {RESULTS}")
if RAW.exists() and any(item.is_file() for item in RAW.rglob("*")) and not (RESUME_I or FINALIZE_ONLY):
    raise SystemExit(f"Refusing to overwrite prior stage-2 output: {RAW}")
if (RESUME_I or FINALIZE_ONLY) and not RAW.exists():
    raise SystemExit("Cannot resume dynamic i: stage2_raw output directory is missing.")
RAW.mkdir(exist_ok=True)
for worker_set in WORKERS:
    (RAW / f"i_{worker_set}").mkdir(exist_ok=True)

manifest_path = LOGS / "COMMANDS.json"
manifest = json.loads(manifest_path.read_text(encoding="utf-8"))


def capture(name: str, argv: list[str], timeout: int | None = None) -> int:
    started = datetime.now(timezone.utc).isoformat()
    t0 = time.monotonic()
    command_text = subprocess.list2cmdline(argv)
    result = subprocess.run(argv, cwd=LOGS, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            check=False, timeout=timeout)
    elapsed = time.monotonic() - t0
    (LOGS / f"{name}.command.txt").write_text(command_text + "\n", encoding="utf-8")
    (LOGS / f"{name}.stdout.txt").write_bytes(result.stdout)
    (LOGS / f"{name}.stderr.txt").write_bytes(result.stderr)
    manifest.append({
        "name": name, "started_utc": started,
        "completed_utc": datetime.now(timezone.utc).isoformat(),
        "elapsed_seconds": round(elapsed, 3), "command": command_text,
        "exit_code": result.returncode,
        "stdout_file": f"{name}.stdout.txt",
        "stdout_sha256": hashlib.sha256(result.stdout).hexdigest().upper(),
        "stderr_file": f"{name}.stderr.txt",
        "stderr_sha256": hashlib.sha256(result.stderr).hexdigest().upper(),
    })
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"{name}: exit={result.returncode}, elapsed={elapsed:.1f}s")
    return result.returncode


def refresh_checksums() -> None:
    sums_path = T0 / "SHA256SUMS.txt"
    entries: dict[str, str] = {}
    for line in sums_path.read_text(encoding="utf-8").splitlines():
        digest, relative = line.split(None, 1)
        entries[relative.strip()] = digest
    for relative in ("src/capture_stage2.py", "src/i_dynamic_probe.ps1"):
        entries[relative] = ""
    for item in RAW.rglob("*"):
        if item.is_file():
            entries[item.relative_to(T0).as_posix()] = ""
    for relative in ("results/T0.0/stage2_measurement_config.json", "results/T0.0/stage2_execution_status.json",
                     "results/T0.0/e.md", "results/T0.0/d_vi.csv", "results/T0.0/d_vi_resumen.csv",
                     "results/T0.0/f_ancho_banda.csv", "results/T0.0/i_condiciones.md",
                     "results/T0.0/measurement_harness_plan.md"):
        item = T0 / relative
        if item.is_file():
            entries[relative] = ""
    for entry in manifest:
        for key in ("stdout_file", "stderr_file"):
            entries[f"logs/{entry[key]}"] = ""
        if entry["name"].startswith("stage2_"):
            entries[f"logs/{entry['name']}.command.txt"] = ""
    for relative in sorted(entries):
        item = T0 / relative
        if item.is_file():
            entries[relative] = hashlib.sha256(item.read_bytes()).hexdigest().upper()
    sums_path.write_text("".join(f"{entries[name]}  {name}\n" for name in sorted(entries)), encoding="utf-8")


background_ps = r"""
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


def do_background(phase: str) -> bool:
    return capture(f"stage2_background_{phase}_cim", [str(PS), "-NoProfile", "-NonInteractive", "-Command", background_ps], timeout=60) == 0


config = {
    "authorized_stage": "T0.0 stage 2",
    "recorded_utc": datetime.now(timezone.utc).isoformat(),
    "measurement_order": ["e", "d", "f", "i_dynamic_P", "i_dynamic_E", "i_dynamic_MIXED"],
    "N": 31, "warmup_count": 5, "statistic": "median", "dispersion": ["IQR", "MAD"],
    "outlier_discard_count": 0, "KERNEL_CALLS_PER_SAMPLE": 128,
    "BW_D_n": [1, 2, 4, 8, 16], "BW_D_buffer_bytes_per_worker": 125829120,
    "worker_sets": {"A": "4 P-cores", "B": "8 P-cores", "C": "8 P + 8 E cores"},
    "dynamic_load": {"duration_seconds_each": DURATION_SECONDS, "worker_counts": WORKERS},
    "dynamic_order": ["P", "E", "MIXED"],
    "logical_bytes_caveat": "BW_L2 and r are logical Q4 bytes; not validated as physical traffic",
}
config_path = RESULTS / "stage2_measurement_config.json"
if RESUME_I or FINALIZE_ONLY:
    if not config_path.is_file():
        raise SystemExit("Cannot resume dynamic i without the frozen stage-2 configuration.")
    config = json.loads(config_path.read_text(encoding="utf-8"))
    prior = {entry["name"]: entry for entry in manifest}
    prior_names = ["stage2_run_e", "stage2_run_d", "stage2_run_f", "stage2_i_dynamic_P"]
    if FINALIZE_ONLY:
        prior_names.extend(["stage2_i_dynamic_E", "stage2_i_dynamic_MIXED", "stage2_background_after_cim"])
    for name in prior_names:
        if name not in prior or prior[name].get("exit_code") != 0:
            raise SystemExit(f"Cannot resume; required prior command did not succeed: {name}")
    sets_to_check = list(WORKERS) if FINALIZE_ONLY else ["P"]
    for worker_set in sets_to_check:
        count = WORKERS[worker_set]
        outdir = RAW / f"i_{worker_set}"
        record = json.loads((outdir / "dynamic_condition_record.json").read_text(encoding="utf-8-sig"))
        expected_footer = f"dynamic_load_complete worker_set={worker_set} worker_count={count} seconds={DURATION_SECONDS} route=AVX2"
        if record.get("process_exit_code") not in (0, None) or expected_footer not in (outdir / "controlled_load.stdout.txt").read_text(encoding="utf-8"):
            raise SystemExit(f"Cannot continue; completed {worker_set} dynamic-load footer/exit status does not validate.")
else:
    if not do_background("before"):
        raise SystemExit("Pre-measurement background capture failed; no measurements started.")
    config_path.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")

failed_at: str | None = None
if not RESUME_I and not FINALIZE_ONLY:
    for label, mode in (("e", "--run-e"), ("d", "--run-d"), ("f", "--run-f")):
        code = capture(f"stage2_run_{label}", [str(EXE), mode, str(TOPOLOGY), str(RAW)], timeout=None)
        if code != 0:
            failed_at = label
            break

if failed_at is None and not FINALIZE_ONLY:
    remaining_workers = list(WORKERS.items())[1:] if RESUME_I else list(WORKERS.items())
    for worker_set, count in remaining_workers:
        outdir = RAW / f"i_{worker_set}"
        code = capture(
            f"stage2_i_dynamic_{worker_set}",
            [str(PS), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
             "-File", str(T0 / "src" / "i_dynamic_probe.ps1"),
             "-WorkerSet", worker_set, "-WorkerCount", str(count),
             "-DurationSeconds", str(DURATION_SECONDS), "-OutputDirectory", str(outdir)],
            timeout=DURATION_SECONDS + 300,
        )
        record_path = outdir / "dynamic_condition_record.json"
        record = json.loads(record_path.read_text(encoding="utf-8-sig")) if record_path.exists() else {}
        footer = f"dynamic_load_complete worker_set={worker_set} worker_count={count} seconds={DURATION_SECONDS} route=AVX2"
        stdout_path = outdir / "controlled_load.stdout.txt"
        stdout_text = stdout_path.read_text(encoding="utf-8") if stdout_path.exists() else ""
        if code != 0 or record.get("process_exit_code") not in (0, None) or footer not in stdout_text:
            failed_at = f"i_{worker_set}"
            break

post_ok = (next(entry.get("exit_code") == 0 for entry in manifest if entry["name"] == "stage2_background_after_cim")
           if FINALIZE_ONLY else do_background("after"))
if failed_at is not None:
    summary = {"status": "STOPPED_AFTER_NONZERO_SERIES", "failed_at": failed_at,
               "post_background_capture_ok": post_ok, "measurements_repeated": False}
    (RESULTS / "stage2_execution_status.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    refresh_checksums()
    print(f"STOPPED after nonzero series {failed_at}; no rerun performed.")
    sys.exit(3)


def read_summary(path: Path) -> dict[str, str]:
    with path.open(newline="", encoding="utf-8-sig") as f:
        return next(csv.DictReader(f))


core_type = {}
with TOPOLOGY.open(newline="", encoding="utf-8-sig") as f:
    for row in csv.DictReader(f):
        core_type[row["physical_core_api_key"]] = row["core_type"]

# d_vi.csv and its consolidated summary
d_rows = []
for path in sorted(RAW.glob("d_vi_*_summary.csv")):
    row = read_summary(path)
    key = row["series"].removeprefix("d_vi_")
    d_rows.append({"worker_key": key, "worker_type": core_type.get(key, "UNMAPPED"), **row})
with (RESULTS / "d_vi.csv").open("w", newline="", encoding="utf-8") as f:
    w = csv.DictWriter(f, fieldnames=list(d_rows[0]))
    w.writeheader(); w.writerows(d_rows)
with (RESULTS / "d_vi_resumen.csv").open("w", newline="", encoding="utf-8") as f:
    d_groups = {}
    for r in d_rows:
        d_groups.setdefault(r["worker_type"], []).append(float(r["median"]))
    summary_rows = []
    for kind, values in sorted(d_groups.items()):
        ordered = sorted(values)
        med = statistics.median(ordered)
        q1 = ordered[int(0.25 * (len(ordered) - 1))]
        q3 = ordered[int(0.75 * (len(ordered) - 1))]
        deviations = [abs(x - med) for x in ordered]
        summary_rows.append({"worker_type": kind, "physical_core_count": len(values),
                             "median_of_core_medians_MAC_per_second": med, "iqr_of_core_medians": q3-q1,
                             "mad_of_core_medians": statistics.median(deviations),
                             "min_core_median": min(values), "max_core_median": max(values),
                             "N_per_core": 31, "warmup_count": 5, "outlier_discard_count": 0})
    w = csv.DictWriter(f, fieldnames=list(summary_rows[0]))
    w.writeheader(); w.writerows(summary_rows)

# e peak by worker type and ISA route: highest per-core median, with its core and dispersion.
e_rows = []
for path in sorted(RAW.glob("e_pico_*_summary.csv")):
    row = read_summary(path)
    series = row["series"]
    if series.startswith("e_pico_vnni256_"):
        key = series[len("e_pico_vnni256_"):]
        route = "AVX-VNNI-256"
    elif series.startswith("e_pico_avx2_"):
        key = series[len("e_pico_avx2_"):]
        route = "AVX2"
    else:
        raise RuntimeError(f"unrecognized e-series name: {series}")
    e_rows.append({"worker_type": core_type.get(key, "UNMAPPED"), "route": route, "worker_key": key, **row})
peaks = {}
for row in e_rows:
    key = (row["worker_type"], row["route"])
    if key not in peaks or float(row["median"]) > float(peaks[key]["median"]):
        peaks[key] = row
lines = ["# T0.0-e — Pico observado por ruta y tipo de núcleo", "",
         "Pico = mayor mediana de throughput entre los núcleos del tipo indicado; sin descarte de outliers.",
         "Todos los valores proceden de las series N=31 del harness.", "",
         "| Tipo | Ruta | Núcleo pico | Mediana MAC/s | IQR | MAD | Mín. | Máx. |", "|---|---|---|---:|---:|---:|---:|---:|"]
for key in sorted(peaks):
    r = peaks[key]
    lines.append(f"| {key[0]} | {key[1]} | {r['worker_key']} | {float(r['median']):.6g} | {float(r['iqr']):.6g} | {float(r['mad']):.6g} | {float(r['min']):.6g} | {float(r['max']):.6g} |")
(RESULTS / "e.md").write_text("\n".join(lines) + "\n", encoding="utf-8")

# f_ancho_banda.csv consolidates all series. These Q4 values are logical bytes, not qualified physical traffic.
f_rows = [read_summary(path) for path in sorted(RAW.glob("f_*_summary.csv")) if path.name != "f_r_summary.csv"]
with (RESULTS / "f_ancho_banda.csv").open("w", newline="", encoding="utf-8") as f:
    w = csv.DictWriter(f, fieldnames=list(f_rows[0]))
    w.writeheader(); w.writerows(f_rows)

# Rebuild PDH rollups from captured raw samples. PPM processor suffixes map to logical processor IDs in b_topologia.csv.
logical_type = {}
with TOPOLOGY.open(newline="", encoding="utf-8-sig") as f:
    for row in csv.DictReader(f):
        logical_type[int(row["logical_processor"])] = row["core_type"]
for worker_set in WORKERS:
    idir = RAW / f"i_{worker_set}"
    raw_path = idir / "processor_frequency_pdh_raw.csv"
    if not raw_path.exists():
        continue
    inst_values: dict[str, list[float]] = {}
    type_values: dict[str, list[float]] = {}
    with raw_path.open(newline="", encoding="utf-8-sig") as f:
        for row in csv.DictReader(f):
            match = re.search(r"(\d+)$", row.get("InstanceName", ""))
            kind = logical_type.get(int(match.group(1)), "UNMAPPED") if match else "UNMAPPED"
            value = float(row["CookedValue"])
            inst_values.setdefault(row["InstanceName"], []).append(value)
            type_values.setdefault(kind, []).append(value)
    instance_rows = []
    for instance, vals in sorted(inst_values.items()):
        match = re.search(r"(\d+)$", instance)
        kind = logical_type.get(int(match.group(1)), "UNMAPPED") if match else "UNMAPPED"
        instance_rows.append({"InstanceName": instance, "CoreType": kind, "Samples": len(vals),
                              "FrequencyEffectiveSustainedMedianMHz": statistics.median(vals),
                              "MinimumMHz": min(vals), "MaximumMHz": max(vals)})
    with (idir / "processor_frequency_summary.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(instance_rows[0]))
        w.writeheader(); w.writerows(instance_rows)
    type_rows = [{"CoreType": kind, "Samples": len(vals),
                  "FrequencyEffectiveSustainedMedianMHz": statistics.median(vals),
                  "MinimumMHz": min(vals), "MaximumMHz": max(vals)} for kind, vals in sorted(type_values.items())]
    with (idir / "processor_frequency_by_type.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(type_rows[0]))
        w.writeheader(); w.writerows(type_rows)

# Per-worker-set and per-core-type effective frequency rollups are in the i_* directories.
i_lines = ["# T0.0-i dinámica — frecuencia efectiva sostenida", "",
           f"Carga Q4 AVX2 controlada: {DURATION_SECONDS} s por condición; workers P={WORKERS['P']}, E={WORKERS['E']}, MIXED={WORKERS['MIXED']}.",
           "Frecuencia sostenida = mediana de CookedValue PDH; min/max abarcan las muestras crudas por tipo de núcleo.",
           "El contador informó 2100 MHz en todas las instancias y muestras; la invariancia exacta queda como anomalía de interpretación, no se repitió la carga.", "",
           "| Conjunto de carga | Tipo núcleo | Muestras | Mediana MHz | Mín. MHz | Máx. MHz |", "|---|---|---:|---:|---:|---:|"]
for worker_set in WORKERS:
    by_type = RAW / f"i_{worker_set}" / "processor_frequency_by_type.csv"
    if by_type.exists():
        with by_type.open(newline="", encoding="utf-8-sig") as f:
            for r in csv.DictReader(f):
                i_lines.append(f"| {worker_set} | {r['CoreType']} | {r['Samples']} | {float(r['FrequencyEffectiveSustainedMedianMHz']):.3f} | {float(r['MinimumMHz']):.3f} | {float(r['MaximumMHz']):.3f} |")
    else:
        i_lines.append(f"| {worker_set} | UNAVAILABLE | 0 | UNAVAILABLE | UNAVAILABLE | UNAVAILABLE |")
i_lines.extend(["", "TEMPERATURE_TELEMETRY=UNAVAILABLE; POWER_TELEMETRY=UNAVAILABLE; PL1/PL2=UNAVAILABLE_WITH_ALLOWED_INTERFACES.",
                 "Instancias PDH ppm_processor_N se enlazaron por sufijo N al logical_processor N de b_topologia.csv.",
                 "Los CSV crudos PDH y los registros por condición conservan muestras, comando, worker count y exit code.",
                 "El JSON de Start-Process dejó process_exit_code=null en las tres condiciones; los wrappers PowerShell terminaron con 0 y cada stdout nativo incluye dynamic_load_complete."])
(RESULTS / "i_condiciones.md").write_text("\n".join(i_lines) + "\n", encoding="utf-8")

background_stats = {}
for phase in ("before", "after"):
    p = LOGS / f"stage2_background_{phase}_cim.stdout.txt"
    if p.is_file():
        record = json.loads(p.read_text(encoding="utf-8"))
        values = [float(row["PercentProcessorTime"]) for row in record["cpu_samples"]]
        background_stats[phase] = {"sample_count": len(values), "mean_cpu_percent": statistics.mean(values),
                                   "min_cpu_percent": min(values), "max_cpu_percent": max(values),
                                   "active_process_count": len(record["processes"])}
plan_text = (
    "# Harness T0.0-d/e/f/i dinámica — mediciones de etapa 2\n\n"
    "Autorización del juez recibida; ejecución secuencial capturada en `stage2_raw/` y `logs/COMMANDS.json`. "
    "Orden oficial: e, d, f, i dinámica P/E/MIXED.\n\n"
    "N=31; warmup=5; mediana; IQR+MAD; cero descarte de outliers; KERNEL_CALLS_PER_SAMPLE=128. "
    "BW_D usa n={1,2,4,8,16} y 125,829,120 B por worker. Dinámica: 120 s con P=8, E=8, MIXED=16.\n\n"
    "Los CSV crudos por repetición y resúmenes conservan los valores medidos. `BW_L2` y `r` representan bytes lógicos Q4 y NO se declaran tráfico físico validado.\n\n"
    "Cuantizador float→INT8 sigue sin especificación; correctness y d/e/f usan entradas INT8+escalas ya formadas. "
    "El contador PDH Processor Frequency fue invariante a 2100 MHz en todas las muestras; revisar su interpretación antes de atribuirlo a frecuencia efectiva. "
    "La propiedad `ExitCode` del proceso controlado quedó nula en JSON, aunque cada wrapper finalizó con 0 y se capturó el footer `dynamic_load_complete`; no se repitieron cargas.\n"
)
(RESULTS / "measurement_harness_plan.md").write_text(plan_text, encoding="utf-8")
summary = {"status": "COMPLETE", "order": config["measurement_order"],
           "series_counts": {"e": len(e_rows), "d": len(d_rows), "f": len(f_rows)},
           "dynamic_conditions": WORKERS, "duration_seconds_each": DURATION_SECONDS,
           "post_background_capture_ok": post_ok, "background_cpu": background_stats,
           "logical_bytes_caveat": config["logical_bytes_caveat"],
           "anomalies": ["PDH Processor Frequency was exactly 2100 MHz for every instance/sample and condition; interpretation remains qualified.",
                         "Start-Process JSON process_exit_code was null for all i conditions; wrappers exited 0 and native completion footers were captured; no condition was repeated."]}
(RESULTS / "stage2_execution_status.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
refresh_checksums()
print(json.dumps(summary))
