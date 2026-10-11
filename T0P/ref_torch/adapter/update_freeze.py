import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
REF = ROOT / "T0P" / "ref_torch"
COMMON = ROOT / "T0P" / "common"
LOG = REF / "adapter" / "verify_common_all.txt"
raw_log = LOG.read_bytes()
log = raw_log.decode("utf-16") if raw_log.startswith((b"\xff\xfe", b"\xfe\xff")) else raw_log.decode("utf-8", errors="replace")
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()

modules = []
for line in log.splitlines():
    m = re.match(r"loaded_module path=(.*?) image_bytes=(\d+) file_bytes=(\d+) sha256=([0-9a-f]{64})$", line)
    if m:
        path, image_bytes, file_bytes, digest = m.groups()
        if sha(path) != digest:
            raise SystemExit(f"runtime module changed since capture: {path}")
        modules.append({"path": path, "image_bytes": int(image_bytes), "file_bytes": int(file_bytes), "sha256": digest})
if len(modules) != 37:
    raise SystemExit(f"expected 37 captured modules, got {len(modules)}")

cells = []
for line in log.splitlines():
    m = re.match(r"cell M=(\d+) fixture=([^ ]+) canonical_sha256=([0-9a-f]{64}) source=(.*)$", line)
    if m:
        M, fixture, digest, source = m.groups()
        p = ROOT / source.replace("/", "\\")
        if sha(p) != digest:
            raise SystemExit(f"canonical cell hash mismatch: {p}")
        cells.append({"M": int(M), "fixture": fixture, "file": source.replace("\\", "/"), "sha256": digest, "bytes": p.stat().st_size})
if len(cells) != 20:
    raise SystemExit(f"expected 20 canonical cells, got {len(cells)}")

summaries = []
for line in log.splitlines():
    m = re.match(r"candidate_summary=([^ ]+) max_abs=([^ ]+) max_rel=([^ ]+) failing_fixtures=(\d+)$", line)
    if m:
        name, ab, rel, failures = m.groups()
        candidate, M = name.split("/m=")
        summaries.append({"candidate": candidate, "M": int(M), "max_abs": float(ab), "max_rel_nonzero_reference": float(rel), "failing_fixtures": int(failures)})

dispatch = {}
for line in log.splitlines():
    m = re.match(r"dispatch_candidate=(\S+) observed_ops=(.*)$", line)
    if m:
        dispatch[m.group(1)] = [x for x in m.group(2).split(",") if x]

torchline = next(x for x in log.splitlines() if x.startswith("torch="))
precision = next(x for x in log.splitlines() if x.startswith("fp32_precision "))
policy = next(x for x in log.splitlines() if x.startswith("tf32_onednn="))
overall = next(x for x in log.splitlines() if x.startswith("overall_max_abs="))
mkldnn = next(x for x in log.splitlines() if x.startswith("mkldnn_linear_final="))

source_files = [
    REF / "adapter" / "ref_torch.cpp", REF / "adapter" / "build.cmd",
    REF / "adapter" / "ref_torch.exe", REF / "adapter" / "ref_torch.exp", REF / "adapter" / "ref_torch.lib",
    REF / "adapter" / "obj" / "ref_torch.obj", REF / "adapter" / "verify_common_all.txt",
    REF / "adapter" / "verifier_unit_tests.txt", REF / "adapter" / "update_freeze.py",
    REF / "REF_FREEZE.md", COMMON / "README.md", COMMON / "common.hpp", COMMON / "common.cpp",
    COMMON / "build.cmd", COMMON / "generate_main.cpp", COMMON / "generate_cells.exe", COMMON / "obj" / "common.obj",
]
source_files += sorted((COMMON / "data").glob("*.bin"))
for p in source_files:
    if not p.exists():
        raise SystemExit(f"missing frozen artifact: {p}")

