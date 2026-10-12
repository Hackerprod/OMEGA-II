# Estado de entrega R11

Última captura: `residency_smoke_20261012_014114` (etiqueta `SMOKE_ONLY`). No se ha creado `AUTHORIZED.json` real ni se ha iniciado una sesión oficial. `plan.json` y los manifiestos base permanecen intactos.

| Punto | Implementación y evidencia | Estado / bloqueo |
|---|---|---|
| 1. A/B/C y reloj | `residency_r11.cpp`: flush CLFLUSHOPT/CLFLUSH, acondicionamiento externo, direcciones/eventos por worker y aplicación, TSC/QPC, márgenes y barreras; smoke A/B/C N=3 en O=912,1024,58256, salidas bit a bit. Captura `residency_smoke_20261012_014114`. | PASS en SMOKE_ONLY. |
| 2. Plan y R | Plan r11: 420 condiciones; hash `f6a24a…25097`. Estimador genera CSV de 420 filas y R común por 64 pares W,m. | Plan/CSV validados; N=31 no ejecutado por A6 y cualificación S-20 pendientes. |
| 3. Derivador | `official_run_r11.py`: clasificación, métricas S-19 y datos sintéticos; el smoke real pasa por el derivador en modo `numeric_metrics_used=false`. | `--test-orchestrator` PASS; smoke 360/360 PASS; sin métricas científicas reales. |
| 4. A6 | Parser estricto, fixture válida solo en `test_r11`, dry-accept y nueve negativas de esquema/hash/tipo/duplicados/campos extra. | `--auth-self-test` PASS; `--dry-accept-fixture` llega a READY_TO_RUN sin medir. |
| 5. Orquestador | Nueve fases, cortes y reintento único en modo sintético; `--official` probado. | `--official` devuelve 20: `AUTHORIZED.json_missing`; lector conjunto EMON/TSC de S-20 sin certificado. Cierra antes de medir. |
| 6. Recursos | `logs_r11/SESSION_ESTIMATE_r11.md/json`: plan y cinco celdas smoke. | 116,15 h y 12,98 GB estimados para residencia+EMON; excluye preflight, cualificación previa y controles, así que el total oficial será mayor. |
| 7. Documentos/hashes | COLLECTOR_SPEC, OFFICIAL_RUN, HARNESS_SPEC, page gate y manifiestos r11 de entrega/evidencia. | PASS: harness 33 entradas, collector 12 y evidencia 132; verificados tras las pruebas. |

Pendiente global: cualificación/certificado del lector conjunto S-20 y autorización A6 real emitida por el juez. Hasta entonces `--official` debe cerrar antes de medir.
