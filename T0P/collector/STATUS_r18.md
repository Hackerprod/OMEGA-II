# Estado R18 — 2026-10-13

Solo se modificaron copias R18 bajo `T0P\harness` y `T0P\collector`; evidencia en `logs_r18\obj_r18`. No se abrió sesión oficial ni se leyó AUTHORIZED real. `SeLockMemoryPrivilege` quedó ENABLED en los procesos de adquisición.

## P1 — S25-A/Q/T, cadena N=3

Comando: `& T0P\harness\run_residency_smoke_r18.ps1 -AttemptName smoke_chain_n3_r18_finalfix_20261013`.

QPC padre 1376342624669→1417751259612, QPF 10 MHz, 4.140,863 s; EMON 142.301.061 B; `emon -stop`, stop=0 y EMON=0. Capturó 264/264 eventos hasta `residency|architecture|C|m16|K16|W288|O1024|r3`. La captura no tuvo HOLD por worker/probe; cualificación y residencia quedaron `SMOKE_ONLY`. La verificación final del replay con crudos SHA enlazados dio `RESIDENCY_SMOKE_R18_VERIFY PASS ... S20_state=INVALID ... D13_raws_verified=37 EMON_loops=103356 attempt_bytes=242325761`.

S-20 FULL se ejecutó y dio `INVALID` trazable: `CACHE_DENOM_ZERO,CHANNEL_VALUE_MISSING,CLOCK_MAP_UNCERTAINTY,CONTROL_PEERS_MISSING,COVERAGE_LOW,ENERGY_VALUE_INVALID,GAP_TOO_LARGE,THERMAL_READING_INVALID,WINDOW_COUNT_LOW`. S22: PASS 1488/1488; S-25 inicio/cierre: PASS 186/186, 31 por endpoint/segmento. Preflight y QA/instrumentos/expulsión: PASS. D8 final y ratio frío/caliente: INVALID por controles no adquiridos en el corte smoke.

El wrapper original alcanzó su deadline de adquisición ya después del stop EMON y canceló el postproceso. Se corrigió para respetar `capture_timing_r18.json`; prueba ejecutada con deadline expirado: `P1_POSTCAPTURE_DEADLINE_R18 PASS capture_done=True expired_deadline_ignored=true`. El replay de los crudos completos terminó y pasó el verificador.

## P2 — S25-D

Comando ejecutado: `python -B T0P\collector\verify_instrumentation_budget_r18.py --source T0P\harness\residency_r18.cpp --attempt T0P\collector\logs_r18\obj_r18\smoke_chain_n3_compact_replay_r18_20261013 --executable T0P\harness\logs_r18\obj_r18\harness_r18.exe --output ...\instrumentation_budget_r18.json`.

Salida: `S25_D_INSTRUMENTATION_R18 PASS samples=186 max_fraction=0.00629536583079 threshold=0.01`. Mediana de región 4,7826 s; p99 0,5076 %, media 0,0965 %. Hashes, JSON, prints, I/O, asignaciones y validaciones quedan fuera de `[t_pad_ini,t_pad_fin]`; evidencia vincula el binario final `00eb6b90c74cd443cc3ccb053c1aeca0c14c8aa2c6e384b18563651378a67ec2`. El reporte write-once existente se contrastó con el hash del ejecutable; la repetición de escritura se rechazó porque el artefacto ya existe.

## P3 — D10 FULL

Postproceso ejecutado: `python -B T0P\collector\telemetry_r18.py postprocess --attempt T0P\collector\logs_r18\obj_r18\smoke_chain_n3_compact_replay_r18_20261013 --output T0P\collector\logs_r18\obj_r18\smoke_chain_n3_compact_replay_r18_20261013\telemetry_status_r18.json`.

Modo `full`; grupos/fases derivados del plan y crudos ligados por SHA-256; `_phase_science` y `_phase_derivation` no declaran listas. Configuración efectiva `b1ecb45e…f2fc4fe041`; plan `b397c171…bb7524d`. S22 y S-25 pasaron; D8 y ratio quedaron INVALID por falta de capturas finales del smoke, con razón explícita. El S-20 INVALID se conservó íntegro en validación y se compactó solo al persistir; cambios de crudos se rechazan en self-test (`S20_mutations=4 D13=PASS`).

## P4 — D14

Comandos: `python -B T0P\collector\official_run_r18.py --self-test --output T0P\collector\logs_r18\obj_r18\official_run_r18_final_selftest_recheck.txt`; `python -B T0P\collector\official_run_r18.py --official`.

Salidas: `OFFICIAL_RUN_R18_SYNTHETIC_QA PASS cases=21 adapter_execution_double_only=true`; 40 mutaciones AUTH rechazadas, un reintento técnico global con razones tipadas, pausa entre réplicas no consume presupuesto, segundo HOLD terminal y FAIL científico sin reintento. Sin AUTH: `OFFICIAL_R18_REJECT AUTHORIZED_path_and_session_id_required; measurements_started=false`, exit 20.

## P5 — S25-B N=1

`official_run_r18.py --instrument-smoke` ejecutó el schedule N=1 con el binario final: seis eventos, 62 sondas, 44.263.800 B EMON, QPC 1356537726741→1369412815989 (1.287,509 s), stop=0 y EMON=0. Dictamen real `HOLD_INSTRUMENTATION_QUALIFICATION`, razón `S25B_S20_temporal_capture_not_valid`; no fue error de implementación. S22 496/496 y S-25 62/62 PASS. La prueba FULL36 (36 bloques ≥60 s y 180 s de acondicionamiento) no se ejecutó, según instrucción; estimación 1,5–2 h.

## P6 — D11

Derivación ejecutada desde los crudos P1 con `residency_derive_r18.py derive`; salida `RESIDENCY_DERIVE_R18 state=SMOKE_ONLY numeric_metrics_used=False event_union_exact=False`: 264/13.688 IDs gobernantes, 13.424 ausentes, 0 extra. Self-test ejecutado: `RESIDENCY_DERIVER_R18_QA PASS branches=13 events=13688`; fixtures verifican la ruta oficial de 31 réplicas gobernantes y rechazo de datos inválidos.

## P7 — estimaciones, specs, manifiestos y limpieza

Actualizados `SESSION_ESTIMATE_r18.md`, `HARNESS_SPEC_r18.md` y `COLLECTOR_SPEC_r18.md`. Banda Chrome cerrado: 30–33 min por cada uno de 31 segmentos gobernantes; total 33–37 h, incluidos 18.414 eventos de cualificación, sondas de inicio/cierre, acondicionamiento 180 s por segmento, controles y S-25-B. P1 midió 23,0 min/segmento de diagnóstico y acondicionamiento mínimo 181,721 s; P5 midió 21,458 min. FULL36 está incluido como banda 1,5–2 h, no como ejecución.

Los manifiestos R18 cubren todos los archivos de paquete R18 enumerados (fuente, scripts, esquemas y especificaciones) y excluyen logs, crudos, outputs y binarios; están fuera del propio hash para evitar autorreferencia. No se encontraron `__pycache__` en las raíces de harness/collector; las invocaciones Python usan `-B`.

Limpieza incompleta por bloqueo real del auto-review: rechazó `Remove-Item -LiteralPath T0P\collector\logs_r18\obj_r18\smoke_chain_n3_reprocessed_r18_20261013 -Recurse -Force`. Ese replay fallido contiene un JSON de 2.274.124.767 B y suma 2.471.918.227 B; no se intentó evadir el rechazo. C: quedó con 19.549.556.736 B libres en la lectura final.


