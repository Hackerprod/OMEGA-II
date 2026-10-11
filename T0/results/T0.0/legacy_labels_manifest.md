# Manifiesto de etiquetas legacy — T0.0

Este manifiesto aplica etiquetas interpretativas a archivos heredados sin modificar ni renombrar ninguno. Los CSV originales se conservan byte por byte. Para la clasificación científica gobiernan MD/25 §14 y MD/28; las métricas nuevas y medidas del kernel final están en las tablas `phase1_*`.

| Archivo(s) originales (sin cambios) | Etiqueta interpretativa | Unidad real / significado | Uso permitido |
|---|---|---|---|
| `d_vi.csv`, `d_vi_resumen.csv`, `stage2_raw/d_vi_*_raw.csv`, `stage2_raw/d_vi_*_summary.csv` | `Q4_M1_PREOPT_PER_CORE` | `ops/s` de la implementación Q4 pre-optimización; no MAC/s. Una MAC convencional son 2 operaciones. | Histórico por núcleo; no sustituye el `v_i` final del kernel entregado. |
| `d_vi_MACconv.csv`, `d_vi_MACconv_resumen.csv`, `d_vi_MACconv_reps.csv` | `Q4_M1_PREOPT_PER_CORE_MACCONV` | MAC convencionales/s, calculados desde las fuentes legacy mediante `ops/2`; se conservan sus cabeceras/unidades originales. | Conversión histórica aclaratoria; no son mediciones del kernel final. |
| `e.md` | `Q4_M1_PREOPT_BY_ISA` | La tabla rotulada «MAC/s» deriva de la antigua contabilidad de operaciones (`ops/s`); no debe interpretarse como MAC convencionales/s. | Resumen legacy por ruta/tipo. |
| `e_MACconv.md` | `Q4_M1_PREOPT_BY_ISA_MACCONV` | MAC convencionales/s tras aplicar la corrección `ops/2` descrita en el propio documento. | Resumen legacy corregido, aún pre-optimización. |
| `f_ancho_banda.csv`: `f_bw_l2_i_*`, `f_bw_l2_sigma_A_4P`, `f_bw_l2_sigma_B_allP`, `f_bw_l2_sigma_C_P_plus_E` | `BW_Q4_LOGICAL_PREOPT` | Demanda lógica de payload Q4 expresada en B/s. `BW_L2` es el rótulo histórico: no demuestra tráfico físico L2. | Descriptivo de la implementación pre-optimización. No reconstruir A/C final desde sumas de singles. |
| `f_ancho_banda.csv`: `f_bw_d_n1`, `f_bw_d_n2`, `f_bw_d_n4`, `f_bw_d_n8`, `f_bw_d_n16` | `BW_memcpy_logical(n)` | B/s lógicos de copia, con lectura + escritura según `unit_correction.md`; no es el denominador `BW_DRAM_read_8P`. | Histórico de plataforma. |
| `stage2_raw/f_r_summary.csv` | `LEGACY_PREOPT_DESCRIPTIVE` | Cocientes históricos de demanda lógica Q4 preopt / bytes lógicos de memcpy: A=0.0430687, B=0.0728188, C=0.0919389. | No son tasas de concurrencia ni evidencia para H0; no regenerar ni extrapolar. |
| Rótulos antiguos `BW_L2` y `BW_D` en informes/archivos de etapa | `BW_Q4_logical` y `BW_memcpy_logical(n)`, respectivamente | Renombrado semántico en texto y análisis; no implica edición de archivos fuente. | Usar los nombres actuales al describir demanda lógica. |

La etiqueta actual de `v_i` y el pico por núcleo/ruta del kernel final se consultan en `phase1_P_Q4_i_m1_by_core.csv`; la confirmación E y el pico `P_Q4_peak` están en `phase1_E_Q4_m1_by_core.csv`, `phase1_P_Q4_peak_by_core.csv` y las tablas relacionadas. A/C permanecen `DEFERRED_TO_T0_1`; B es la serie concurrente medida `P_Q4_8P_m1`. Los ratios antiguos `r_A/r_B/r_C` permanecen exclusivamente `LEGACY_PREOPT_DESCRIPTIVE`.