freeze = {
    "campaign": "T0P-Q4-RESIDENCY",
    "revision": "common-inputs-and-S10-adapter-validation",
    "torch_version": "2.11.0+cu128",
    "binary": "CPU ATen only; CUDA-enabled wheel package",
    "dimensions": {"O": 1024, "I": 512, "M": [1, 4, 8, 16]},
    "common_input_contract": {
        "generator": "T0P/common/common.cpp; SplitMix64 state=0x6a09e667f3bcc909 XOR (M<<24) XOR (fixture_id<<40), fixed mixer and fixture-specific deterministic rules documented in README",
        "serialization": "T0P/common/README.md: ASCII magic, little-endian u32 metadata, signed int8 code arrays, little-endian IEEE binary16 scale arrays in fixed O/I/G/M order",
        "canonical_cells": cells,
        "fixtures": ["uniform", "mixed_cancellation", "varied_scales", "sub127", "extremes"],
        "scale_rule": "FP16 bits preserved; FP32 values are exact decodes of the same binary16 bits",
        "ggml_and_omega_conversion": "documented and implemented by ggml_q4_0(), ggml_q8_0(), omega_q4_18() in common.cpp; no requantization",
    },
    "acceptance_reference": {
        "algorithm": "exact signed int32 dot per 32-code group; apply original FP16-representable activation and weight scales; accumulate groups in double; output FP32",
        "tolerance": "abs(y-y_ref) <= 1e-5 + 1e-5*abs(y_ref)",
        "finite_checks": ["weight scales", "activation scales", "reference", "candidate output", "absolute error"],
        "verifier_unit_tests": "PASS: valid accepted; NaN rejected; Inf rejected; finite out-of-tolerance value rejected",
        "relative_error_convention": "max relative excludes exactly-zero references; max absolute includes them",
    },
    "adapter_run": {
        "input": "all 20 serialized common cells loaded from T0P/common/data",
        "candidate_summaries": summaries,
        "overall": overall,
        "timed_benchmark": False,
        "dispatch_observed_ops": dispatch,
        "explicit_mkldnn": {"status": "NOT_COMPARABLE_PAGE_POLICY", "detail": mkldnn.split(" cause=", 1)[-1], "operator_observed": "aten::mkldnn_linear"},
    },
    "threads": {"effective": torchline, "policy_and_precision_readback": [precision, policy], "worker_model": "one process thread; no worker thread created"},
    "buffer_pages": {
        "method": "VirtualQuery on the effective ATen tensor data_ptr after from_blob(...).clone()",
        "effective_tensors": ["dynamic X clone", "persistent FP32 W [O,I] clone", "persistent contiguous FP32 Wt [I,O] clone"],
        "log": "T0P/ref_torch/adapter/verify_common_all.txt; records pointer, bytes, alignment, region, state, type and page protection",
        "mkldnn_limitation": "packed opaque tensor has no data_ptr; exact ATen error recorded; excluded from comparable candidates",
    },
    "loaded_modules": modules,
    "build": {"compiler": "MSVC 14.44.35207 x64", "sdk": "10.0.26100.0", "commands": ["T0P/common/build.cmd", "T0P/ref_torch/adapter/build.cmd"]},
    "ambiguities_open": [
        "ATen opaque MKLDNN storage allocator and physical page policy cannot be inspected through installed public API",
        "FP32 dense candidates fail the acceptance tolerance on most fixtures; future Torch comparator eligibility remains open",
        "multiworker affinity and 8P harness execution were not in this untimed adapter validation",
    ],
    "artifacts": {},
}
for p in source_files:
    key = str(p.relative_to(ROOT)).replace("\\", "/")
    freeze["artifacts"][key] = sha(p)
REF.joinpath("REF_FREEZE.json").write_text(json.dumps(freeze, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

manifest_paths = {str(p.resolve()) for p in modules and [Path(x["path"]) for x in modules]}
manifest_paths.update(str(p.resolve()) for p in source_files)
manifest_paths.add(str((REF / "REF_FREEZE.json").resolve()))
manifest_paths.add(str((REF / "REF_FREEZE.md").resolve()))
records = []
for name in sorted(manifest_paths, key=str.lower):
    records.append(f"{sha(name)}  {name}")
REF.joinpath("SHA256SUMS_ref_torch.txt").write_text("\n".join(records) + "\n", encoding="utf-8")

for line in REF.joinpath("SHA256SUMS_ref_torch.txt").read_text(encoding="utf-8").splitlines():
    digest, path = line.split("  ", 1)
    if sha(path) != digest:
        raise SystemExit(f"manifest mismatch: {path}")
print(f"freeze_ok manifest_entries={len(records)} modules={len(modules)} cells={len(cells)}")
