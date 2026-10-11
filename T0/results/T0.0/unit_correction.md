# Corrección de unidades (derivados; originales preservados)

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
