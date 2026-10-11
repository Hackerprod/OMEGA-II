"""Derive conventional-MAC/s tables from captured d/e raw CSVs; never runs benchmarks."""
from __future__ import annotations

import csv
import hashlib
import json
import math
import statistics
from pathlib import Path

T0 = Path(__file__).resolve().parent.parent
RESULTS = T0 / "results" / "T0.0"
RAW = RESULTS / "stage2_raw"
SUMS = T0 / "SHA256SUMS.txt"

original_paths = [
    RESULTS / "d_vi.csv", RESULTS / "d_vi_resumen.csv", RESULTS / "e.md",
    RESULTS / "f_ancho_banda.csv", RAW / "f_r_summary.csv",
    *sorted(RAW.glob("d_vi_*_raw.csv")), *sorted(RAW.glob("d_vi_*_summary.csv")),
    *sorted(RAW.glob("e_pico_*_raw.csv")), *sorted(RAW.glob("e_pico_*_summary.csv")),
    *sorted(RAW.glob("f_*_raw.csv")), *sorted(RAW.glob("f_*_summary.csv")),
]
original_hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest().upper() for p in original_paths}
old_sums = {}
for line in SUMS.read_text(encoding="utf-8").splitlines():
    digest, relative = line.split(None, 1)
    old_sums[relative.strip()] = digest

with (RESULTS / "b_topologia.csv").open(newline="", encoding="utf-8-sig") as f:
    core_type = {r["physical_core_api_key"]: r["core_type"] for r in csv.DictReader(f)}


def median(values: list[float]) -> float:
    return statistics.median(values)


def quantile(values: list[float], p: float) -> float:
    ordered = sorted(values)
    return ordered[math.floor(p * (len(ordered) - 1))]


def spread(values: list[float]) -> tuple[float, float, float, float, float]:
    center = median(values)
    iqr = quantile(values, .75) - quantile(values, .25)
    mad = median([abs(v - center) for v in values])
    return center, iqr, mad, min(values), max(values)


def raw_samples(pattern: str) -> list[dict[str, object]]:
    rows = []
    for path in sorted(RAW.glob(pattern)):
        with path.open(newline="", encoding="utf-8-sig") as f:
            for row in csv.DictReader(f):
                ops_count = int(row["macs"])
                if ops_count % 2:
                    raise ValueError(f"odd ops count in {path.name}, rep {row['rep']}")
                conv_macs = ops_count // 2
                elapsed_ns = float(row["group_elapsed_ns"])
                if elapsed_ns <= 0:
                    raise ValueError(f"nonpositive elapsed time in {path.name}, rep {row['rep']}")
                rows.append({
                    "source_raw_file": path.name,
                    "series": row["series"],
                    "rep": int(row["rep"]),
                    "warmup_count": int(row["warmup_count"]),
                    "N": int(row["N"]),
                    "worker_key": row["worker_key"],
                    "worker_type": row["worker_type"],
                    "processor_group": int(row["processor_group"]),
                    "logical_processor": int(row["logical_processor"]),
                    "route": row["route"],
                    "group_elapsed_ns": elapsed_ns,
                    "original_ops_count": ops_count,
                    "conventional_MACs": conv_macs,
                    "logical_bytes": int(row["logical_bytes"]),
                    "conventional_MAC_per_second": conv_macs / (elapsed_ns * 1e-9),
                    "discarded": row["discarded"],
                })
    return rows


d_reps = raw_samples("d_vi_*_raw.csv")
e_reps = raw_samples("e_pico_*_raw.csv")
if len(d_reps) != 16 * 31 or len(e_reps) != 32 * 31:
    raise ValueError(f"unexpected raw row counts: d={len(d_reps)}, e={len(e_reps)}")

rep_fields = ["source_raw_file", "series", "rep", "warmup_count", "N", "worker_key", "worker_type",
              "processor_group", "logical_processor", "route", "group_elapsed_ns", "original_ops_count",
              "conventional_MACs", "logical_bytes", "conventional_MAC_per_second", "discarded"]
for name, rows in (("d_vi_MACconv_reps.csv", d_reps),):
    with (RESULTS / name).open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=rep_fields); w.writeheader(); w.writerows(rows)

d_groups: dict[str, list[dict[str, object]]] = {}
for row in d_reps:
    d_groups.setdefault(str(row["worker_key"]), []).append(row)
