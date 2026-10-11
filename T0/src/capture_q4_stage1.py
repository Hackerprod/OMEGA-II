"""Build Q4/correctness/harness and run correctness only; no performance modes."""
from __future__ import annotations

import hashlib
import json
import os
import subprocess
from datetime import datetime, timezone
from pathlib import Path

T0 = Path(__file__).resolve().parent.parent
OUT = T0 / "logs"
RESULTS = T0 / "results" / "T0.0"
BUILD = T0 / "build"
CMD = Path(r"C:\Windows\System32\cmd.exe")
PS = Path(r"C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe")

manifest_path = OUT / "COMMANDS.json"
manifest = json.loads(manifest_path.read_text(encoding="utf-8")) if manifest_path.is_file() else []
# Replace this stage's previous capture entries because the paired raw files below are overwritten.
manifest = [entry for entry in manifest if not str(entry.get("name", "")).startswith("stage1_")]


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
    manifest.append({"name": name, "started_utc": started, "command": command_text,
                     "environment_overrides": env_override or {}, "exit_code": result.returncode,
                     "stdout_file": f"{name}.stdout.txt", "stdout_sha256": hashlib.sha256(result.stdout).hexdigest().upper(),
                     "stderr_file": f"{name}.stderr.txt", "stderr_sha256": hashlib.sha256(result.stderr).hexdigest().upper()})
    return result.stdout


capture("stage1_build_probe", [str(CMD), "/c", "call", str(BUILD / "build_probe.cmd")])
capture("stage1_build_q4_correctness", [str(CMD), "/c", "call", str(BUILD / "build_q4_correctness.cmd")])
capture("stage1_build_measurement_harness", [str(CMD), "/c", "call", str(BUILD / "build_measure_harness.cmd")])
capture("stage1_measurement_plan_only", [str(BUILD / "out" / "t0_measure_harness.exe"), "--plan", str(RESULTS / "b_topologia.csv")])
ps_parse = "$tokens=$null; $errors=$null; [System.Management.Automation.Language.Parser]::ParseFile('C:\\Projects\\OMEGA\\Ejercise\\T0\\src\\i_dynamic_probe.ps1',[ref]$tokens,[ref]$errors) | Out-Null; if($errors.Count){$errors | Format-List; exit 1}else{'PARSE_OK'}"
capture("stage1_i_dynamic_script_parse_only", [str(PS), "-NoProfile", "-NonInteractive", "-Command", ps_parse])
ps_pdh = "Get-Counter -ListSet 'Processor Performance' -ErrorAction Stop | Select-Object CounterSetName,Counter | ConvertTo-Json -Depth 4"
capture("stage1_pdh_frequency_counter_catalog", [str(PS), "-NoProfile", "-NonInteractive", "-Command", ps_pdh])
raw = capture("stage1_q4_correctness_cpu_only", [str(BUILD / "out" / "q4_correctness.exe")])
(RESULTS / "q4_correctness_raw.jsonl").write_bytes(raw)
manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

# Refresh the existing checksum inventory and include every raw artifact produced in this capture.
checksums_path = T0 / "SHA256SUMS.txt"
checksum_entries: dict[str, str] = {}
for line in checksums_path.read_text(encoding="utf-8").splitlines():
    digest, relative = line.split(None, 1)
    checksum_entries[relative.strip()] = digest
for entry in manifest:
    for key in ("stdout_file", "stderr_file"):
        relative = f"logs/{entry[key]}"
        checksum_entries[relative] = ""
    relative = f"logs/{entry['name']}.command.txt"
    checksum_entries[relative] = ""
for relative in sorted(checksum_entries):
    path = T0 / relative
    if path.is_file():
        checksum_entries[relative] = hashlib.sha256(path.read_bytes()).hexdigest().upper()
checksums_path.write_text("".join(f"{checksum_entries[path]}  {path}\n" for path in sorted(checksum_entries)), encoding="utf-8")
