# Estado R9 — checklist final

Ámbito: solo T0P\harness\. Objetos en obj_r9\; ejecuciones en logs_r9\. No se creó AUTHORIZED.json, no se inició sesión oficial, no cambiaron umbrales y no se usaron Git ni Sol.

## Punto 1 — derivación de telemetría
- Estado: IMPLEMENTADO + PRUEBA N=3 COMPLETA.
- Comando: powershell -NoProfile -ExecutionPolicy Bypass -File T0P\harness\run_smoke_r9.ps1 -SampleCount 3; exit 0. Captura: logs_r9\smoke_20261011_232935.
- Salida: raw 96 filas, 12/12 bloques VALID, cuatro proveedores; f_activo por P-core, a_i, f_pared y P_pkg impresos. QPC por muestra: 2,507526–11,622958 s.
- Negativos deterministas: python T0P\harness\telemetry_r9.py --self-test; TELEMETRY_QA result=PASS; seis casos rechazan flag térmico, desvío f_TSC >3 %, TCC<5 °C, denominadores L2/LLC cero y potencia no estimable. Salida: logs_r9\qa_point6_r9\telemetry_selftest.txt.
- N=31 se informa en punto 5: el modo preserva INVALID_TELEMETRY por muestra; no sustituye la prueba N=3 válida.

## Punto 2 — R común calibrado
- Estado: IMPLEMENTADO + PRUEBA EJECUTADA.
- R se calibra con piloto de 200 ms por proveedor/worker; el R mínimo se fija para que la ruta más rápida llegue al objetivo 2,5 s y cada muestra supera 2,0 s. Un R por unidad comparativa se registra en calibration.json.
- N=3: R=717926 en las 96 filas; QPC mínimo 2,507526 s. N=31 final: R=717041 en las 992 filas; QPC mínimo 2,500483 s. Calibraciones en las capturas de los puntos 1 y 5.
- Geometría: seis celdas tienen exactamente un R_common cada una; 168 filas por celda. O912/m1=796998, O912/m16=69811, O1024/m1=717172, O1024/m16=62209, O58256/m1=2268, O58256/m16=464. Comprobación PASS en logs_r9\geometry_20261011_222852.

## Punto 3 — despacho del plan
- Estado: IMPLEMENTADO + PRUEBA EJECUTADA.
- El runner ejecuta collector.exe --plan-check antes de calibrar; C++ rehash del plan canónico y rechaza discrepancias antes de crear calibración. Plan SHA256: 39856a658f1ba7e70846870fc372c7f82042680d9d508f54805a4a433b78290c.
- Despacho observado en smoke: ggml_repack@2, torch_mm@10, ggml_flat@26, omega@186; verified_before_calibration=true.
- Prueba de alteración: harness_r9.exe --smoke ... plan_mutated_hash_r9.json <sha válido>; HARNESS_ERROR PLAN_HASH_MISMATCH_BEFORE_CALIBRATION, exit 1, sin calibration.json. Evidencia: logs_r9\plan_hash_probe_r9\. El control collector rechazó además JSON alterado antes de EMON.

## Punto 4 — geometría
- Estado: IMPLEMENTADO + SMOKE SOLICITADO COMPLETO.
- Matriz implementada O={912,1024,1824,2728,3640,4552,5464,6368,7280,9104,10920,14560,21848,29128,43688,58256}, m={1,4,8,16}. Smoke N=3 de O={912,1024,58256}, m={1,16}, ocho workers y siete rutas: OMEGA, ggml flat/repack y Torch mm/matmul/linear/mv_out.
- Comando: powershell -NoProfile -ExecutionPolicy Bypass -File T0P\harness\run_geometry_smoke_r9.ps1. Captura: logs_r9\geometry_20261011_222852. Harness: 1008 filas, seis unidades, 42 resúmenes; numerical/page/status PASS en todas las filas.
- Primer cierre del runner exit 1 por validador que rechazaba plan_index=0. Corregido el validador y reejecutado: raw 1008 y summary 42, ambos schema PASS; registro logs_r9\geometry_20261011_222852\validation_rerun.txt.
- La cuadrícula geométrica autorizada completa no se midió porque requiere AUTHORIZED.json y sesión oficial, ambos prohibidos en este encargo.