d_core_rows = []
for key, rows in sorted(d_groups.items()):
    values = [float(r["conventional_MAC_per_second"]) for r in rows]
    med, iqr, mad, low, high = spread(values)
    first = rows[0]
    d_core_rows.append({
        "worker_key": key, "worker_type": first["worker_type"], "series": first["series"], "route": first["route"],
        "metric": "conventional_MAC_per_second", "median_conventional_MAC_per_second": med,
        "iqr_conventional_MAC_per_second": iqr, "mad_conventional_MAC_per_second": mad,
        "min_conventional_MAC_per_second": low, "max_conventional_MAC_per_second": high,
        "N": len(values), "warmup_count": first["warmup_count"], "outlier_discard_count": 0,
    })
d_fields = list(d_core_rows[0])
with (RESULTS / "d_vi_MACconv.csv").open("w", newline="", encoding="utf-8") as f:
    w = csv.DictWriter(f, fieldnames=d_fields); w.writeheader(); w.writerows(d_core_rows)

d_type_rows = []
for kind in ("P-core", "E-core"):
    values = [float(r["median_conventional_MAC_per_second"]) for r in d_core_rows if r["worker_type"] == kind]
    med, iqr, mad, low, high = spread(values)
    d_type_rows.append({"worker_type": kind, "physical_core_count": len(values),
                        "median_of_core_medians_conventional_MAC_per_second": med,
                        "iqr_of_core_medians_conventional_MAC_per_second": iqr,
                        "mad_of_core_medians_conventional_MAC_per_second": mad,
                        "min_core_median_conventional_MAC_per_second": low,
                        "max_core_median_conventional_MAC_per_second": high,
                        "N_per_core": 31, "warmup_count": 5, "outlier_discard_count": 0})
with (RESULTS / "d_vi_MACconv_resumen.csv").open("w", newline="", encoding="utf-8") as f:
    w = csv.DictWriter(f, fieldnames=list(d_type_rows[0])); w.writeheader(); w.writerows(d_type_rows)

e_groups: dict[tuple[str, str, str], list[dict[str, object]]] = {}
for row in e_reps:
    key = (str(row["worker_key"]), str(row["worker_type"]), str(row["route"]))
    e_groups.setdefault(key, []).append(row)
e_summary = []
for (worker_key, kind, route), rows in sorted(e_groups.items()):
    values = [float(r["conventional_MAC_per_second"]) for r in rows]
    med, iqr, mad, low, high = spread(values)
    e_summary.append({"worker_key": worker_key, "worker_type": kind, "route": route,
                      "median": med, "iqr": iqr, "mad": mad, "min": low, "max": high,
                      "N": len(values), "source_raw_file": str(rows[0]["source_raw_file"])})
peaks: dict[tuple[str, str], dict[str, object]] = {}
for row in e_summary:
    key = (str(row["worker_type"]), str(row["route"]))
    if key not in peaks or float(row["median"]) > float(peaks[key]["median"]):
        peaks[key] = row
lines = [
    "# e — throughput derivado en MAC convencionales/s", "",
    "Derivado directamente de las 31 filas por núcleo en los CSV crudos e: `conventional_MACs = macs/2`; `MAC/s = conventional_MACs / (group_elapsed_ns*1e-9)`. `macs` del original cuenta dos operaciones por MAC convencional. El pico por ruta/tipo es el núcleo con mayor mediana convencional; no se repitieron series.", "",
    "| Tipo | Ruta | Núcleo pico | Mediana convencional MAC/s | IQR | MAD | Mín. | Máx. |", "|---|---|---|---:|---:|---:|---:|---:|",
]
route_names = {"ISA_BASE_AVX2": "AVX2", "ISA_OPT_AVX_VNNI_256": "AVX-VNNI-256"}
for (kind, route), row in sorted(peaks.items()):
    lines.append(f"| {kind} | {route_names.get(route, route)} | {row['worker_key']} | {float(row['median']):.6g} | {float(row['iqr']):.6g} | {float(row['mad']):.6g} | {float(row['min']):.6g} | {float(row['max']):.6g} |")
lines.extend(["", "Fuente: `stage2_raw/e_pico_*_raw.csv`; cada llamada tiene `O=512,I=512,m=1`, 128 llamadas por repetición. Las estadísticas se calcularon sobre la tasa convencional por repetición derivada de `group_elapsed_ns`."])
(RESULTS / "e_MACconv.md").write_text("\n".join(lines) + "\n", encoding="utf-8")

