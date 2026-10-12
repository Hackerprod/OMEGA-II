# EVICT_QA_RESULTS — cualificación por bloques con márgenes

## Resultado de la única repetición nueva

**EVICTION_QUALIFIED.** Se ejecutó una sola repetición con EMON CLI oficial (`-collect-edp` y `emon_joint_qualification.edp`), salida conjunta completa: 3.600/3.600 grupos, exit 0. La captura, las marcas QPC/UTC, el mapa de reloj, la salida cruda, los resultados y el analizador reproducible están en `capture_blocks_r2/`.

Método oficial de esta cualificación: para cada condición se mantuvo la carga de forma continua entre `[t_pad_ini,t_pad_fin]`; la región medida `[t_ini,t_fin]` queda dentro, con **m_ini,m_fin ≥ 0,200 s** de la misma condición. El harness fijó 0,200 s por lado para las condiciones P y 0,250 s para `arch_evict8`; márgenes mínimos medidos: **200,026 ms** (P) y **250,375/253,450 ms** (inicio/final arquitectónico). Los márgenes no forman parte de las muestras de rendimiento ni del denominador de cobertura. Ventana incluida si solapa la región y está completamente dentro del acolchado; cruce excluido y reportado, sin interpolar. Ninguna ventana cruzó el acolchado en esta ejecución. Esto difiere de llamar “bloque” a la propia región medida: la región es interior, y solo las ventanas EMON completas protegidas por la misma condición pueden cubrirla. El módulo común queda documentado para ratificación de Sol.

La unión de ventanas completas da cobertura inferior **100,000 %** en todas las filas; U=**0,892900 ms** (residuo máximo 0,200300 ms + medio bracket 0,192600 ms + cuantización civil 0,500000 ms). Hueco superior máximo **3,519 ms**; duración de ventana máxima **78,660 ms**. Todas las condiciones tienen 41–59 ventanas y duración medida ≥2,5 s. Umbrales S-20 y físicos permanecen inmutables.

El método de lectura en tiempo real queda **solo como diagnóstico**, nunca como sincronización oficial. El análisis previo de la tubería mostró alternancia de desfase +2/−60 ms, p95 64 ms y máximo 81 ms. En esta captura diagnóstica, la mayor demora detectada fue 143,628 ms; la etiqueta de mediana emitida por el harness no se calcula sobre una serie ordenada y no se usa. Los límites oficiales de esta repetición proceden de la región acolchada y del alineamiento offline EMON civil↔QPC; no del instante en que llega una línea a `stdout`.

## Resultados por P-core y condición

`ratio` es mediana de los 31 cocientes pareados; se repite en las filas de condición para facilitar auditoría. En `arch_evict8` no aplica ratio. Fracción LLC = suma de misses L3 / demandas de lectura para el par LP del P indicado y las ventanas listadas. `cold_clflush_diag`/`hot_clflush_diag` son diagnóstico.

