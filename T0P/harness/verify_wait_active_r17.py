#!/usr/bin/env python3
"""Verify the linked R17 worker/busy-barrier wait path from its disassembly."""
from __future__ import annotations

import argparse
import re
from pathlib import Path


FORBIDDEN = re.compile(
    r"(?:__imp_)?(?:Sleep|WaitOnAddress|SwitchToThread|YieldProcessor|umwait|tpause)|"
    r"condition_variable|_Cnd_(?:do_wait|timedwait)|this_thread::yield", re.IGNORECASE)


def verify(disassembly: Path, source: Path) -> dict[str, int]:
    raw_disassembly = disassembly.read_bytes()
    if raw_disassembly.startswith((b"\xff\xfe", b"\xfe\xff")):
        disassembly_text = raw_disassembly.decode("utf-16")
    else:
        disassembly_text = raw_disassembly.decode("utf-8", errors="replace")
    lines = disassembly_text.splitlines()
    gate_start = next((i for i, line in enumerate(lines)
                       if "Gate::wait(void)" in line and line.rstrip().endswith(":")), None)
    worker_start = next((i for i, line in enumerate(lines)
                         if "worker_main(unsigned int)" in line and line.rstrip().endswith(":")), None)
    if gate_start is None or worker_start is None or worker_start <= gate_start:
        raise ValueError("disassembly_gate_or_worker_symbol_missing")
    gate_end = next((i for i in range(gate_start + 1, len(lines))
                     if lines[i].startswith("?") and lines[i].rstrip().endswith(":")), len(lines))
    worker_end = next((i for i in range(worker_start + 1, len(lines))
                       if lines[i].startswith("?dtor") and "worker_main" in lines[i]), len(lines))
    gate, worker = "\n".join(lines[gate_start:gate_end]), "\n".join(lines[worker_start:worker_end])
    for name, body in (("Gate::wait", gate), ("worker_main", worker)):
        hit = FORBIDDEN.search(body)
        if hit:
            raise ValueError(f"forbidden_wait_in_{name}:{hit.group(0)}")
    if "lock xadd" not in gate or not re.search(r"\bpause\b", gate, re.IGNORECASE):
        raise ValueError("Gate_wait_not_atomic_pause_spin")
    if ("g_start@" not in worker or "g_done@" not in worker or "lock xadd" not in worker or
            not re.search(r"\bpause\b", worker, re.IGNORECASE)):
        raise ValueError("worker_main_missing_gate_or_pause_spin")
    cpp = source.read_text(encoding="utf-8", errors="replace")
    def function_body(anchor: str) -> str:
        start = cpp.index(anchor)
        opening = cpp.index("{", start)
        depth = 0
        for index in range(opening, len(cpp)):
            if cpp[index] == "{": depth += 1
            elif cpp[index] == "}":
                depth -= 1
                if depth == 0:
                    return cpp[opening:index + 1]
        raise ValueError(f"source_function_brace_missing:{anchor}")
    source_gate = function_body("bool wait()")
    source_worker = function_body("void worker_main(unsigned worker)")
    if FORBIDDEN.search(source_gate) or FORBIDDEN.search(source_worker):
        raise ValueError("forbidden_wait_in_source_worker_path")
    if ("_mm_pause()" not in source_gate or "if (!g_start.wait()) return;" not in source_worker or
            "g_done.wait()" not in source_worker):
        raise ValueError("source_worker_wait_contract_missing")
    return {"gate_pause": len(re.findall(r"\bpause\b", gate, re.IGNORECASE)),
            "worker_pause": len(re.findall(r"\bpause\b", worker, re.IGNORECASE)),
            "worker_atomic_barriers": len(re.findall(r"lock xadd", worker, re.IGNORECASE))}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--disassembly", type=Path, required=True)
    parser.add_argument("--source", type=Path, default=Path(__file__).with_name("residency_r17.cpp"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = verify(args.disassembly, args.source)
        report = ("ACTIVE_WAIT_DISASSEMBLY_R17 PASS " + " ".join(f"{k}={v}" for k, v in result.items()) +
                  " forbidden_worker_wait_calls=0\n")
        args.output.write_text(report, encoding="utf-8", newline="\n")
        print(report.rstrip())
        return 0
    except (OSError, ValueError, StopIteration) as exc:
        print(f"ACTIVE_WAIT_DISASSEMBLY_R17 FAIL {type(exc).__name__}:{exc}")
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