unit_note = """# Corrección de unidades (derivados; originales preservados)

## Archivos/columnas originales que expresan ops/s

- `stage2_raw/d_vi_*_raw.csv` y `stage2_raw/e_pico_*_raw.csv`: la columna `macs` contiene `2*O*I*calls` (dos operaciones por multiply-accumulate convencional); dividir por `group_elapsed_ns` da ops/s, no MAC convencionales/s.
- `stage2_raw/d_vi_*_summary.csv` y `stage2_raw/e_pico_*_summary.csv`: `metric=MAC_per_second` es el numerador anterior; columnas `median,iqr,mad,min,max` están en ops/s.
- Original `d_vi.csv`: `metric=MAC_per_second`; `median,iqr,mad,min,max` están en ops/s aunque la etiqueta diga MAC/s.
- Original `d_vi_resumen.csv`: `median_of_core_medians_MAC_per_second`, `iqr_of_core_medians`, `mad_of_core_medians`, `min_core_median`, `max_core_median` están en ops/s.
- Original `e.md`: `Mediana MAC/s, IQR, MAD, Mín., Máx.` están en ops/s aunque las cabeceras digan MAC/s.

## Archivos derivados que expresan MAC convencionales/s

- `d_vi_MACconv_reps.csv`: `conventional_MACs=original_ops_count/2`; `conventional_MAC_per_second` usa `group_elapsed_ns` crudo.
- `d_vi_MACconv.csv`: `median_conventional_MAC_per_second`, `iqr_conventional_MAC_per_second`, `mad_conventional_MAC_per_second`, `min_conventional_MAC_per_second`, `max_conventional_MAC_per_second`.
- `d_vi_MACconv_resumen.csv`: agregados P/E de las medianas por núcleo, con las mismas unidades convencionales.
- `e_MACconv.md`: todos los valores numéricos de throughput son MAC convencionales/s, derivados por fila del raw e y divididos por el elapsed QPC de esa muestra.

Los archivos originales `d_vi.csv`, `d_vi_resumen.csv`, `e.md`, los CSV crudos/resúmenes d/e/f y `f_r_summary.csv` no se modificaron. No se reejecutaron mediciones.

## Confirmación de bytes y r

En `doWork` (`src/t0_measure_harness.cpp`), cada Q4 llamada aplica `O=512,I=512,m=1`; `WeightMatrix::payload_bytes()` es `O*(I/32)*18 = 147,456 B` (16 B de nibbles + 2 B FP16 por grupo de 32 pesos). El bucle Q4 hace una llamada y luego suma exactamente `payload_bytes()*calls`: para una llamada cuenta 147,456 B de pesos Q4 lógicos una vez. Escalas ya están dentro de los 18 B; activaciones, scratch/output y transferencias de caché no se cuentan. Con `m=1`, cada peso se consume una vez por llamada.

`BW_D` cuenta `2*source.size()*calls`: un read y un write lógicos por `memcpy` de 125,829,120 B. El código también modifica un byte de `source` y lee un byte de `destination` para el sink por llamada; esas pocas referencias accesorias no se incluyen en el volumen BW_D declarado. `r` divide tasas de bytes lógicos/s Q4 (`BW_L2,Σ`, solo payload de pesos) por tasas de bytes lógicos/s de copia (`BW_D`, lectura+escritura de buffer). Es dimensionalmente coherente como cociente de volúmenes lógicos definidos por el benchmark, no afirma tráfico físico equivalente: no validar `BW_L2`/`r` como bytes transferidos desde L2/DRAM.
"""
(RESULTS / "unit_correction.md").write_text(unit_note, encoding="utf-8")

for path, digest in original_hashes.items():
    if hashlib.sha256(path.read_bytes()).hexdigest().upper() != digest:
        raise RuntimeError(f"original artifact changed unexpectedly: {path}")

entries = dict(old_sums)
for relative in ("src/derive_macconv.py", "results/T0.0/d_vi_MACconv_reps.csv", "results/T0.0/d_vi_MACconv.csv",
                 "results/T0.0/d_vi_MACconv_resumen.csv", "results/T0.0/e_MACconv.md", "results/T0.0/unit_correction.md"):
    entries[relative] = ""
for relative in sorted(entries):
    path = T0 / relative
    if path.is_file():
        entries[relative] = hashlib.sha256(path.read_bytes()).hexdigest().upper()
SUMS.write_text("".join(f"{entries[relative]}  {relative}\n" for relative in sorted(entries)), encoding="utf-8")
print(json.dumps({"derived_d_rows": len(d_reps), "derived_e_rows": len(e_reps),
                  "derived_d_cores": len(d_core_rows), "derived_e_core_routes": len(e_summary),
                  "originals_unchanged": len(original_hashes), "checksum_entries": len(entries)}))
