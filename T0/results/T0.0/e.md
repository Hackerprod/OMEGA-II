# T0.0-e — Pico observado por ruta y tipo de núcleo

Pico = mayor mediana de throughput entre los núcleos del tipo indicado; sin descarte de outliers.
Todos los valores proceden de las series N=31 del harness.

| Tipo | Ruta | Núcleo pico | Mediana MAC/s | IQR | MAD | Mín. | Máx. |
|---|---|---|---:|---:|---:|---:|---:|
| E-core | AVX-VNNI-256 | CORE_API_11_G0_MASK_19 | 1.46127e+09 | 2.04743e+07 | 5.92632e+06 | 1.34282e+09 | 1.4691e+09 |
| E-core | AVX2 | CORE_API_11_G0_MASK_19 | 1.48218e+09 | 1.30902e+07 | 4.64047e+06 | 1.42814e+09 | 1.48925e+09 |
| P-core | AVX-VNNI-256 | CORE_API_07_G0_MASK_14_15 | 2.26605e+09 | 3.29987e+07 | 1.61969e+07 | 2.19899e+09 | 2.29677e+09 |
| P-core | AVX2 | CORE_API_06_G0_MASK_12_13 | 2.26834e+09 | 2.35441e+07 | 1.14045e+07 | 2.23357e+09 | 2.34282e+09 |
