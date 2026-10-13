# Especificación ejecutable del harness R18

## Propósito y límites

El harness ejecuta la cadena R18: preflight, QA de proveedores/instrumentos/expulsión, cualificación mínima, corte automático y residencia. Los smokes llevan `numeric_metrics_used=false`; no adjudican resultados ni abren sesión oficial. Esta tarea no usa AUTHORIZED real.

## Construcción y privilegio

`build_r18.cmd` enlaza kernel, QA standalone sobre el mismo objeto, topología, EMON, expulsión, proveedores OMEGA/ggml/Torch, BW_DRAM y P_instr. Binario final: `logs_r18/obj_r18/harness_r18.exe`, SHA-256 `00eb6b90c74cd443cc3ccb053c1aeca0c14c8aa2c6e384b18563651378a67ec2`. Los procesos de smoke habilitan y registran `SeLockMemoryPrivilege`; el preflight confirma páginas grandes, mapa P-core/LP y QPC del padre. La revisión de desensamblado pasó: `ACTIVE_WAIT_DISASSEMBLY_R18 PASS gate_pause=1 worker_pause=3 worker_atomic_barriers=3 forbidden_worker_wait_calls=0`.

## Región medida y S-25-D

Ocho workers persistentes se fijan a sus LP P-core. La sincronización de trabajo usa barreras atómicas y `_mm_pause`. En `[t_pad_ini,t_pad_fin]` solo quedan el dispatch mínimo, marcas copiadas de workers y registro binario en buffers preasignados. Hashes, JSON, impresión, escrituras, asignaciones y validaciones quedan fuera; la escritura ocurre después del margen final. El registro de cada probe distingue `replica` del plan y `sample` 1–31.

Medición final con el binario final y los tres segmentos de P1: 186 sondas; región de aplicación 4,7498–4,8660 s, mediana 4,7826 s; overhead máximo 0,6295 %, p99 0,5076 %, medio 0,0965 %, límite 1 %. `verify_instrumentation_budget_r18.py` enlazó la medida a SHA-256 del binario y probó cobertura de apertura/cierre, ocho workers y las fronteras de fuente.

## Sonda S-25-A y cualificación S-25-B

Cada segmento adquiere 31 probes al inicio y 31 al cierre: 62 probes load-only, footprint 294.912 B por P, ocho P concurrentes, K=16. En P1 fueron 186/186 filas; S22 tuvo 1.488/1.488 filas core y pasó. El acondicionamiento observado mínimo fue 181,721 s por segmento. Los pesos no se refrescan durante la sonda.

`--instrument-qualification-r18` acepta schedule N=1 de humo o FULL36. El smoke N=1 ejecutó los seis eventos y alcanzó dictamen real: `HOLD_INSTRUMENTATION_QUALIFICATION`, razón `S25B_S20_temporal_capture_not_valid`; S20 temporal fue inválido por cobertura/canales/energía/térmica, S22 496/496 y probes 62/62 pasaron. Stop y EMON devolvieron 0. El FULL36 (36 bloques ≥60 s, acondicionamiento 180 s) queda reservado al juez; no se ejecutó.

## Captura y presupuesto

Antes de EMON se exige C: ≥4 GiB y cada intento se calcula bajo 1 GiB. La cadena N=3 midió 4.140,863 s entre QPC padre y padre, guardó 142.301.061 B de EMON y cerró con `emon -stop` (stop=0, EMON=0); ejecutó 264/264 eventos y terminó en `r3/m16/K16/C`. La residencia se marcó `SMOKE_ONLY`, sin HOLD de instrumentación.

`run_residency_smoke_r18.ps1` aplica el deadline a adquisición. Cuando `capture_timing_r18.json` acredita stop=0 y EMON=0, continúa supervisando espacio/tamaño durante el postproceso sin cancelar el lector. Se probó el caso con deadline expirado y captura terminada (`P1_POSTCAPTURE_DEADLINE_R18 PASS`).

## Comandos de evidencia ejecutados

```powershell
cmd /c T0P\harness\build_r18.cmd
python -B T0P\collector\verify_residency_smoke_r18.py --attempt T0P\collector\logs_r18\obj_r18\smoke_chain_n3_compact_replay_r18_20261013
python -B T0P\collector\verify_instrumentation_budget_r18.py --source T0P\harness\residency_r18.cpp --attempt T0P\collector\logs_r18\obj_r18\smoke_chain_n3_compact_replay_r18_20261013 --executable T0P\harness\logs_r18\obj_r18\harness_r18.exe --output T0P\collector\logs_r18\obj_r18\smoke_chain_n3_compact_replay_r18_20261013\instrumentation_budget_r18.json
```

Salidas clave: `HARNESS_R18_BUILD PASS`; `RESIDENCY_SMOKE_R18_VERIFY PASS schedule_events=264 ... S20_state=INVALID ...`; `S25_D_INSTRUMENTATION_R18 PASS samples=186 max_fraction=0.00629536583079 threshold=0.01`.

## Límite de interpretación

S20 FULL corrió desde crudos, configuración y hashes completos; su `INVALID` es correcto y trazable al corte SMOKE. D8 final y ratio frío/caliente quedan `INVALID` porque el corte no adquiere controles finales. No se relajan los umbrales ni se presenta un smoke como PASS científico.

