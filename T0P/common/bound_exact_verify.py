#!/usr/bin/env python3
"""Independent Fraction oracle for all generated S-12 bounds."""
from fractions import Fraction
from pathlib import Path
import csv, math, struct, sys

ROOT = Path(__file__).resolve().parent
DATA = ROOT / 'data'
I, O, G = 512, 1024, 16

def half(bits):
    return Fraction.from_float(struct.unpack('<e', struct.pack('<H', bits))[0])

def cell(path):
    b = path.read_bytes()
    pos = len(b' T0P-CELL-v1'.strip())
    if b[:pos] != b'T0P-CELL-v1': raise ValueError(f'bad header: {path}')
    o, i, m, fixture = struct.unpack_from('<IIII', b, pos); pos += 16
    if (o, i) != (O, I): raise ValueError(f'bad dimensions: {path}')
    w = list(struct.unpack_from(f'<{O*I}b', b, pos)); pos += O*I
    ws = [half(x) for x in struct.unpack_from(f'<{O*G}H', b, pos)]; pos += O*G*2
    x = list(struct.unpack_from(f'<{m*I}b', b, pos)); pos += m*I
    xs = [half(v) for v in struct.unpack_from(f'<{m*G}H', b, pos)]; pos += m*G*2
    if pos != len(b): raise ValueError(f'trailing bytes: {path}')
    return m, w, ws, x, xs

def exact_bound(m, o, w, ws, x, xs):
    s = Fraction(0)
    for g in range(G):
        for k in range(32):
            j = g*32+k
            s += abs(w[o*I+j] * x[j] * ws[o*G+g] * xs[g])
    u32, u64 = Fraction(1, 1 << 24), Fraction(1, 1 << 53)
    g514_32 = (514*u32)/(1-514*u32)
    g18_64 = (18*u64)/(1-18*u64)
    g514_64 = (514*u64)/(1-514*u64)
    return (g514_32+g18_64)*s/(1-g514_64)

def main(csv_path):
    rows = list(csv.reader(Path(csv_path).open(newline='')))
    checked = 0
    cache = {}
    for fixture, ms, slots, os, bs in rows:
        m, slot, o, b = int(ms), int(slots), int(os), float(bs)
        key = (fixture, m)
        if key not in cache: cache[key] = cell(DATA/f'm{m}_{fixture}.bin')
        cm, w, ws, x, xs = cache[key]
        if cm != m: raise ValueError('M mismatch')
        exact = exact_bound(m, o, w, ws, x[slot*I:(slot+1)*I], xs[slot*G:(slot+1)*G])
        if Fraction.from_float(b) < exact:
            raise AssertionError(f'B below exact rational bound: {fixture} m={m} slot={slot} o={o} B={b}')
        checked += 1
    print(f'EXACT_RATIONAL_BOUND checks={checked} failures=0 status=PASS')
    return 0

if __name__ == '__main__':
    if len(sys.argv) != 2: raise SystemExit('usage: bound_exact_verify.py BOUNDS.csv')
    raise SystemExit(main(sys.argv[1]))
