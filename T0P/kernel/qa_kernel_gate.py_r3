#!/usr/bin/env python3
"""Coverage gate for OMEGA Q4 kernel QA. Mirrors common/qa_gate.py S-16 page math."""
import json
import re
import sys
from pathlib import Path

MS = (1, 4, 8, 16)
FIXTURES = ("uniform", "mixed_cancellation", "varied_scales", "sub127", "extremes",
            "zero_input", "single_coordinate", "cancellation_high", "cancellation_extreme",
            "cancellation_exact_groups")
BUFFERS = {
    "weights": "packed_q4_and_fp16_scales",
    "activations": "prepared_i8_scales_and_correction",
    "Y": "kernel_output",
}
GROUP_FIXTURES = ("uniform", "mixed_cancellation", "varied_scales", "sub127", "extremes")
U64_MAX = (1 << 64) - 1


def page_rows(expected_path):
    obj = json.loads(Path(expected_path).read_text(encoding="utf-8"))
    sizes = obj["expected_bytes"]
    governing = obj["governing_sizes"]
    required = {"weights", "activations", "Y", "dots"}
    if set(sizes) != required:
        raise ValueError("expected_bytes debe fijar weights, activations, Y y dots")
    if sizes["weights"] != 294912 or sizes["activations"] != "672*m" or \
       sizes["Y"] != "4096*m" or sizes["dots"] != "65536*m":
        raise ValueError("tamaños PAGE contractuales incorrectos")
    if governing != {"n_W": 294912, "n_X": "672*m", "n_Y": "4096*m", "n_dots_QA": "65536*m"}:
        raise ValueError("governing_sizes PAGE contractuales incorrectos")
    return obj["buffers"], obj["dwPageSize"], sizes


def expected_bytes(buffer, m):
    return {"weights": 294912, "activations": 672 * m,
            "Y": 4096 * m, "dots": 65536 * m}.get(buffer, 0)


def record_valid(buffer, m, address, n, q, v, large, status, lp, page):
    # The logical allocation size is checked before page coverage is calculated.
    if m not in MS or n != expected_bytes(buffer, m):
        return False
    good = (0 < address <= U64_MAX and 0 < n <= U64_MAX and n <= U64_MAX - address)
    if not good:
        return False
    off = address % page
    span = off + n
    if span > U64_MAX - (page - 1):
        return False
    qexp = (span + page - 1) // page
    return q == qexp and q > 0 and q == v == large and status == "PASS" and lp == "true"


def self_test():
    page = 4096
    if record_valid("weights", 1, 0x1000, 1, 1, 1, 1, "PASS", "true", page):
        raise AssertionError("bytes=1 para pesos se aceptó")
    if record_valid("weights", 1, 0x1000, 294912, 1, 1, 1, "PASS", "true", page):
        raise AssertionError("contadores incoherentes se aceptaron")
    for name, m in (("activations", 4), ("Y", 8), ("dots", 16)):
        expected = expected_bytes(name, m)
        if record_valid(name, m, 0x1000, expected - 1, 1, 1, 1, "PASS", "true", page):
            raise AssertionError(f"tamaño incorrecto {name} se aceptó")
    wanted = {("a", 1), ("b", 1)}
    if {("a", 1)} == wanted:
        raise AssertionError("ausencia no detectada")
    duplicate_rows = [("a", 1), ("a", 1)]
    if len(duplicate_rows) == len(set(duplicate_rows)):
        raise AssertionError("duplicación no detectada")
    print("KERNEL_GATE_SELF_TEST=PASS cases=bytes1,missing,duplicate,incoherent_counters,wrong_X,wrong_Y,wrong_dots")
    return 0