| P | Condición | Cobertura | Hueco ms | Ventanas | Ratio | Fracción LLC | Estado |
|---:|---|---:|---:|---:|---:|---:|---|
| 0 | cold_selected | 100% | 3.008 | 46 | 6.412808 | 0.935232 | EVICTION_QUALIFIED |
| 0 | hot_selected | 100% | 3.256 | 43 | 6.412808 | 0.004528 | EVICTION_QUALIFIED |
| 0 | cold_clflush_diag | 100% | 3.043 | 58 | 6.3882 | 0.933665 | EVICTION_QUALIFIED |
| 0 | hot_clflush_diag | 100% | 3.41 | 42 | 6.3882 | 0.003593 | EVICTION_QUALIFIED |
| 1 | cold_selected | 100% | 3.152 | 46 | 6.462507 | 0.936462 | EVICTION_QUALIFIED |
| 1 | hot_selected | 100% | 3.519 | 43 | 6.462507 | 0.003515 | EVICTION_QUALIFIED |
| 1 | cold_clflush_diag | 100% | 3.291 | 59 | 6.466499 | 0.937697 | EVICTION_QUALIFIED |
| 1 | hot_clflush_diag | 100% | 2.937 | 42 | 6.466499 | 0.006961 | EVICTION_QUALIFIED |
| 2 | cold_selected | 100% | 3.179 | 46 | 6.837959 | 0.936956 | EVICTION_QUALIFIED |
| 2 | hot_selected | 100% | 3.186 | 43 | 6.837959 | 0.002633 | EVICTION_QUALIFIED |
| 2 | cold_clflush_diag | 100% | 3.363 | 58 | 6.750848 | 0.928849 | EVICTION_QUALIFIED |
| 2 | hot_clflush_diag | 100% | 2.89 | 42 | 6.750848 | 0.003751 | EVICTION_QUALIFIED |
| 3 | cold_selected | 100% | 3.154 | 46 | 6.910052 | 0.941217 | EVICTION_QUALIFIED |
| 3 | hot_selected | 100% | 3.191 | 41 | 6.910052 | 0.001964 | EVICTION_QUALIFIED |
| 3 | cold_clflush_diag | 100% | 3.25 | 57 | 6.782818 | 0.92519 | EVICTION_QUALIFIED |
| 3 | hot_clflush_diag | 100% | 3.278 | 43 | 6.782818 | 0.00636 | EVICTION_QUALIFIED |
| 4 | cold_selected | 100% | 3.126 | 44 | 7.102952 | 0.923824 | EVICTION_QUALIFIED |
| 4 | hot_selected | 100% | 3.122 | 42 | 7.102952 | 0.006039 | EVICTION_QUALIFIED |
| 4 | cold_clflush_diag | 100% | 3.3 | 58 | 7.099247 | 0.909836 | EVICTION_QUALIFIED |
| 4 | hot_clflush_diag | 100% | 3.352 | 42 | 7.099247 | 0.005689 | EVICTION_QUALIFIED |
| 5 | cold_selected | 100% | 3.117 | 46 | 7.009334 | 0.94162 | EVICTION_QUALIFIED |
| 5 | hot_selected | 100% | 3.205 | 42 | 7.009334 | 0.003764 | EVICTION_QUALIFIED |
| 5 | cold_clflush_diag | 100% | 3.427 | 57 | 6.987736 | 0.923071 | EVICTION_QUALIFIED |
| 5 | hot_clflush_diag | 100% | 2.989 | 42 | 6.987736 | 0.003229 | EVICTION_QUALIFIED |
| 6 | cold_selected | 100% | 3.289 | 46 | 6.978457 | 0.959142 | EVICTION_QUALIFIED |
| 6 | hot_selected | 100% | 3.232 | 42 | 6.978457 | 0.001933 | EVICTION_QUALIFIED |
| 6 | cold_clflush_diag | 100% | 3.011 | 56 | 6.88008 | 0.948994 | EVICTION_QUALIFIED |
| 6 | hot_clflush_diag | 100% | 3.415 | 42 | 6.88008 | 0.001688 | EVICTION_QUALIFIED |
| 7 | cold_selected | 100% | 3.16 | 47 | 6.824909 | 0.963489 | EVICTION_QUALIFIED |
| 7 | hot_selected | 100% | 3.203 | 43 | 6.824909 | 0.001708 | EVICTION_QUALIFIED |
| 7 | cold_clflush_diag | 100% | 2.963 | 57 | 6.830523 | 0.946812 | EVICTION_QUALIFIED |
| 7 | hot_clflush_diag | 100% | 2.988 | 42 | 6.830523 | 0.00164 | EVICTION_QUALIFIED |
| 0 | arch_evict8 | 100% | 3.045 | 41 | N/A | 0.016253 | EVICTION_QUALIFIED |
| 1 | arch_evict8 | 100% | 3.045 | 41 | N/A | 0.020048 | EVICTION_QUALIFIED |
| 2 | arch_evict8 | 100% | 3.045 | 41 | N/A | 0.015314 | EVICTION_QUALIFIED |
| 3 | arch_evict8 | 100% | 3.045 | 41 | N/A | 0.011115 | EVICTION_QUALIFIED |
| 4 | arch_evict8 | 100% | 3.045 | 41 | N/A | 0.030019 | EVICTION_QUALIFIED |
| 5 | arch_evict8 | 100% | 3.045 | 41 | N/A | 0.023948 | EVICTION_QUALIFIED |
| 6 | arch_evict8 | 100% | 3.045 | 41 | N/A | 0.014253 | EVICTION_QUALIFIED |
| 7 | arch_evict8 | 100% | 3.045 | 41 | N/A | 0.010838 | EVICTION_QUALIFIED |

## QA y bloqueos

La selección congelada fue CLFLUSHOPT por CPUID.07H:EBX[23]; CLFLUSH quedó diagnóstico. N=31 frío/caliente por cada P-core (LP 0,2,4,6,8,10,12,14). Ratios seleccionados: 6,412808–7,102952 (mínimo exigido 1,50). LLC fría seleccionada: 0,923824–0,963489 (mínimo 0,90); LLC caliente: 0,001708–0,006039 (máximo 0,10). La repetición arquitectónica ejecutó 8 shards × O=1024, 294.912 B por shard, ocho workers concurrentes; barrera y mapa de líneas pasaron. LLC por P en `arch_evict8`: 0,010838–0,030019; ratio N/A.

Pruebas deterministas del módulo común: cruce de condición excluido/reportado, margen insuficiente, hueco real >20 ms, cobertura <95 %, duplicado y orden inválido: PASS. También timeout, EMON detenido, marca duplicada/fuera de orden y retardo sobre presupuesto del diagnóstico de sincronización: PASS como rechazos. Resultados: `T0P/telemetry/reader/sync/sync_qa_raw_r1.txt`; matriz calculada en `capture_blocks_r2/padded_block_analysis.csv`.

No hay bloqueos de instrumentación ni de criterio físico en esta repetición. Estado global: **EVICTION_QUALIFIED**. No se relajó S-20 ni ningún umbral.


