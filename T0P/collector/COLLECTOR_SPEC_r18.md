# Especificación ejecutable del collector R18

## Alcance

El collector valida y deriva desde crudos write-once, schedule congelado y configuración ligada por SHA-256. Un smoke no adjudica. `--official` exige autorización válida; esta tarea no leyó un AUTHORIZED real ni abrió una sesión oficial.

## Plan, schedule y cadena D14

`residency_plan_r18.py` verifica el plan completo, los 31 órdenes gobernantes réplica-mayor, las condiciones A/A y los segmentos indivisibles. La cadena `official_run_r18.py` ejecuta preflight → QA/instrumentos/expulsión → cualificación → corte → residencia; el adaptador observado recibe cada transición y solo permite un reintento técnico global. Razones tipadas: duración insuficiente, frecuencia inestable, pérdida transitoria de telemetría y pérdida transitoria de EMON. Las pausas entre réplicas no consumen el presupuesto; segundo HOLD es terminal; fallo científico no se reintenta.

La prueba ejecutada `official_run_r18.py --self-test` informa 21 casos: `D14_runtime_retry=PASS ... adapter_observed=qualification_attempt_1+technical_retry_01 planned_pause_consumes=0`, `D14_second_hold=PASS`, `D14_scientific_fail=PASS`, 40 mutaciones de campos AUTH rechazadas. `--official` sin AUTH terminó con `OFFICIAL_NO_AUTH_EXIT=20` y `measurements_started=false`.

## S-20 FULL, D10 y D13

`telemetry_r18.py` construye grupos y comparaciones desde plan/schedule y filas adquiridas, liga configuración, QPC/QPF, eventos EMON, mapa topológico, segmentos y probes por hashes, y usa `s20_validator_r18.py` como único validador. `_phase_science` y `_phase_derivation` no declaran listas de IDs admisibles. Mutaciones sintéticas de S-20 y D13 se ejecutaron: `S20_mutations=4 D13=PASS`.

En P1, S-20 corrió en modo `full` sobre 3.120 muestras, 68.766 intervalos y 113.356 filas de clock map. Resultado `INVALID` con razones `CACHE_DENOM_ZERO`, `CHANNEL_VALUE_MISSING`, `CLOCK_MAP_UNCERTAINTY`, `CONTROL_PEERS_MISSING`, `COVERAGE_LOW`, `ENERGY_VALUE_INVALID`, `GAP_TOO_LARGE`, `THERMAL_READING_INVALID`, `WINDOW_COUNT_LOW`. La auditoría conserva hashes de plan, preflight, QA, schedule, eventos y configuración. El resultado `INVALID` corresponde al corte SMOKE; no es un PASS científico.

S22 fue `PASS` con 1.488/1.488 filas core, `max_H2=0,00013687`, `max_H3/D2=0,00004417`, `min_D2=17.309.886.395`. S-25 inicio/cierre fue `PASS`, 186/186 probes únicas y completas, 31 muestras por endpoint/segmento. Preflight y QA/instrumentos/expulsión fueron PASS; cualificación y residencia, `SMOKE_ONLY`. Controles D8 finales y ratio frío/caliente son `INVALID: D8_REVALIDATION_NOT_ACQUIRED_IN_SMOKE_PHASE_CUT` y `INVALID: COLD_HOT_RATIO_REQUIRES_HASHED_D8_FINAL_CONTROL_CAPTURE`.

El validador comprueba todas las ventanas antes de persistir. `s20_validation_r18.json` conserva estado/razones, métricas de muestra/core, recuentos de ventana y extremos numéricos; los arrays extensos se resumen después de validar para respetar el límite de 1 GiB. El lineage D13 enlaza el resultado compacto con todos los crudos; S25 evalúa el resultado completo en memoria antes de resumirlo.

## Derivación S-19/D11

`residency_derive_r18.py` exige unión exacta de IDs para adjudicar, episodios y márgenes por worker, S-20 válido y evidencia L2/LLC ligada a crudos. La ruta oficial usa 31 réplicas gobernantes. Se ejecutó desde los crudos P1: estado `SMOKE_ONLY`, ejecutados 264/13.688, faltan 13.424, extras 0, unión exacta falsa. El self-test D11 pasó 13 ramas y 13.688 eventos de fixture; incluye rechazo de falta de muestra/procedencia y estados inválidos.

## S-25-B y salidas

El schedule N=1 ejecutó sus seis eventos y obtuvo HOLD real por `S25B_S20_temporal_capture_not_valid`; no quedó error del parser/evaluador. S22 496/496 y probes 62/62 pasan en ese diagnóstico. El schedule FULL36 queda reservado al juez y no se ejecutó.

Crudos y derivados se guardan bajo `logs_r18/obj_r18/<attempt>` con `raw_manifest_r18` y `derived_lineage_r18`. Los manifiestos SHA-256 de publicación cubren los archivos fuente y especificaciones R18, excluyen logs, crudos, intentos y temporales.

## Comandos de prueba ejecutados

```powershell
python -B T0P\collector\telemetry_r18.py self-test --output T0P\collector\logs_r18\obj_r18\telemetry_r18_compact_selftest.txt
python -B T0P\collector\verify_residency_smoke_r18.py --attempt T0P\collector\logs_r18\obj_r18\smoke_chain_n3_compact_replay_r18_20261013
python -B T0P\collector\residency_derive_r18.py derive --plan <attempt>\residency_plan_r18.json --events <attempt>\residency_events_r18.jsonl --episodes <attempt>\residency_episodes_r18.jsonl --attempt-directory <attempt> --output <attempt>\residency_derived_r18.json
python -B T0P\collector\official_run_r18.py --self-test --output T0P\collector\logs_r18\obj_r18\official_run_r18_final_selftest_after_pipeline.txt
```

Salidas clave: `TELEMETRY_R18_QA PASS`; `RESIDENCY_SMOKE_R18_VERIFY PASS ... D13_raws_verified=37`; `RESIDENCY_DERIVE_R18 state=SMOKE_ONLY ... event_union_exact=False`; `OFFICIAL_RUN_R18_SYNTHETIC_QA PASS cases=21`.
