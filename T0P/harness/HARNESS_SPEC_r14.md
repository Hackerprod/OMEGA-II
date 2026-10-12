# HARNESS_SPEC_r14

## Alcance

`harness_r14.exe` implementa la adquisición SMOKE de residencia S-19/S-23 para una celda arquitectónica (`W=288`, `O=1024`, `m=1`, `K=16`), con tres réplicas de A/B/C y tres pares A/A observados. El ejecutor no abre una sesión oficial y `main_r14.cpp --official` falla cerrado. La geometría de campaña completa y sus IDs los define `collector/residency_plan_r14.py`; el smoke no sustituye las 420 condiciones ni sus 31 réplicas.

## Construcción y ejecución

- Construcción: `T0P\harness\build_r14.cmd`; genera `logs_r14\obj_r14\harness_r14.exe` y enlaza `T0P\evict\evict.cpp`/`evict.hpp` sin modificarlos.
- Pipeline SMOKE: `T0P\harness\run_residency_smoke_r14.ps1 -AttemptName <nombre-nuevo>`; prepara intento D13 con un grupo de eventos y límite EMON `-l144000`, ejecuta EMON oficial en continuo, ejecuta harness, llama al validador S-20 único y al derivador.
- Harness directo: `harness_r14.exe --residency-smoke-r14 <directorio> <schedule.tsv>`.
- Pruebas sintéticas del plan, S-20/D13 y derivador: comandos documentados en `COLLECTOR_SPEC_r14.md`.

El proceso intenta habilitar páginas grandes mediante `t0common::enable_large_page_privilege()` antes de crear buffers. Si la API del sistema deniega privilegio o las regiones no resultan grandes, escribe `BLOCKED_REAL` con el error del sistema y no registra una medición como válida.

## Buffers y condiciones

- A y C usan los mismos objetos `PackedWeights` vivos: dirección, longitud, SHA256 de bytes y `region_instance_id` se registran por trabajador. Ninguna copia A/C se crea para el tratamiento.
- B asigna `K` copias independientes por trabajador y verifica no solapamiento y bytes iguales antes de medir.
- Las activaciones y los pesos se preparan antes de episodios; las rutas A/B/C hacen la expulsión mediante la primitiva enlazada de `T0P\evict`.
- S12 valida la salida de cada trabajador y las huellas permiten verificar igualdad entre A/B/C y las observaciones A/A.

## Temporización y expulsión

Cada aplicación conserva `start_tsc`, `end_tsc`, `start_qpc`, `end_qpc` y `t_app_us`; cada episodio conserva límites QPC/TSC y número de episodio. El sampler de reloj registra QPC antes/después y FILETIME. Cada condición medida tiene márgenes pre y post de la misma condición de al menos 0,200 s, con carga continua. `R` se duplica desde 1 hasta que los tres eventos A/B/C del mismo `W,m,K` cumplen 2,5 s de kernel agregado; el mismo R se usa para A/B/C.

La única primitiva de expulsión invocada es `omega_evict::evict_range_record`. Cada episodio deja `eviction_manifest_r14.jsonl` con método, llamadas observadas/esperadas y líneas expulsadas. Los contadores separan rangos de pesos y activaciones, y se falla ante discrepancia. No hay `flush_region`, CLFLUSH ni rutina paralela de expulsión en las fuentes r14.

## D13 y archivos

`telemetry_r14.py prepare` crea un intento D13 nuevo con `CREATE_NEW`, copia plan, schedule y EDP, y registra hashes. Los crudos de harness/EMON se registran al cerrar la adquisición; el validador los sella en el almacén de artefactos write-once. `derive()` escribe únicamente salidas derivadas dentro del intento. No se reescribe un crudo existente.

Crudos principales: `residency_events_r14.jsonl`, `residency_episodes_r14.jsonl`, `residency_calibration_trials_r14.jsonl`, `residency_margins_r14.jsonl`, `residency_margin_episodes_r14.jsonl`, `block_marks_r14.csv`, `clock_map.csv`, `eviction_manifest_r14.jsonl`, `emon_raw.txt`, `emon_stderr.txt`, `harness_stdout.txt`, `harness_stderr.txt`, plan, schedule y `events.edp`. Un resultado SMOKE se marca `numeric_metrics_used=false` y no se usa para adjudicación.

## Evidencia reproducible

- Plan: `python -B T0P\collector\residency_plan_r14.py self-test`.
- QA de captura/derivación: `python -B T0P\collector\qualify_r14.py --self-test`, `python -B T0P\collector\residency_derive_r14.py self-test`, `python -B T0P\collector\telemetry_r14.py self-test`.
- Búsqueda de rutina antigua: `rg -n 'flush_region|CLFLUSH|clflush|clflushopt|_mm_clflush|evict_weights_regions_record' T0P\harness\residency_r14.cpp T0P\harness\main_r14.cpp T0P\harness\harness_r14.hpp T0P\harness\provider_common_r14.hpp` (sin coincidencias).
