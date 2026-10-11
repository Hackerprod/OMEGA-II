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

## ESTADO GOBERNANTE — S-13 (2026-10-11)

- Se ejecutaron 80 celdas (10 fixtures × 4 M × flat/repack): S-12, finitud y representación exacta pasaron. La variante `cancellation_exact_groups` pasó 29.696 salidas por ruta, todas exactamente ±0. El sweep de 512 coordenadas conserva 2.048 lotes por ruta, cero fallos.
- `EXACT_GROUP_DOT_ISOLATION`: ggml flat, 2.375.680 comprobaciones, 0 fallos; repack, 2.375.680, 0 fallos. Se usaron los cinco fixtures congelados, escalas unitarias y 16 grupos para cada M.
- Despacho plano: el breakpoint INT3/VEH alcanzó la dirección del trait en m=1. m=4,8,16: `reached=false`; se registraron módulo `ggml-cpu.dll`, SHA-256 `cfce3924a4de5ef82317089937edc1a4ae494515af1f1d9ee8ef05f6e921ba23`, RVA `0xc8ad0` y contexto para cada intento. Por ello el gate obligatorio final es FAIL y el adaptador sale con código 2.
- Repack enlaza directamente `ggml_gemv_q4_0_8x8_q8_0` en m=1 y `ggml_gemm_q4_0_8x8_q8_0` en m≥4. `repack_x86.cpp` upstream se compiló con `/arch:AVX2 /D__AVXVNNI__`; `dumpbin /disasm` encontró 218 `VPDPBUSD`. SHA-256 objeto actual: `f263ccfa407e36106e4f94bb2e66ae65fd5e0d7487f288a6198c7d36489eec38`. Hash histórico citado por Sol para el objeto sin VNNI: `cc3223ed3d4afff5c7f665059b30ca6e75425891254fb01d8b0a624b8f477f3b`; la recompilación histórica reproducida localmente queda conservada como `obj/repack_x86_preS13.obj`, SHA-256 `52c82e43008604654eda786c7359d697b7ba67927405d9a0086ca719afcdfba4`, 0 `VPDPBUSD` (no coincide byte a byte con el hash histórico citado).
- Frontera documentada (MD/35): estática = pesos Q4 y repack estático, contexto/grafo/plan, scratch y buffers de salida. Dinámica plana = copia Q8_0 original al buffer de activación reutilizado; dinámica repack = esa preparación más empaquetado lossless a `block_q8_0x4`. `run_once_flat/repack` no contiene callbacks, reconstrucción de pesos, comprobaciones QA ni relleno NaN. Esta carpeta solo contiene el adaptador de QA y no se ejecutaron tiempos.
- Páginas: `whoami /priv` no lista `SeLockMemoryPrivilege`; `GetTokenInformation` confirma ausencia y no se tocaron políticas. HOLD de recurso; `QueryWorkingSetEx` pasó en buffers efectivos y marcó `LargePage=false`, asignación normal.
- Gate/parser: `common/qa_gate.py ggml adapter/qa_output.txt` emite FAIL por despacho m≥4; todos los controles numéricos y de representación listados arriba pasan.

Las secciones anteriores describen el historial S-8…S-12 y no sustituyen este estado gobernante.## ESTADO GOBERNANTE ? S-13 (2026-10-11, cierre final)

- QA final: 80/80 celdas pasan S-12, finitud y representaci?n; barrido de 512 coordenadas conservado. Cancelaci?n exacta: 29.696 comprobaciones por ruta, 0 fallos; salida exactamente cero.
- `EXACT_GROUP_DOT_ISOLATION`: flat 2.375.680/0 y repack 2.375.680/0.
- Callback plano: INT3 + VEH alcanz? `ggml-cpu.dll` en m=1,4,8,16. SHA-256 DLL `cfce3924a4de5ef82317089937edc1a4ae494515af1f1d9ee8ef05f6e921ba23`; RVA `0xc8ad0`; retorno `base+0x455a`. `ggml_cpu_disasm.txt` muestra el callback desde `0xc8ad0` y `VPDPBUSD` en RVAs `0xc8baf`, `0xc8c0a`, `0xc8c5f`, `0xc8caf`, `0xc8d3a`.
- Repack upstream: 218 `VPDPBUSD`; objeto actual SHA-256 `5fcb938e96e740d31f936e7cb0b1ae500baa50bdbd3c981af96d1430b4907688`; objeto recompilado con flags anteriores conservado en `obj/repack_x86_preS13.obj` (0 VNNI; hash local `52c82e43008604654eda786c7359d697b7ba67927405d9a0086ca719afcdfba4`). Hash hist?rico citado por Sol: `cc3223ed3d4afff5c7f665059b30ca6e75425891254fb01d8b0a624b8f477f3b`.
- Fronteras MD/35 documentadas: est?tica = pesos, repack est?tico, contexto/grafo/plan, scratch y salida; din?mica flat = activaci?n Q8_0 en buffer reutilizado; din?mica repack = preparaci?n de activaci?n m?s empaquetado lossless Q8_0x4. Las comprobaciones QA quedan fuera de `run_once_*`; esta carpeta contiene el adaptador de QA y no hubo cronometraje.
- P?ginas: `SeLockMemoryPrivilege` ausente de `whoami /priv` y del token (`GetTokenInformation`); HOLD de recurso, asignaci?n normal, `QueryWorkingSetEx` sobre buffers efectivos con `LargePage=false`; no se cambi? pol?tica.
- Gate final: `qa_gate.py` emite PASS para 80/80 celdas; el ejecutable reporta `MANDATORY_GATE=PASS`.

Las secciones previas son HISTORIAL. Este bloque y `ESTADO_GOBERNANTE_S13` del JSON gobiernan.


### Cierre final de p?ginas efectivas

Tras ejecutar ambos kernels se consultaron todas las p?ginas cubiertas por cada buffer de pesos, activaciones y salida efectivo mediante `QueryWorkingSetEx`: 230 consultas de buffer, 0 fallos (cada p?gina result? v?lida). El gate final pasa 80/80 celdas y el proceso termina con c?digo 0.


### Fronteras MD/35 ? APIs finales

`prepare_static(data,m,fixture,Flat)` crea solo Q4_0 plano, contexto/grafo/plan/scratch y salida plana; `Repack` crea solo block_q4_0x8 y salida repack, sin contexto/grafo plano; `QA` combina ambos solo para controles comparativos. `STATIC_ROUTE_ISOLATION` compara sus layouts con el combinado y pasa. `prepare_dynamic(...,Flat)` copia Q8_0 al buffer reutilizado; `Repack` empaqueta directamente desde los c?digos/escalas can?nicos a block_q8_0x4 (para M=1 escribe el Q8_0 requerido por GEMV); `QA` crea ambos para reconstrucci?n. Ninguna rama din?mica de candidata crea el trabajo de la otra. `run_once_flat/repack` solo ejecuta su kernel. Se documentan entradas preparadas como frontera uno; preparaci?n din?mica de la candidata m?s kernel como frontera dos. No se tomaron tiempos.

Comprobaci?n final E3: `STATIC_ROUTE_ISOLATION` y `DYNAMIC_ROUTE_ISOLATION` pasan en la corrida gobernante; el gate exige ambos.
