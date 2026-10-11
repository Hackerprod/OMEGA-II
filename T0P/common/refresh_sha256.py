#!/usr/bin/env python3
"""Regenerate and verify SHA256 manifests for the three authorized T0P folders."""
from hashlib import sha256
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
TARGETS=[(ROOT/'T0P/common','SHA256SUMS_common.txt'),(ROOT/'T0P/ref_torch','SHA256SUMS_ref_torch.txt'),(ROOT/'T0P/ref_ggml','SHA256SUMS_ref_ggml.txt')]
for folder,name in TARGETS:
    manifest=folder/name
    rows=[]
    for path in sorted((p for p in folder.rglob('*') if p.is_file() and p!=manifest),key=lambda p:p.relative_to(folder).as_posix().lower()):
        h=sha256(path.read_bytes()).hexdigest()
        rows.append(f"{h}  {path.relative_to(folder).as_posix()}")
    manifest.write_text("\n".join(rows)+"\n",encoding="ascii",newline="\n")
    bad=[]
    for row in rows:
        expected,rel=row.split("  ",1)
        actual=sha256((folder/Path(rel)).read_bytes()).hexdigest()
        if actual!=expected: bad.append(rel)
    print(f"manifest={manifest.relative_to(ROOT).as_posix()} matched={len(rows)-len(bad)}/{len(rows)} failures={len(bad)}")
    if bad: raise SystemExit(1)