# Resultados QA ggml con S-12 (sin cronometraje)

Cada fila es una celda ruta-fixture-M. `error/B máximo` es el máximo por celda, no se combinan salidas de celdas distintas. La tolerancia antigua se conserva como diagnóstico.

| Ruta | Fixture | M | S-12 | Fallos | max error/B | Tolerancia antigua | Representación exacta |
|---|---|---:|:---:|---:|---:|---|:---:|
| flat | uniform | 1 | sí | 0 | 0.000856075656448 | falla (5) | PASS |
| repack | uniform | 1 | sí | 0 | 0.000923773041122 | falla (2) | PASS |
| flat | uniform | 4 | sí | 0 | 0.00095984456113 | falla (26) | PASS |
| repack | uniform | 4 | sí | 0 | 0.00125952771448 | falla (14) | PASS |
| flat | uniform | 8 | sí | 0 | 0.000921100421967 | falla (42) | PASS |
| repack | uniform | 8 | sí | 0 | 0.00117461537558 | falla (13) | PASS |
| flat | uniform | 16 | sí | 0 | 0.00102745439004 | falla (86) | PASS |
| repack | uniform | 16 | sí | 0 | 0.00135805361875 | falla (41) | PASS |
| flat | mixed_cancellation | 1 | sí | 0 | 0 | pasa (0) | PASS |
| repack | mixed_cancellation | 1 | sí | 0 | 0 | pasa (0) | PASS |
| flat | mixed_cancellation | 4 | sí | 0 | 0 | pasa (0) | PASS |
| repack | mixed_cancellation | 4 | sí | 0 | 0 | pasa (0) | PASS |
| flat | mixed_cancellation | 8 | sí | 0 | 0 | pasa (0) | PASS |
| repack | mixed_cancellation | 8 | sí | 0 | 0 | pasa (0) | PASS |
| flat | mixed_cancellation | 16 | sí | 0 | 0 | pasa (0) | PASS |
| repack | mixed_cancellation | 16 | sí | 0 | 0 | pasa (0) | PASS |
| flat | varied_scales | 1 | sí | 0 | 0.000894693550589 | falla (3) | PASS |
| repack | varied_scales | 1 | sí | 0 | 0.000703762497975 | falla (1) | PASS |
| flat | varied_scales | 4 | sí | 0 | 0.00080404402344 | falla (21) | PASS |
| repack | varied_scales | 4 | sí | 0 | 0.00108763995235 | falla (9) | PASS |
| flat | varied_scales | 8 | sí | 0 | 0.00106418842065 | falla (27) | PASS |
| repack | varied_scales | 8 | sí | 0 | 0.0015246932862 | falla (8) | PASS |
| flat | varied_scales | 16 | sí | 0 | 0.00106679262386 | falla (74) | PASS |
| repack | varied_scales | 16 | sí | 0 | 0.00153211577474 | falla (27) | PASS |
| flat | sub127 | 1 | sí | 0 | 0.000737778922354 | falla (3) | PASS |
| repack | sub127 | 1 | sí | 0 | 0.000759645714638 | falla (2) | PASS |
| flat | sub127 | 4 | sí | 0 | 0.000997883759595 | falla (15) | PASS |
| repack | sub127 | 4 | sí | 0 | 0.00132674279203 | falla (6) | PASS |
| flat | sub127 | 8 | sí | 0 | 0.000934470123709 | falla (36) | PASS |
| repack | sub127 | 8 | sí | 0 | 0.00100341337401 | falla (16) | PASS |
| flat | sub127 | 16 | sí | 0 | 0.00106036203027 | falla (65) | PASS |
| repack | sub127 | 16 | sí | 0 | 0.00146862623122 | falla (22) | PASS |
| flat | extremes | 1 | sí | 0 | 0.00732122160105 | pasa (0) | PASS |
| repack | extremes | 1 | sí | 0 | 0.00732122160105 | pasa (0) | PASS |
| flat | extremes | 4 | sí | 0 | 0.00739685486919 | pasa (0) | PASS |
| repack | extremes | 4 | sí | 0 | 0.00739685486919 | pasa (0) | PASS |
| flat | extremes | 8 | sí | 0 | 0.00726399354342 | pasa (0) | PASS |
| repack | extremes | 8 | sí | 0 | 0.00726399354342 | pasa (0) | PASS |
| flat | extremes | 16 | sí | 0 | 0.00776821610203 | pasa (0) | PASS |
| repack | extremes | 16 | sí | 0 | 0.00776821610203 | pasa (0) | PASS |
| flat | zero_input | 1 | sí | 0 | 0 | pasa (0) | PASS |
| repack | zero_input | 1 | sí | 0 | 0 | pasa (0) | PASS |
| flat | zero_input | 4 | sí | 0 | 0 | pasa (0) | PASS |
| repack | zero_input | 4 | sí | 0 | 0 | pasa (0) | PASS |
| flat | zero_input | 8 | sí | 0 | 0 | pasa (0) | PASS |
| repack | zero_input | 8 | sí | 0 | 0 | pasa (0) | PASS |
| flat | zero_input | 16 | sí | 0 | 0 | pasa (0) | PASS |
| repack | zero_input | 16 | sí | 0 | 0 | pasa (0) | PASS |
| flat | single_coordinate | 1 | sí | 0 | 0 | pasa (0) | PASS |
| repack | single_coordinate | 1 | sí | 0 | 0 | pasa (0) | PASS |
| flat | single_coordinate | 4 | sí | 0 | 0 | pasa (0) | PASS |
| repack | single_coordinate | 4 | sí | 0 | 0 | pasa (0) | PASS |
| flat | single_coordinate | 8 | sí | 0 | 0 | pasa (0) | PASS |
| repack | single_coordinate | 8 | sí | 0 | 0 | pasa (0) | PASS |
| flat | single_coordinate | 16 | sí | 0 | 0 | pasa (0) | PASS |
| repack | single_coordinate | 16 | sí | 0 | 0 | pasa (0) | PASS |
| flat | cancellation_high | 1 | sí | 0 | 0 | pasa (0) | PASS |
| repack | cancellation_high | 1 | sí | 0 | 0 | pasa (0) | PASS |
| flat | cancellation_high | 4 | sí | 0 | 0 | pasa (0) | PASS |
| repack | cancellation_high | 4 | sí | 0 | 0 | pasa (0) | PASS |
| flat | cancellation_high | 8 | sí | 0 | 0 | pasa (0) | PASS |
| repack | cancellation_high | 8 | sí | 0 | 0 | pasa (0) | PASS |
| flat | cancellation_high | 16 | sí | 0 | 0 | pasa (0) | PASS |
| repack | cancellation_high | 16 | sí | 0 | 0 | pasa (0) | PASS |
| flat | cancellation_extreme | 1 | sí | 0 | 0 | pasa (0) | PASS |
| repack | cancellation_extreme | 1 | sí | 0 | 0 | pasa (0) | PASS |
| flat | cancellation_extreme | 4 | sí | 0 | 0 | pasa (0) | PASS |
| repack | cancellation_extreme | 4 | sí | 0 | 0 | pasa (0) | PASS |
| flat | cancellation_extreme | 8 | sí | 0 | 0 | pasa (0) | PASS |
| repack | cancellation_extreme | 8 | sí | 0 | 0 | pasa (0) | PASS |
| flat | cancellation_extreme | 16 | sí | 0 | 0 | pasa (0) | PASS |
| repack | cancellation_extreme | 16 | sí | 0 | 0 | pasa (0) | PASS |

