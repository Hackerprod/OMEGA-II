#!/usr/bin/env python3
"""Extract inner K4 loop instruction counts from dumpbin /disasm output."""
import re
import sys
from pathlib import Path

INSN = re.compile(r"^\s+([0-9A-Fa-f]{16}):\s+((?:[0-9A-Fa-f]{2}\s+)+)(.*)$")


def functions(lines):
    result = {}
    current = None
    for line in lines:
        if "run_tile<1>(" in line:
            current = 1
            result[current] = []
            continue
        if "run_tile<4>(" in line:
            current = 4
            result[current] = []
            continue
        if current is not None and line.startswith("??") and "run_tile<" not in line:
            current = None
        if current is None:
            continue
        m = INSN.match(line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        tail = m.group(3).strip()
        if not tail:
            continue
        parts = tail.split(None, 1)
        op = parts[0].lower()
        operands = parts[1] if len(parts) > 1 else ""
        if op == "vex" and operands.lower().startswith("vpdpbusd"):
            op = "vpdpbusd"
            operands = operands[len("vpdpbusd"):].strip()
        result[current].append((addr, op, operands))
    return result


def inner_body(instructions):
    branches = []
    for i, (addr, op, operands) in enumerate(instructions):
        if op not in {"jne", "jnz"}:
            continue
        m = re.search(r"([0-9A-Fa-f]{16})", operands)
        if m:
            dst = int(m.group(1), 16)
            if dst < addr:
                candidate = [x for x in instructions if dst <= x[0] <= addr]
                ops = [x[1] for x in candidate]
                if "vpdpbusd" in ops and any(op in {"vpand", "vpsrlw"} for op in ops):
                    branches.append((addr - dst, dst, i))
    if not branches:
        raise RuntimeError("no se detectó salto de bucle hacia atrás")
    _, start, end = min(branches)
    return [x for x in instructions if start <= x[0] <= instructions[end][0]]


def categorize(body):
    counts = {k: 0 for k in ("vpdpbusd", "loads", "decode", "conversions", "fma", "spills", "other")}
    for _, op, operands in body:
        if op == "vpdpbusd":
            counts["vpdpbusd"] += 1
        elif op.startswith("vcvt"):
            counts["conversions"] += 1
        elif op.startswith("vfmadd") or op.startswith("vfnmadd"):
            counts["fma"] += 1
        elif op in {"vpand", "vpsrlw", "vpunpckhbw", "vpunpcklbw", "vinserti128"}:
            counts["decode"] += 1
        elif ("ptr [" in operands.lower() and (op.startswith("vmov") or op.startswith("vpbroadcast"))):
            if re.search(r"\[(?:e|r)(?:bp|sp)(?:[+\-\]])", operands.lower()):
                counts["spills"] += 1
            else:
                counts["loads"] += 1
        else:
            counts["other"] += 1
    counts["total"] = sum(counts.values())
    counts["fraction"] = counts["vpdpbusd"] / counts["total"] if counts["total"] else 0.0
    return counts


def main(disasm, output):
    parsed = functions(Path(disasm).read_text(encoding="utf-8", errors="replace").splitlines())
    if 1 not in parsed or 4 not in parsed:
        raise RuntimeError("no se encontraron run_tile<1> y run_tile<4> en dumpbin")
    counts = {m: categorize(inner_body(parsed[tile])) for m, tile in ((1, 1), (4, 4), (8, 4), (16, 4))}
    lines = ["# Conteo estático de instrucciones", "",
             "Fuente: dumpbin /disasm obj\\q4_kernel.obj. Para cada especialización se cuenta el salto K4 más corto que incluye decode y VPDPBUSD, incluidas cargas/broadcasts y el salto. No es una medida dinámica.", "",
             "| m | tile activaciones | VPDPBUSD | cargas/broadcasts | decode | conversiones | FMA | spills | otras | total | VPDPBUSD/total |",
             "|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for m in (1, 4, 8, 16):
        c = counts[m]
        lines.append(f"| {m} | {1 if m == 1 else 4} | {c['vpdpbusd']} | {c['loads']} | {c['decode']} | {c['conversions']} | {c['fma']} | {c['spills']} | {c['other']} | {c['total']} | {100*c['fraction']:.2f}% |")
    lines += ["", "## Comparación ronda 1 / ronda 2", "",
              "| m | VPDPBUSD/total R1 | VPDPBUSD/total R2 | decode/VPDPBUSD R2 | cadenas R2 | spills R2 |",
              "|---:|---:|---:|---:|---:|---:|",
              "| 1 | 1/13 = 7,69% | 18/60 = 30,00% | 27/18 = 1,50 | 9 | 0 |",
              "| 4 | 4/19 = 21,05% | 32/68 = 47,06% | 12/32 = 0,375 | 8 | 0 |",
              "| 8 | 4/19 = 21,05% | 32/68 = 47,06% | 12/32 = 0,375 | 8 | 0 |",
              "| 16 | 4/19 = 21,05% | 32/68 = 47,06% | 12/32 = 0,375 | 8 | 0 |",
              "",
              "El decoder reconoce saltos K4 por contener tanto instrucciones de nibble como VPDPBUSD; cuenta como spill las cargas/almacenamientos SIMD a slots de stack dentro de ese cuerpo. Los guardados ABI en el prólogo y epílogo quedan fuera. El escalado FP32 está fuera del K4. Los porcentajes son conteos del objeto, no mediciones de rendimiento.", ""]
    Path(output).write_text("\n".join(lines), encoding="utf-8")
    for m, c in counts.items():
        print(f"STATIC m={m} vpdpbusd={c['vpdpbusd']} loads={c['loads']} decode={c['decode']} conversions={c['conversions']} fma={c['fma']} spills={c['spills']} other={c['other']} total={c['total']} fraction={c['fraction']:.6f}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: inspect_disasm.py <dumpbin output> <report.md>")
    main(sys.argv[1], sys.argv[2])
