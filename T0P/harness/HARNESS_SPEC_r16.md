# HARNESS_SPEC_r16

## Alcance

`harness_r16.exe` implementa la adquisición SMOKE S-19/S-23 para una celda (`W=288`, `O=1024`, `m=1`, `K=16`), tres réplicas A/B/C y tres pares A/A. No lanza la sesión oficial. El plan completo y sus IDs están en `collector/residency_plan_r16.py`: 420 condiciones y 31 réplicas.

La entrada integrada `--official` delega al orquestador R16; sin AUTH válido falla cerrado antes de medir. `--synthetic-self-test` recorre la máquina de fases con lector/harness sintéticos etiquetados, y `--qa-r16` ejecuta QA del kernel sin abrir sesión.

## Construcción y ejecución

- Construir: `T0P\harness\build_r16.cmd`; genera `logs_r16\obj_r16\harness_r16.exe` y enlaza `T0P\evict\evict.cpp/.hpp` sin modificarlos.
- Smoke integral: `T0P\harness\run_residency_smoke_r16.ps1 -AttemptName <nombre-nuevo>`; prepara D13, EMON `-collect-edp` continuo, ejecuta el harness, espera el cierre normal de EMON, llama al único validador S-20 y al derivador.
- Harness directo: `harness_r16.exe --residency-smoke-r16 <directorio> <schedule.tsv>`.
- QA kernel: `harness_r16.exe --qa-r16`.
- Cadena sintética: `harness_r16.exe --synthetic-self-test`.

El entry integrado invoca `t0common::enable_large_page_privilege()` antes de delegar a Python. Si Windows deniega el privilegio o la asignación de páginas grandes falla, devuelve un HOLD con el error del sistema y no registra medidas válidas.

## Buffers y condiciones

- A y C comparten los mismos objetos `PackedWeights` vivos: dirección, longitud, SHA-256 y `region_instance_id` quedan registrados por worker. No se crea una copia A/C para el tratamiento.
- B reserva K copias independientes por worker y verifica que no se solapen y que tengan los mismos bytes antes de medir.
- Las activaciones y pesos se preparan antes de los episodios; A/B/C expulsan mediante la primitiva enlazada `omega_evict::evict_range_record`.
- S-12 valida las salidas de los workers y las huellas verifican igualdad de bytes para A/C y observaciones A/A.

## Temporización y expulsión

Cada aplicación registra `start_tsc`, `end_tsc`, `start_qpc`, `end_qpc` y `t_app_us`; cada episodio conserva límites QPC/TSC y número de episodio. El sampler registra QPC antes/después y FILETIME. Cada observación de residencia tiene margen pre y post de la misma condición ≥0.200 s, con carga continua. R se duplica hasta que los eventos A/B/C del mismo `W,m,K` alcancen 2.5 s agregados de kernel; A/B/C usan el mismo R.

La única primitiva de expulsión invocada es `omega_evict::evict_range_record`. Cada episodio deja `eviction_manifest_r16.jsonl` con método, llamadas observadas/esperadas y líneas expulsadas. Los contadores separan rangos de pesos y activaciones y fallan ante discrepancias. Las fuentes R16 no contienen rutina paralela de flush/expulsión.

## D13 y artefactos

`telemetry_r16.py prepare` crea un intento D13 nuevo mediante `CREATE_NEW`, copia plan, schedule y EDP y registra hashes. Los crudos se cierran y sellan al terminar harness/EMON; el validador único escribe su resultado sin reescribir el original. `derive()` solo escribe salidas derivadas en el intento.

Crudos principales: eventos, episodios, calibraciones R, márgenes, bloqueos, mapa de reloj, manifiesto de expulsión, raw/stderr EMON, stdout/stderr del harness, plan, schedule y `events.edp`. Toda ejecución SMOKE lleva `numeric_metrics_used=false`.

En la corrida R16 del 2026-10-12: el harness N=3 pasó y D9 fue PASS; EMON terminó con footer completo tras 144,000 grupos. S-20 rechazó la captura por `FREQ_WINDOW_EXCURSION` y el derivador informó `SMOKE_ONLY` con unión de 18/13,688 eventos. La adquisición se conserva como prueba de cadena, no como resultado adjudicable.

## Pruebas reproducibles

- `python -B T0P\collector\residency_plan_r16.py self-test`
- `python -B T0P\collector\qualify_r16.py --self-test`
- `python -B T0P\collector\telemetry_r16.py self-test`
- `python -B T0P\collector\residency_derive_r16.py self-test`
- `rg -n 'flush_region|CLFLUSH|clflush|clflushopt|_mm_clflush|evict_weights_regions_record' T0P\harness\residency_r16.cpp T0P\harness\main_r16.cpp T0P\harness\harness_r16.hpp T0P\harness\provider_common_r16.hpp` (sin coincidencias).
