"""Build H0-only executables and run structural/correctness QA; it never invokes H0 timing modes."""
from __future__ import annotations

import hashlib
import json
import subprocess
import sys
from datetime import datetime,timezone
from pathlib import Path

T0=Path(__file__).resolve().parent.parent
LOGS=T0/"logs"
OUT=T0/"src"/"build"
RESULTS=T0/"results"/"T0.1"
CMD=Path(r"C:\Windows\System32\cmd.exe")
PYTHON=Path(r"C:\Projects\OMEGA\.venv_cuda\Scripts\python.exe")
manifest_path=LOGS/"COMMANDS.json"
manifest=json.loads(manifest_path.read_text(encoding="utf-8"))


def capture(name:str,argv:list[str],timeout:int=240)->subprocess.CompletedProcess:
    if any((LOGS/f"{name}.{suffix}").exists() for suffix in ("command.txt","stdout.txt","stderr.txt")):
        raise FileExistsError(f"refusing to overwrite H0 build/QA log {name}")
    command=subprocess.list2cmdline(argv)
    started=datetime.now(timezone.utc).isoformat()
    result=subprocess.run(argv,cwd=LOGS,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=False,timeout=timeout)
    (LOGS/f"{name}.command.txt").write_text(command+"\n",encoding="utf-8")
    (LOGS/f"{name}.stdout.txt").write_bytes(result.stdout)
    (LOGS/f"{name}.stderr.txt").write_bytes(result.stderr)
    manifest.append({"name":name,"started_utc":started,"completed_utc":datetime.now(timezone.utc).isoformat(),
                     "command":command,"exit_code":result.returncode,"measurement":False,
                      "program_executed":not name.startswith("h0_build_attempt"),"official_timing":False,
                     "stdout_file":f"{name}.stdout.txt","stdout_sha256":hashlib.sha256(result.stdout).hexdigest().upper(),
                     "stderr_file":f"{name}.stderr.txt","stderr_sha256":hashlib.sha256(result.stderr).hexdigest().upper()})
    manifest_path.write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")
    print(f"{name}: exit={result.returncode}")
    return result


def update_plan_hashes()->None:
    plan_path=RESULTS/"h0_measurement_plan.json"
    plan=json.loads(plan_path.read_text(encoding="utf-8"))
    artifacts=("src/h0_scaffold.hpp","src/h0_scaffold.cpp","src/h0_measure.cpp","src/h0_scaffold_main.cpp","src/h0_qa.cpp",
               "src/h0_runner.py","src/test_h0_r_noise.py","src/capture_h0_scaffold.py","src/verify_h0_disassembly.py","src/build/build_h0_scaffold.cmd",
                "src/q4_kernel.cpp","src/q4_kernel.hpp","../T0.md","../Roadmap.md","../MD/29.md","../MD/30.md","../MD/31.md","../MD/32.md",
               "src/build/h0_scaffold.exe","src/build/h0_qa.exe")
    hashes={rel:hashlib.sha256((T0/rel).read_bytes()).hexdigest().upper() for rel in artifacts}
    plan["artifacts_sha256"]={"state":"SCAFFOLD_BUILD_AND_QA_COMPLETED_NO_TIMING","sources_and_binaries":hashes}
    plan_path.write_text(json.dumps(plan,indent=2)+"\n",encoding="utf-8")


def refresh_hashes(names:list[str])->None:
    sums=T0/"SHA256SUMS.txt";entries={}
    for line in sums.read_text(encoding="utf-8").splitlines():
        parts=line.split(None,1)
        if len(parts)==2:entries[parts[1].strip()]=parts[0]
    tracked=["src/h0_scaffold.hpp","src/h0_scaffold.cpp","src/h0_measure.cpp","src/h0_scaffold_main.cpp","src/h0_qa.cpp","src/h0_runner.py","src/test_h0_r_noise.py",
             "src/capture_h0_scaffold.py","src/verify_h0_disassembly.py","src/build/build_h0_scaffold.cmd",
             "results/T0.1/h0_measurement_plan.json","../T0.md","../Roadmap.md","../MD/29.md","../MD/30.md","../MD/31.md","../MD/32.md"]
    entries.update({rel:"" for rel in tracked})
    for name in names:
        for suffix in ("command.txt","stdout.txt","stderr.txt"):entries[f"logs/{name}.{suffix}"]=""
    for path in OUT.glob("h0_*"):
        if path.is_file():entries[path.relative_to(T0).as_posix()]=""
    for rel in list(entries):
        path=T0/rel
        if path.is_file():entries[rel]=hashlib.sha256(path.read_bytes()).hexdigest().upper()
        else:del entries[rel]
    sums.write_text("".join(f"{entries[rel]}  {rel}\n" for rel in sorted(entries)),encoding="utf-8")


if sys.argv[1:] not in ([],["--qa-only"]):raise SystemExit("usage: capture_h0_scaffold.py [--qa-only]")
used={entry.get("name","") for entry in manifest}
attempt=1
while f"h0_build_attempt{attempt}" in used:attempt+=1
build_name=f"h0_build_attempt{attempt}"
describe_name=f"h0_describe_scaffold_attempt{attempt}"
qa_name=f"h0_qa_no_timing_attempt{attempt}"
disassembly_name=f"h0_disassembly_check_attempt{attempt}"
noise_qa_name=f"h0_r_noise_synthetic_qa_attempt{attempt}"
build=capture(build_name,[str(CMD),"/c","call",str(T0/"src"/"build"/"build_h0_scaffold.cmd")])
if build.returncode:raise SystemExit(build.returncode)
describe=capture(describe_name,[str(OUT/"h0_scaffold.exe"),"--describe"])
if describe.returncode:raise SystemExit(describe.returncode)
qa=capture(qa_name,[str(OUT/"h0_qa.exe"),str(T0/"results"/"T0.0"/"b_topologia.csv")],timeout=300)
qa_text=qa.stdout.decode("utf-8",errors="replace")
if qa.returncode or '"status":"PASS"' not in qa_text or '"measurement":false' not in qa_text or '"official_timing":false' not in qa_text:
    raise SystemExit("H0 QA did not PASS with explicit no-timing markers")
disassembly=capture(disassembly_name,[str(PYTHON),str(T0/"src"/"verify_h0_disassembly.py"),str(OUT/"h0_scaffold.asm")])
if disassembly.returncode:raise SystemExit("H0 K-loop disassembly check failed")
noise_qa=capture(noise_qa_name,[str(PYTHON),str(T0/"src"/"test_h0_r_noise.py")])
noise_text=noise_qa.stdout.decode("utf-8",errors="replace")
if noise_qa.returncode or '"n31_index":29' not in noise_text or '"n20_index":18' not in noise_text or '"measurement":false' not in noise_text or '"status":"PASS"' not in noise_text:
    raise SystemExit("synthetic MD/31 R_noise percentile QA failed")
update_plan_hashes()
refresh_hashes([build_name,describe_name,qa_name,disassembly_name,noise_qa_name])
print(json.dumps({"build":"PASS","qa":"PASS","r_noise_synthetic_qa":"PASS","disassembly":"PASS","official_timing_performed":False,
                  "plan":"results/T0.1/h0_measurement_plan.json","sha_manifest":"SHA256SUMS.txt"}))