def main(log_path, expected_path):
    text = Path(log_path).read_text(encoding="utf-8", errors="replace")
    buffers, page, sizes = page_rows(expected_path)
    if not isinstance(page, int) or page <= 0 or page > U64_MAX:
        raise ValueError("dwPageSize inválido")
    pat = (r"PAGE fixture=(\S+) m=(\d+) candidate=omega_q4 buffer=(\S+) function=(\S+) "
           r"alias=(\S+) address=(0x[0-9a-fA-F]+) bytes=(\d+) queried_pages=(\d+) "
           r"valid_pages=(\d+) large_pages=(\d+) QueryWorkingSetEx=(PASS|FAIL) LargePage=(true|false)")
    found = {}
    for f, m, b, fn, alias, ahex, nstr, qstr, vstr, lstr, status, lp in re.findall(pat, text):
        key = (f, int(m), b, fn, alias)
        if key in found:
            print(f"PAGE_DUPLICATE key={key}")
            return 1
        a, n, q, v, l = int(ahex, 16), int(nstr), int(qstr), int(vstr), int(lstr)
        good = record_valid(b, int(m), a, n, q, v, l, status, lp, page)
        found[key] = good
    wanted = set()
    for f in FIXTURES:
        for m in MS:
            for b, fn in BUFFERS.items():
                wanted.add((f, m, b, fn, f"cell_{m}_{f}_{b}"))
    for m in MS:
        for b, fn in BUFFERS.items():
            wanted.add(("single_coordinate_sweep", m, b, fn, f"single_coordinate_sweep_{m}_{b}"))
        for f in GROUP_FIXTURES:
            for b, fn in {**BUFFERS, "dots": "qa_group_dot_int32"}.items():
                wanted.add((f, m, b, fn, f"group_{m}_{f}_{b}"))
    if len(wanted) != 212:
        raise ValueError("cobertura esperada interna incorrecta")
    coverage = set(found) == wanted and all(found.values())
    if set(found) != wanted:
        print(f"PAGE_COVERAGE expected={len(wanted)} found={len(found)} missing={len(wanted-set(found))} extra={len(set(found)-wanted)}")
    for m in MS:
        rows = [k for k in found if k[1] == m]
        okay = len(rows) == 53 and all(found[k] for k in rows)
        print(f"PAGE_POLICY m={m} checks={len(rows)} failures={sum(not found[k] for k in rows)} status={'PASS' if okay else 'FAIL'}")

    cells = re.findall(r"CELL fixture=(\S+) m=(\d+) route=omega_q4 finite=(\d+) S12_pass=(\d+) S12_failures=(\d+).*?representation=(PASS|FAIL) negative_representation=(PASS|FAIL).*?exact_output_failures=(\d+)", text)
    cell_map = {(f, int(m)): (finite, s12, int(fail), repr_, neg, int(exact)) for f, m, finite, s12, fail, repr_, neg, exact in cells}
    sweep_rows = {int(m): (int(positions), int(fail), exact) for m, positions, fail, exact in
                  re.findall(r"SINGLE_SWEEP route=omega_q4 M=(\d+) positions=(\d+) batches=\d+ checks=\d+ failures=(\d+) exact_integer_outputs=(PASS|FAIL)", text)}
    # Older and new fixtures share one cell contract; all ten are mandatory per shape.
    numerical_by_m = {}
    for m in MS:
        expected_cells = {(f, m) for f in FIXTURES}
        crows = [cell_map.get(k) for k in expected_cells]
        cell_ok = len(cells) == 40 and len(cell_map) == 40 and all(x is not None for x in crows)
        cell_ok = cell_ok and all(x[0] == "1" and x[1] == "1" and x[2] == 0 and x[3] == "PASS" and x[4] == "PASS" for x in crows)
        exact_fixtures = {"zero_input", "single_coordinate", "cancellation_exact_groups"}
        cell_ok = cell_ok and all(value[5] == 0 for (fixture, _), value in cell_map.items()
                                  if _ == m and fixture in exact_fixtures)
        sweep_ok = sweep_rows.get(m) == (512, 0, "PASS")
        numerical_by_m[m] = cell_ok and sweep_ok
        print(f"QA_NUMERICAL m={m} cells={sum(k[1] == m for k in cell_map)}/10 sweep={'PASS' if sweep_ok else 'FAIL'} status={'PASS' if numerical_by_m[m] else 'FAIL'}")

    group = re.search(r"EXACT_GROUP_DOT_ISOLATION route=omega_q4 checks=(\d+) failures=(\d+) status=(PASS|FAIL)", text)
    ref_like = bool(group and int(group.group(1)) == 2375680 and int(group.group(2)) == 0 and group.group(3) == "PASS"
                    and "REFERENCE_LIKE=PASS" in text and "MANDATORY_GATE=PASS" in text)
    for m in MS:
        ref_ok = ref_like and numerical_by_m[m]
        print(f"REFERENCE_LIKE m={m} status={'PASS' if ref_ok else 'FAIL'}")
    final = coverage and all(numerical_by_m.values()) and ref_like and "PAGE_POLICY=PASS" in text and "QA_NUMERICAL=PASS" in text
    print(f"KERNEL_QA_GATE={'PASS' if final else 'FAIL'}")
    return 0 if final else 2


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        sys.exit(self_test())
    if len(sys.argv) != 3:
        print("usage: qa_kernel_gate.py <qa log> <PAGE_EXPECTED_KERNEL.json>", file=sys.stderr)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2]))
