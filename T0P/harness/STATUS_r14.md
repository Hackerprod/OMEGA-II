# STATUS_r14

Actualizado: 2026-10-12 20:05 UTC. Implementación, capturas, QA, postproceso y verificación completos. Manifiestos SHA256 R14 generados y verificados al 100%; los digests finales están en los propios manifiestos.

| Punto | Estado y evidencia ejecutada | Pendiente |
|---|---|---|
| D2 | `python -B T0P\collector\residency_plan_r14.py self-test` → `PASS geometry architecture=W288/O1024 sweep_W1024/O3640`; 420 condiciones, 140 calibraciones, 124 pares A/A, 13 688 eventos serializados; IDs únicos y unión exacta; faltantes/duplicados rechazados. Plan SHA256 `357ded52dc363313844f3b90ee94d8936a2d9cc5ea203ebe5f658532096f67b5`. | Ninguno. |
| D9 | `complete3` → R=16384 común A/B/C, W288/O1024/m1/K16; A/C misma región viva (dirección 1932984844288 y SHA256 `d1ffaa79a9b53c2cc167a1e023b604b8c595294828bf8b6acbabd97aacd1d1e3`); MAC 524288=512·1024·1; episodios/timestamps y márgenes ≥0,200 s, mismo condition; salida bitwise PASS. `verify_residency_smoke_r14.py` → PASS. Números etiquetados SMOKE, no usados. | Ninguno. |
| D8-integr | Harness enlazado solo a `T0P\evict\evict.hpp/.cpp`; grep sin rutina alternativa ni escritura MSR; tres sitios de llamada a la primitiva única para pesos/X/Y. Smoke: 22496696 llamadas pesos + 88751872 activaciones = 111248568; 346687 episodios de manifiesto JSON válido, contador y unión PASS; 106992970368 líneas expulsadas. | Ninguno. |
| D10 | Validador único `T0P\validator\s20_validator.py`, solo llamado, sin editar. `qualify_r14.py --self-test` → 8 casos; 2 mutaciones S20 rechazadas. Captura real: `HARNESS_S20_R14 state=VALID reason=none samples=120 intervals=144000 clock_rows=35018 raw_write_once=PASS`, SHA `bb08884d4b6ec7aae7ad710bb622e11cb2084a9ead5dd9f3368dcd3ad4103db6`. | Ninguno. |
| D11 | `residency_derive_r14.py self-test` → PASS 9 ramas (incluye cada HOLD y PASS). Smoke real deriva `SMOKE_ONLY numeric_metrics_used=False reason=smoke_is_not_adjudicable event_union_exact=False`, esperado para 18/13688 eventos; no adjudica métricas. | Ninguno para la entrega SMOKE. |
| D13 | Store validator write-once; intento fallido `complete2` queda intacto y marcado REJECTED por JSON D8 inválido; no se reemplazaron crudos. `complete3` → verificador confirma 17 crudos y SHA laterales; derivación escribe solo artefacto derivado. Fixture de sustitución rechazada, SHA intacto. | Ninguno. |
| T | Synthetic QA y smoke real `complete3`: 18 eventos, calibración 3, A/B/C 9, A/A 3 pares/6 observaciones, N=3; EMON continuo terminó con footer `2395.098s real`; large-page privilege llamado por el propio harness y `AVAILABLE`; seguridad/energía/PL sin cambios. `SMOKE_R14_CAPTURE PASS`, sin métricas usadas. Runner aceptó footer/stderr vacío; harness dio `ExitCode` vacío con marker PASS, así que postproceso/derive/verificador se ejecutaron manualmente y pasaron; el fallback quedó corregido y probado. | Ninguno. |
| M | Specs R14 y scripts SHA256 listos. Build → `HARNESS_R14_BUILD PASS`; `SHA256SUMS_harness_r14.txt` 310/310 y `SHA256SUMS_collector_r14.txt` 170/170 → `PASS set_exact=true`; no hay `__pycache__` ni temporales en `logs_r14\obj_r14` (tmp vacío retirado). | Ninguno. |

Pendientes funcionales únicos: orquestador D1/D14 y re-estimación de duración. Bloqueos reales del sistema: ninguno.

Incidencias que se deben informar: (1) una prueba previa de `qualify_r14.py` escribió `synthetic_qa_r12.txt` y fixtures bajo `T0P\collector\logs_r12\obj_r12`; se corrigió el destino y no se volverá a tocar esa ruta; (2) el intento `complete2` generó JSON D8 malformado, quedó rechazado con crudos inmutables; `complete3` usa serializador corregido y verificado; (3) el runner recibió `ExitCode` vacío para procesos con evidencia de cierre completa, se corrigió el fallback y se probaron ambas ramas.




