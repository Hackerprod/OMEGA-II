# Evidencia de smoke R17

## Cadena N=3

Comando de runner: `python -B T0P\collector\official_run_r17.py --smoke --integrated-executable T0P\harness\logs_r17\obj_r17\harness_r17.exe --plan T0P\collector\logs_r17\obj_r17\residency_plan_r17.json --attempt-directory T0P\collector\logs_r17\obj_r17\n3_chain_smoke_r17 --emon <ruta EMON del entorno>`. El path del ejecutable EMON no quedó serializado en los artefactos de fase.

Schedule N=3: 264 eventos (60 calibración, 180 residencia, 24 A/A), segmentos R01–R03. Plan ligado: `f384596a19e15f834bd42e1e487d98699cb9bbc88abb54d006dab50f49a1afe9`; orden: `7fabbe0f85660d2e7215701693cebf9f1452d5fd41c47dbffb431cc449181330`.

- Preflight: PASS; QPF 10.000.000 Hz, large pages y LP 0,2,4,6,8,10,12,14.
- QA kernel/instrumentación/expulsión: PASS.
- Cualificación: `SMOKE_ONLY`, sin métricas científicas; BW_DRAM adquirida (24 filas), P_instr (9 variantes), validación auxiliar completa (240 filas).
- Corte diagnóstico: PASS; entró a residencia.
- HOLD real en R01/BEGIN, muestra 1: `S25_HOT_PROBE_DURATION_OR_INSTRUMENTATION_HOLD`, región 4,780637 s, `coordinator_overhead_fraction=7.014149`.
- EMON: `emon -stop`, `stop_exit=0`, `emon_exit=0`, QPC del padre, 1.307,234 s, 44.518.297 B crudos. Intento completo: 89.813.282 B (<1 GiB).
- No se obtuvo ningún bloque de residencia; S-20 quedó en HOLD por falta del preflight gobernante completo.

La inspección posterior localizó que las marcas de worker se copiaban después del margen final y quedaban reemplazadas por las marcas del margen R=32.768, frente a R=262.144 medido. Se corrigió el orden en `T0P\harness\residency_r17.cpp`; `verify_hot_probe_stamp_r17.py` y el QA enlazado pasan. No se repitió EMON; el resultado físico anterior sigue siendo HOLD y no valida el arreglo.

## S25-B N=1 diagnóstico

Comando de runner: `python -B T0P\collector\official_run_r17.py --instrument-smoke --integrated-executable T0P\harness\logs_r17\obj_r17\harness_r17.exe --plan T0P\collector\logs_r17\obj_r17\residency_plan_r17.json --schedule T0P\collector\logs_r17\obj_r17\instrument_qualification_smoke_n1_schedule_r17.tsv --attempt-directory T0P\collector\logs_r17\obj_r17\instrument_qualification_smoke_n1_r17 --emon <ruta EMON del entorno>`. El path EMON tampoco se guardó en el JSON de bloqueo.

Salida clave del bloqueo: `HOLD_INSTRUMENTATION_QUALIFICATION reason=S25_HOT_PROBE_DURATION_OR_INSTRUMENTATION_HOLD`; captura 23.645.815 B, intento 54.016.213 B y EMON detenido controladamente. El wrapper eliminó el intento fallido y conservó `instrument_smoke_blocker_1791869497444163900.json`. El modo FULL de 36 bloques no se ejecutó.
