# Estado final de verificación R15

Actualizado: 2026-10-12. Cambios y artefactos limitados a copias R15 de `T0P/collector` y `T0P/harness`, `logs_r15/obj_r15`.

| Punto | Implementación y prueba ejecutada | Estado / pendiente |
|---|---|---|
| D1 | `harness_r15.exe --synthetic-self-test` → `PASS cases=26 phases=9`; `official_run_r15_qa.txt` prueba cada una de las nueve inyecciones, corte en fase 4, y cuatro salidas: `FALLIDA_CUALIFICACION`, `FALLO_RESIDENCIA_VALIDO`, `L2_NOT_ESTABLISHED`, `NO_ADJUDICABLE_CONTROLES`. | PASS sintético integrado; no se lanzó una sesión oficial. |
| S-23 (i–iii) | Mismo comando: R1/LP0 histórico diagnóstico con 8 workers; REF_TSC bajo, frecuencia inválida y canal ausente → HOLD; fallo científico con instrumentos válidos → `FALLIDA_CUALIFICACION`, residencia no iniciada, LP0 retenido. | PASS; no existe fallback de siete P. |
| D14 | `python -B T0P\collector\official_run_r15.py --auth-self-test` → 14/14 campos superiores y 38 campos anidados mutados rechazados; sesión/ejecutable distintos, malformed JSON, clave desconocida/ausente y AUTH ausente rechazados. Fixtures `TEST_ONLY` eliminadas. | Parser/esquema PASS. Snapshot real HOLD: no existen `torch.dll`, `torch_cpu.dll`, `c10.dll` en la ruta Torch requerida; sin esas entradas no puede validarse un AUTH real. No se creó AUTH real. |
| R | `SESSION_ESTIMATE_r15.md` usa QPC/EMON del SMOKE R15, plan corregido, 25,668 IDs de cualificación, 13,020 observaciones, 140 unidades R, 248 A/A, 26,040 márgenes y QA/D8/cierre. Escenarios 535.9/957.0/2,740.6 h; Chrome cerrado durante toda la sesión. | Estimación explícita; cualificación completa no cronometrada; S-20 de este SMOKE detectó excursión de frecuencia y el tiempo del proceso difiere del footer EMON. |
| I | `build_r15.cmd` → `HARNESS_R15_BUILD PASS`; QA → `KERNEL_QA_R15 PASS` (2,048+60, S-12); cadena de SMOKE EMON→harness/evict→D13/S-20→derivador ejecutada. `rg`: sin `return 21`, sin defaults PASS, sin flush ni escritura MSR; una llamada `validate_capture`; única mención “seven-P fallback” es la aserción negativa del test. | PASS para cadena SMOKE y rehearsal integrado. La ruta de sesión oficial no se ejecutó por instrucción; además falta el conjunto real de DLL Torch para AUTH. |
| T | `run_residency_smoke_r15.ps1 -AttemptName smoke_r15_privilege_probe_20261012` terminó: harness `SMOKE_R15_CAPTURE PASS`, R=16,384, D9 PASS, EMON footer `2401.300s real`, 144,000 grupos. Postproceso: `state=INVALID reason=FREQ_WINDOW_EXCURSION`; derivador `SMOKE_ONLY`, 18/13,688, `numeric_metrics_used=false`. | Cadena completa PASS como prueba SMOKE; controles S-20 no pasan, por lo que no es adjudicable ni aporta métricas científicas. |
| M | Specs collector/harness y estimación UTF-8 presentes; manifiestos de set exacto: collector 174/174, harness 64/64. | PASS. No quedan fixtures, `__pycache__` ni temporales R15; `logs_r12/obj_r12` no se tocó. |

## Evidencia ejecutada

- `python -B T0P\collector\residency_plan_r15.py self-test` → `420 conditions`, `140 calibration_units`, `124 AA_pairs`, `13,688 IDs`, `PASS`.
- `python -B T0P\collector\qualify_r15.py --self-test` → 8 casos, D3/D6, mutaciones S-20 y D13, `PASS`.
- `python -B T0P\collector\telemetry_r15.py self-test` → un validador único `T0P/validator/s20_validator.py`, D13 write-once, `PASS`.
- `python -B T0P\collector\residency_derive_r15.py self-test` → 9 ramas, 13,688 eventos, `PASS`.
- `harness_r15.exe --official --session-id no-auth-test-r15` → exit 20; `measurements_started=false`; `Test-Path T0P\collector\AUTHORIZED.json=False`.
- `rg -n 'validate_capture\(' T0P\collector\telemetry_r15.py` → una sola invocación (línea 113). Greps en fuentes R15 → sin retorno 21, default PASS, flush o escritura MSR.

## Hashes finales

Plan R15: `f86a5588a4fbec7c093daa295ee462c67e4c53928e080f6f66a8bd24f00bb5e4`.

Manifiestos SHA-256 de alcance exacto: `SHA256SUMS_collector_r15.txt` (174/174) y `SHA256SUMS_harness_r15.txt` (64/64); sus hashes se reportan junto con esta entrega.

No hay sesión oficial ni autorización real. Los dos límites observados son insumos Torch ausentes para calcular/validar hashes reales y HOLD S-20 `FREQ_WINDOW_EXCURSION` en la adquisición SMOKE.