## Punto 5 — N=31, estadísticos y --official
- Estado: N=31 SMOKE Y PUERTA OFICIAL PROBADAS.
- Comando final: powershell -NoProfile -ExecutionPolicy Bypass -File T0P\harness\run_smoke_r9.ps1 -SampleCount 31; exit 0. Captura logs_r9\smoke_20261011_233515; harness.exit=0, raw 992, cuatro resúmenes N=31, warmup=5. R=717041 común; muestra mínima 2,500483 s.
- Resúmenes mediana/IQR/MAD/mín/máx (µs): ggml_repack 8360040/79921,4/50103,4/8285820/8542320; torch_mm 11292600/233556/127677/11106600/11495700; ggml_flat 11778800/251460/111933/11652600/12506500; omega 2559000/33530,1/8984,72/2549080/2635170. Esquemas pre/post PASS.
- Límite telemétrico de este N=31: derivación registra 25/124 VALID y 99 INVALID por “no hay grupos EMON completos dentro del bloque”; exit interno 4 es aceptado y los motivos quedan en JSONL. Los cuatro resúmenes agregados quedan INVALID_TELEMETRY. El smoke N=3 del punto 1 sí dio 12/12 VALID.
- Puerta ausente: harness_r9.exe --official; mensaje AUTHORIZED.json_missing, exit 20, measurements_started=false. Runner run_official_r9.ps1 rechazó exit 20 antes de EMON; captura logs_r9\official_20261011_232549 contiene solo plan check y puerta, sin artefactos EMON.
- Fixtures sin crear AUTHORIZED.json: clave duplicada, esquema incorrecto y hash de plan incorrecto; cada --auth-fixture-check devuelve exit 20 y measurements_started=false. Capturas auth_fixture_*_r9. AUTHORIZED.json sigue ausente.
- La adquisición oficial completa no se ejecutó por la regla de no AUTHORIZED ni sesión oficial.

## Punto 6 — JSONL estricto e integración
- Estado: IMPLEMENTADO + PRUEBAS EJECUTADAS.
- validate_r9.py exige campos y tipos exactos; rechaza claves duplicadas, constantes no finitas, overflow, trailing data, faltantes y extras. HARNESS_SPEC_r9.md documenta el esquema.
- Pruebas: python T0P\harness\validate_r9.py --self-test → HARNESS_SCHEMA_SELF_TEST PASS; python T0P\harness\telemetry_r9.py --self-test → TELEMETRY_QA result=PASS. Evidencia: logs_r9\qa_point6_r9\.
- postprocess_r9.ps1 conecta prevalidación, derivación y validación posterior. Ejecutado sobre N=3: 96 raw/4 summaries PASS y 12/12 telemetrías VALID. Ejecutado sobre N=31: 992 raw/4 summaries PASS y 124 estados conservados, incluidos INVALID_TELEMETRY; exit 0 del posprocesador.

## Punto 7 — documentación, SHA256 y limpieza
- Estado: DOCUMENTACIÓN Y AUDITORÍA SHA COMPLETAS.
- Documentos: HARNESS_SPEC_r9.md y README_r9.md.
- SHA256SUMS_harness_r9.txt contiene 541 entradas para los 541 archivos bajo T0P\harness salvo el propio manifiesto; auditoría 541/541 exacta. No se regeneraron manifiestos base.
- obj_r9\tmp está vacío; .lib/.exp R9 en obj_r9; logs de prueba permanecen en logs_r9 como evidencia. La salida legacy que apuntaba fuera de harness se retiró; T0P\evict\padded_block_analysis.csv no existe.
- Limpieza y alcance: obj_r9\tmp vacío; .lib/.exp R9 en obj_r9; logs conservados en logs_r9; sin archivos de salida fuera de harness. Sin bloqueos de compilación o ejecución.