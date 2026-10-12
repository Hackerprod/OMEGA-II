# Estado R10

Actualizado: 2026-10-12. Los SMOKE se usan solo para validar instrumentación y el pipeline; sus cifras no se interpretan como cualificación.

## A. Captura EMON — PASS

- N=31 SMOKE, geometría `smoke_uniform_m1`, captura `logs_r10/smoke_20261012_002106_attempt1`.
- Plan SHA-256: `39856a658f1ba7e70846870fc372c7f82042680d9d508f54805a4a433b78290c`.
- Duración estimada 1068,132 s; margen configurado 20 %; `-l=20975` (1281,759 s estimados; ≥N); espacio libre ≈14 GB y salida prevista 41 990 243 B (<100 MiB).
- Resultado: 20 975/20 975 grupos, pie temporal presente, salida EMON 42 608 870 B, estado `COMPLETE`; 992/992 filas y 4/4 resúmenes con esquema PASS.
- S-20: 124/124 bloques `VALID`, 0 `INVALID_TELEMETRY`; motivos inválidos: ninguno. Sin parada temprana ni reintento.
- Cualificador: `SMOKE_ONLY`, 992 válidas, 0 inválidas, `numeric_metrics_used=false`, residencia `NOT_STARTED`.
- Runner actualizado a margen 50 % para futuros reintentos truncados. Reejecución N=3 usó margen 50 % y verificó `harness_exit=0`.

## B. Harness y cualificador — PASS

- Copias r10 aisladas. `BW_DRAM`: 8×256 MiB, large pages comprobadas, solo lecturas AVX2 256 bit ×4. `P_instr`: VPDPBUSD, U=8, cadenas 1,2,4,8,10,12,14. Ambos proveedores fijan LP 0,2,…,14.
- Proveedores ejecutados N=3 SMOKE: `BW_DRAM` y siete cadenas `P_instr`; afinidad y páginas grandes PASS.
- Cualificador único `T0P/collector/qualify_r10.py`; referencias por mediana propia y desempate ASCII, celdas requeridas, gates congelados, S-20, cierre sin residencia y único reintento. Corregida y probada la agregación P-core (un worker) y 8P (ocho workers).
- No se modificaron umbrales ni se habilitó medición oficial.

## C. Pruebas — PASS

- Sintéticos JSON del estado: 9 casos PASS (PASS, límites G_ggml/G_Torch/H-KQ mediana/H-KQ mínimo/D′, plateau, instrumento inválido y segundo intento inválido); residencia `NOT_STARTED` en todos.
- Matriz oficial sintética completa del cálculo real: 8 casos PASS (todos los gates, cada fallo aislado y HOLD); cada rama adversa cierra antes de residencia.
- Parser cualificador: 5 negativas JSON estrictas PASS. Parser harness: 5 negativas PASS. Telemetría: 6 negativas S-20 y continuidad antes/después de `last_alive_qpc` PASS.
- SMOKE N=3 actualizado, `logs_r10/smoke_20261012_004314_attempt1`: 288/288 filas, 12/12 resúmenes, 36/36 bloques válidos; `-l=4272`, 4272 grupos, pie y `harness_exit=0` PASS. Cualificador `SMOKE_ONLY`, sin métricas numéricas.
- Revalidación con el cualificador final: N=3 288/288 y N=31 992/992 válidas; ambos `SMOKE_ONLY`.

## D. Documentación, hashes y limpieza

- `HARNESS_SPEC_r10.md` y `T0P/collector/COLLECTOR_SPEC.md` actualizados.
- `obj_r10/tmp` vacío; sin `.tmp`, `.temp` ni `__pycache__` en los árboles r10. Logs se conservan como evidencia.
- SHA256SUMS r10 generados; auditoría completa PASS: 708/708 archivos harness y 80/80 collector, hashes coincidentes y manifiesto excluido de sí mismo.

## Nota de ejecución

Un intento previo (00:07) y su único reintento fallaron antes de iniciar harness porque EMON informó contención de recursos PMU. Esa evidencia se conserva en `logs_r10/smoke_20261012_000736_attempt1/`, `smoke_20261012_000740_attempt2/` y `GLOBAL_RETRY_STATUS.txt`. Las capturas finales N=31 y N=3 iniciaron y cerraron completas sin reintento.

Restricciones respetadas: sin Git/Sol, sin `AUTHORIZED.json`, sin sesión oficial y sin regenerar manifiestos base.

Pendiente: residencia A/B/C, barrido W, derivador e integración de `AUTHORIZED.json`.
