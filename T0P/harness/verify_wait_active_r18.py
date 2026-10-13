#!/usr/bin/env python3
"""Verify the linked R18 worker/busy-barrier wait path from its disassembly."""
from __future__ import annotations

import argparse
import re
from pathlib import Path


FORBIDDEN = re.compile(
    r"(?:__imp_)?(?:Sleep|WaitOnAddress|SwitchToThread|YieldProcessor|umwait|tpause)|"
    r"condition_variable|_Cnd_(?:do_wait|timedwait)|this_thread::yield", re.IGNORECASE)


def verify(disassembly: Path, source: Path, linker_map: Path) -> dict[str, int]:
    raw_disassembly = disassembly.read_bytes()
    if raw_disassembly.startswith((b"\xff\xfe", b"\xfe\xff")):
        disassembly_text = raw_disassembly.decode("utf-16")
    else:
        disassembly_text = raw_disassembly.decode("utf-8", errors="replace")
    lines = disassembly_text.splitlines()
    map_lines = linker_map.read_text(encoding="utf-8", errors="replace").splitlines()
    symbols: list[tuple[int, str, str]] = []
    symbol_pattern = re.compile(r"^\s*\S+:\S+\s+(\S+)\s+([0-9A-Fa-f]{16})\s+f\s+(\S+)\s*$")
    for line in map_lines:
        match = symbol_pattern.match(line)
        if match and match.group(3) == "residency_r18.obj":
            symbols.append((int(match.group(2), 16), match.group(1), match.group(3)))
    gate_rows = [row for row in symbols if row[1].startswith("?wait@Gate@") and row[1].endswith("QEAA_NXZ")]
    worker_rows = [row for row in symbols if row[1].startswith("?worker_main@?A") and
                   row[1].endswith("@harness@@YAXI@Z")]
    if len(gate_rows) != 1 or len(worker_rows) != 1:
        raise ValueError(f"linker_map_gate_or_worker_symbol_missing:{len(gate_rows)}:{len(worker_rows)}")
    def mapped_body(target: tuple[int, str, str]) -> str:
        starts = sorted({address for address, _name, _obj in symbols if address > target[0]})
        if not starts:
            raise ValueError(f"linker_map_function_end_missing:{target[1]}")
        end_address = starts[0]
        instructions: list[str] = []
        for line in lines:
            match = re.match(r"\s*([0-9A-Fa-f]{16}):", line)
            if match:
                address = int(match.group(1), 16)
                if target[0] <= address < end_address:
                    instructions.append(line)
        if not instructions or not instructions[0].lstrip().startswith(f"{target[0]:016X}:"):
            raise ValueError(f"disassembly_function_range_missing:{target[1]}:{target[0]:016X}")
        return "\n".join(instructions)
    gate, worker = mapped_body(gate_rows[0]), mapped_body(worker_rows[0])
    for name, body in (("Gate::wait", gate), ("worker_main", worker)):
        hit = FORBIDDEN.search(body)
        if hit:
            raise ValueError(f"forbidden_wait_in_{name}:{hit.group(0)}")
    if "lock xadd" not in gate or not re.search(r"\bpause\b", gate, re.IGNORECASE):
        raise ValueError("Gate_wait_not_atomic_pause_spin")
    worker_gate_barriers = len(re.findall(r"lock xadd", worker, re.IGNORECASE))
    worker_pause = len(re.findall(r"\bpause\b", worker, re.IGNORECASE))
    if worker_gate_barriers < 2 or worker_pause < 2:
        raise ValueError(f"worker_main_missing_two_inline_spin_barriers:{worker_gate_barriers}:{worker_pause}")
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
            "worker_pause": worker_pause, "worker_atomic_barriers": worker_gate_barriers}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--disassembly", type=Path, required=True)
    parser.add_argument("--source", type=Path, default=Path(__file__).with_name("residency_r18.cpp"))
    parser.add_argument("--map", type=Path, default=Path(__file__).resolve().parent / "logs_r18" / "obj_r18" / "harness_r18.map")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = verify(args.disassembly, args.source, args.map)
        report = ("ACTIVE_WAIT_DISASSEMBLY_R18 PASS " + " ".join(f"{k}={v}" for k, v in result.items()) +
                  " forbidden_worker_wait_calls=0\n")
        args.output.write_text(report, encoding="utf-8", newline="\n")
        print(report.rstrip())
        return 0
    except (OSError, ValueError, StopIteration) as exc:
        print(f"ACTIVE_WAIT_DISASSEMBLY_R18 FAIL {type(exc).__name__}:{exc}")
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