## Controles y límites

Todos los 36 controles de reconstrucción plana y post-repack pasaron. La referencia y los límites fueron finitos; las 72 filas pasaron S-12. El kernel no expone dots INT32 por grupo: no se obtuvo captura directa pre-escalado y la igualdad interna sigue sin certificar.

Las pruebas comunes de frontera cubren error en el límite y un ULP fuera a ambos lados, NaN, Inf, S=0 y tamaño inconsistente. Pruebas negativas de representación: una mutación Q4 y un bit de escala FP16 con activaciones nulas fueron rechazados.

En `cancellation_high`, S/|r|≈2049; en `cancellation_extreme`, r=2^-24 y S/|r|≈9.55e11. El barrido de 512 posiciones no está implementado; `single_coordinate` solo prueba la posición 0.

## Barrido de una coordenada

Cada combinación (M, coordenada) es un lote canónico independiente: hay 512 posiciones para cada M y los M renglones activan la misma coordenada con escala 1. Los hashes SHA-256 de los 2.048 lotes están en `T0P/common/NEW_FIXTURES_FROZEN.json`, congelados antes de QA. Torch (mm, matmul, linear, mv para M=1 y mkldnn_linear) registró cero fallos S-12 y cero diferencias exactas. ggml (flat y repack) registró cero fallos y salida entera exacta para todos los lotes.

| M | Ruta | Lotes | Fallos exactos/S-12 | max error/B |
|---:|---|---:|---:|---:|
| 1 | flat | 512 | 0 | 0 |
| 1 | repack | 512 | 0 | 0 |
| 4 | flat | 512 | 0 | 0 |
| 4 | repack | 512 | 0 | 0 |
| 8 | flat | 512 | 0 | 0 |
| 8 | repack | 512 | 0 | 0 |
| 16 | flat | 512 | 0 | 0 |
| 16 | repack | 512 | 0 | 0 |

Precisión de la limitación de productos de grupo: `ggml_mul_mat` expone solo el tensor final ya escalado desde el callback Q4_0 del trait; el símbolo runtime no está exportado ni hay PDB. Las llamadas directas `ggml_gemv_q4_0_8x8_q8_0` y `ggml_gemm_q4_0_8x8_q8_0` devuelven solo salidas agregadas y no tienen hook/ABI para leer los 16 acumuladores INT32. No se modificó el upstream ni se sustituyó la primitiva por un cálculo escalar; por ello los dots por grupo de la primitiva no quedan certificados, aunque `exact_group_dots()` y su oráculo común sí pasan sus pruebas de API.
