# e — throughput derivado en MAC convencionales/s

Derivado directamente de las 31 filas por núcleo en los CSV crudos e: `conventional_MACs = macs/2`; `MAC/s = conventional_MACs / (group_elapsed_ns*1e-9)`. `macs` del original cuenta dos operaciones por MAC convencional. El pico por ruta/tipo es el núcleo con mayor mediana convencional; no se repitieron series.

| Tipo | Ruta | Núcleo pico | Mediana convencional MAC/s | IQR | MAD | Mín. | Máx. |
|---|---|---|---:|---:|---:|---:|---:|
| E-core | AVX2 | CORE_API_11_G0_MASK_19 | 7.41092e+08 | 6.54508e+06 | 2.32024e+06 | 7.14071e+08 | 7.44626e+08 |
| E-core | AVX-VNNI-256 | CORE_API_11_G0_MASK_19 | 7.30637e+08 | 1.02371e+07 | 2.96316e+06 | 6.71408e+08 | 7.34548e+08 |
| P-core | AVX2 | CORE_API_06_G0_MASK_12_13 | 1.13417e+09 | 1.17721e+07 | 5.70225e+06 | 1.11679e+09 | 1.17141e+09 |
| P-core | AVX-VNNI-256 | CORE_API_07_G0_MASK_14_15 | 1.13303e+09 | 1.64993e+07 | 8.09844e+06 | 1.0995e+09 | 1.14839e+09 |

Fuente: `stage2_raw/e_pico_*_raw.csv`; cada llamada tiene `O=512,I=512,m=1`, 128 llamadas por repetición. Las estadísticas se calcularon sobre la tasa convencional por repetición derivada de `group_elapsed_ns`.
