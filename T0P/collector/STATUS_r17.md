# STATUS R17 — 2026-10-12

Alcance: cambios solo en `T0P\harness\` y `T0P\collector\`; resultados bajo `logs_r17\obj_r17`. No se leyó AUTHORIZED real, no se abrió sesión oficial y no se modificaron validator, evict ni kernel. Las pruebas físicas disponibles terminaron en HOLD; no se les atribuye PASS científico.

## Resultado de ejecución y bloqueo

El smoke de cadena N=3 llamó preflight → QA/instrumentación/expulsión → cualificación → corte diagnóstico → residencia. Preflight y QA pasaron; cualificación acabó `SMOKE_ONLY` y adquirió BW_DRAM/P_instr. En R01/BEGIN de la sonda S25-A se detuvo con región 4,780637 s y overhead calculado 7,014149. EMON se cerró con `emon -stop`: QPC del padre, 1.307,234 s y 44.518.297 B. No se midió ninguna fila de residencia.

La causa fue un defecto de asociación temporal: la lectura de marcas de worker ocurría tras el margen final y observaba las marcas de R=32.768 en lugar de las de la región R=262.144. Se corrigió el orden; pasaron la prueba sintética de orden, build, QA del binario enlazado, cancelación y desensamblado. Por la regla de no repetir la prueba S25-B hasta PASS, no se repitió EMON. El HOLD físico previo sigue vigente y la corrección aún no tiene PASS físico.

El smoke S25-B N=1 también terminó en HOLD instrumental antes del arreglo; EMON cerró controladamente (23.645.815 B, intento 54.016.213 B). El wrapper eliminó ese intento y guardó el JSON de bloqueo. El FULL de 36 bloques quedó sin ejecutar. El intento N=3 fallido, de 89.813.282 B, se eliminó después de guardar el resumen en `logs_r17\obj_r17\smoke_chain_n3_evidence_r17.md`.

## Checklist con evidencia ejecutada

| Punto | Prueba ejecutada y salida clave | Estado / pendiente |
|---|---|---|
| OP1 | `python -B T0P\collector\official_run_r17.py --self-test --output ...\official_run_r17_qa_final5.txt` → `PASS cases=18`; incluye expulsión válida LLC-miss 0,996 PASS y la antigua inversión 0,001 HOLD. | **PASS sintético**: D8 usa `cold_M3/OFFCORE ≥0,90`. Sin captura científica gobernante. |
| OP2 | `python -B T0P\collector\verify_residency_smoke_r17.py --self-test` → `PASS cases=4 R(W,m,K)_distinct=2 R_common_A_B_C=true`; plan → 140 unidades R, 420 condiciones y R fijo N=31. | **PASS sintético/plan**; aún sin calibración física de residencia (el HOLD precedió a la primera fila). |
| OP3 | Runner integrado, cadena N=3 real: `acquisition_calls=[preflight,qa_instruments_expulsion,qualification,residency]`; R17 `main` admite `--acquire-r17` por fase; chain self-test → `PASS cases=18 adapter_execution_double_only=true`. | **PARCIAL**: adaptador/exe integrados y decisiones desde evidencia; no completó residencia, controles finales ni derivación. S-20 FULL con captura completa pendiente. |
| OP4 | `python -B T0P\collector\residency_plan_r17.py self-test --output ...\plan_self_test_r17_final4.txt` → `BW_DRAM+H-KQ+Dprime planned in one Q segment`; N=3 → BW_DRAM 24 filas, P_instr 9 variantes. | **PASS en plan y cualificación N=3**; no valida sesión oficial. Specs r17 reescritas con límites reales. |
| S25-A | `python -B T0P\harness\verify_hot_probe_stamp_r17.py --output ...\hot_probe_stamp_order_r17.txt` → `PASS measured_dispatch<binary_stamp_copy<post_margin<pad_end<validation`; test del binario → `forbidden_worker_wait_calls=0`; cancelación → `PASS bounded_wait_ms=1000`. | **HOLD físico** antes de corregir marcas; corrección inspeccionada y compilada, pero requiere la siguiente adquisición autorizada para PASS. Factores aᵢ,k y límites H2/H3/D2 no pasaron una ventana S25 válida en esa captura. |
| S25-B | `python -B T0P\collector\instrument_qualification_r17.py self-test --output ...\instrument_qualification_self_test_r17_final4.txt` → `PASS synthetic_cases=5 full_blocks=36 smoke_N1_blocks=1`; smoke N=1 físico: HOLD, EMON 23.645.815 B y parada controlada. | **HOLD**: FULL 36 bloques no ejecutado. No se repite hasta autorización y PASS. |
| S25-C | `python -B T0P\collector\residency_plan_r17.py self-test ...` → `31_replica_orders hash=PASS 16_unique_W_per_segment=PASS pause_only_between_replica_segments=PASS`; hash agregado `7fabbe0f85660d2e7215701693cebf9f1452d5fd41c47dbffb431cc449181330`. | **PASS de plan y mutaciones**. |
| S25-D | Prueba de orden S25-A anterior; inspección y QA del binario. El código escribe marcas binarias preasignadas dentro del pad y hace validación/hash/JSON fuera; esquema usa `frequency_of_condition`. | **PARCIAL**: integración fuente/binary comprobada; presupuesto ≤1% no se midió en ventana válida post-arreglo. |
| D10 | `python -B T0P\collector\qualify_r17.py --self-test ...` → `PASS cases=8 S20_mutations=4 D13=PASS`; chain self-test → `D10_postprocess=PASS missing_governing_preflight_fails_closed`. | **PARCIAL**: FULL se deriva del plan/IDs completos y configuración ligada; cuatro mutaciones sintéticas pasan. Sin crudos FULL ni constantes oficiales verificadas en sesión. |
| D11 | `python -B T0P\collector\residency_derive_r17.py self-test --output ...\derive_self_test_r17_final4.txt` → `PASS branches=13 events=13688`. | **PASS de fixtures/ramas**: sin 31 réplicas gobernantes reales que atribuir. |
| D12 | `harness_r17.exe --qa-r17` en la compilación final → `KERNEL_QA_R17 PASS ... same_object_as_harness=true exact_O1024_and_512_coordinate_sweeps=true`; O=1024 checks=40 failures=0. | **PASS sobre objeto enlazado final**. |
| D14 | `python -B T0P\collector\official_run_r17.py --self-test ...` → `PASS cases=18`, mutaciones de autorización rechazadas; `--official --plan ...` → `OFFICIAL_R17_REJECT AUTHORIZED_path_and_session_id_required; measurements_started=false`, exit 20. | **PARCIAL**: schema liga exe/Torch/plan/segmentación/órdenes/validador y cierra sin autorización. Sin AUTHORIZED real por instrucción; el runner todavía no implementa el reintento global en runtime. |
| Q | Smoke N=3: `capture_timing_r17.json` → `qpc_source=parent_QueryPerformanceCounter`, `stop_command=emon -stop`, `controlled_stop=true`; duración 1.307,234 s y 44.518.297 B; `-l=68904` dimensionado al schedule. | **PARCIAL**: cierre controlado y sin espera `-l` probados; abortó antes del último bloque/margen porque la sonda disparó HOLD. Footer queda diagnóstico. |
| E | `T0P\collector\SESSION_ESTIMATE_r17.md` creado con tasas observadas, cuentas por fase, márgenes y supuestos. | **PASS como estimación**: ≈24,4 min por segmento R; 31–35 h total; requiere recalcular con R medido y S-20 FULL. |
| T/M | Plan: 31 órdenes y mutaciones; instrumentación 5 casos; cualificador 8; derivador 13; chain/D8 18; S25-R 4; telemetry budgets; smoke N=3 y S25-B N=1 registrados. | **PARCIAL**: sintéticas ejecutadas; adquisición de cadena terminó en HOLD y no hay resultado científico N=31. Temporales propios retirados. |

## Hashes y estado de artefactos

- SHA256 del plan lógico: `f384596a19e15f834bd42e1e487d98699cb9bbc88abb54d006dab50f49a1afe9`.
- SHA256 del orden agregado: `7fabbe0f85660d2e7215701693cebf9f1452d5fd41c47dbffb431cc449181330`.
- SHA256 de las 31 órdenes S25-C serializadas: `2c2118cf2d3832f80f229ffc4af4e2887584f2a973f5ce1139da5021c3fe7b04`.
- Hash de archivo del plan congelado: `ac5ba512930f2583d59b0785954497acd16193918bb49f970238d781ef28dc9d`.
- Hash del ejecutable enlazado final: `72e904ed1cba9c455ceb743d0479795a2bd25745e77deac600f4ffadb7b7ebf6`.
- Evidencia de smokes: `logs_r17\obj_r17\smoke_chain_n3_evidence_r17.md`, `instrument_smoke_blocker_1791869497444163900.json`.
- Manifiestos: `SHA256SUMS_harness_r17.txt` y `SHA256SUMS_collector_r17.txt`; alcance = archivos R17 de fuente, docs, schema y scripts en la raíz de cada paquete, sin logs/obj/binarios generados. Se validan al 100% tras escribir este STATUS.
- Sin `__pycache__`; carpetas de intento N=3, fixtures `synthetic_d1_r17`, planes temporales de self-test y `harness\logs_r17\obj_r17\tmp` retirados. C: seguía con más de 25 GB libres al cierre de las pruebas físicas.
- Incidencia de inspección: al inicio se ejecutó una búsqueda `rg` recursiva sobre `T0P\harness` que alcanzó crudos EMON; la salida se recortó (el lector indicó 2,7 GB omitidos). No hubo escrituras; las búsquedas siguientes se limitaron a archivos fuente concretos y no se volvió a recorrer logs ni crudos.

## Bloqueos y siguientes pasos permitidos

1. S25-A/S25-B: solo una nueva adquisición autorizada tras el arreglo podrá cerrar el overhead. La captura anterior queda HOLD; FULL 36 no se ejecutó.
2. S-20 FULL, N=31, controles D8 finales y derivación desde crudos completos están pendientes de la sesión gobernada.
3. D14 requiere completar o probar el presupuesto global de reintento runtime antes de habilitar una sesión oficial.
