"""Preserve exact pre-optimization sources and Phase 0 executable before the bounded kernel change."""
from __future__ import annotations

import hashlib
import json
import shutil
from datetime import datetime, timezone
from pathlib import Path

T0=Path(__file__).resolve().parent.parent
DEST=T0/"results"/"T0.0"/"baseline_preopt"
if DEST.exists() and any(DEST.iterdir()):
    raise SystemExit(f"Refusing to overwrite existing baseline snapshot: {DEST}")
DEST.mkdir(exist_ok=True)
sources={
    "src/q4_kernel.cpp":T0/"src"/"q4_kernel.cpp",
    "src/q4_kernel.hpp":T0/"src"/"q4_kernel.hpp",
    "src/phase0_bench.cpp":T0/"src"/"phase0_bench.cpp",
    "src/build/phase0_bench.exe":T0/"src"/"build"/"phase0_bench.exe",
    "src/build/phase0_q4_kernel.obj":T0/"src"/"build"/"phase0_q4_kernel.obj",
    "src/build/phase0_bench.obj":T0/"src"/"build"/"phase0_bench.obj",
    "results/T0.0/phase0_parameters.json":T0/"results"/"T0.0"/"phase0_parameters.json",
}
manifest={"created_utc":datetime.now(timezone.utc).isoformat(),"classification":"PREOPT_ENGINEERING_BASELINE_SNAPSHOT",
          "source_and_binary_executed_for_invalid_phase0_run":True,"files":[]}
for relative,source in sources.items():
    if not source.is_file(): raise SystemExit(f"Required baseline artifact missing: {source}")
    target=DEST/(relative.replace("/","__"))
    shutil.copy2(source,target)
    digest=hashlib.sha256(source.read_bytes()).hexdigest().upper()
    manifest["files"].append({"original_path":relative,"snapshot_path":target.relative_to(T0).as_posix(),
                              "size_bytes":source.stat().st_size,"sha256":digest})
(DEST/"snapshot_manifest.json").write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")

sum_path=T0/"SHA256SUMS.txt"
entries={}
for line in sum_path.read_text(encoding="utf-8").splitlines():
    digest,path=line.split(None,1);entries[path.strip()]=digest
entries["src/freeze_preopt_baseline.py"]=""
for file in DEST.rglob("*"):
    if file.is_file():entries[file.relative_to(T0).as_posix()]=""
for relative in sorted(entries):
    file=T0/relative
    if file.is_file():entries[relative]=hashlib.sha256(file.read_bytes()).hexdigest().upper()
sum_path.write_text("".join(f"{entries[p]}  {p}\n" for p in sorted(entries)),encoding="utf-8")
print(json.dumps({"snapshot_files":len(manifest["files"]),"destination":DEST.relative_to(T0).as_posix(),
                  "phase0_raw_run1_invalidated":True}))
