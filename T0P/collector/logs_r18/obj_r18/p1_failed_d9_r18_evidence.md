# P1 fallo t?cnico r18 ? evidencia preservada

- Intento: `T0P\collector\logs_r18\obj_r18\smoke_chain_n3_r18_c42_retest_20261013` (propio; fallido, pendiente de limpieza).
- Comando: `T0P\harness\run_residency_smoke_r18.ps1 -AttemptName smoke_chain_n3_r18_c42_retest_20261013`.
- Preflight, QA/instrumentos/expulsi?n y cualificaci?n m?nima: PASS; cualificaci?n `SMOKE_ONLY`, N=3, 240 filas auxiliares, m?tricas cient?ficas desactivadas.
- Causa: `acquisition_adapter_error:GateError:acquisition_process_failed:residency:exit=1:HARNESS_R18_ERROR D9_INSTRUMENTATION_OVERHEAD_GT_1_PERCENT:event=residency|architecture|B|m8|K1|W288|O1024|r1:fraction=0.010413 measurements_label=SMOKE numeric_metrics_used=false
`; primer evento de residencia, D9, fracci?n 0.010413 (1.0413 %); no es HOLD S-20.
- EMON: cierre controlado=True, stop_exit=0, emon_exit=0, captura=53140361 bytes.
- QPC padre: `parent_QueryPerformanceCounter`, QPC 1234875849083?1250565171910, QPF=10000000, duraci?n=1568.932 s.
- Intento total=98,455,447 bytes; C: libre tras cierre=25,355,067,392 bytes.
- SHA-256 binario fallido: `86f240b3566309c2f30c6f592350690193d2360eb26429b25f7aa83c1f09b134`.
- Verificador P1: no alcanzado porque falt? `preflight_r18.json` despu?s del HOLD D9.
- Registrado UTC: 2026-10-13T07:45:00.471247+00:00.
