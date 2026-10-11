# Freeze de referencia ggml CPU

## HISTORIAL ? S-12/S-13

Los bloques S-12 y S-13 describen ejecuciones previas. En particular, la conclusi?n intermedia S-13 de callback plano para todas las formas fue reemplazada por la captura S-14. Los artefactos fuente congelados de upstream y sus hashes permanecen registrados en `REF_FREEZE.json`; no se modificaron fixtures congelados.

## GOBERNANTE ? S-14

La candidata plana usa `ggml_mul_mat` con `use_ref=false`; se conservan las optimizaciones del build (AVX2, AVX-VNNI y llamafile). La plana con `use_ref=true` es solo diagn?stica. Repack conserva sus llamadas upstream y build VNNI, con 218 instrucciones `VPDPBUSD` en el objeto.

La QA completa pas? en plana y repack: 40 celdas por ruta; barrido de coordenadas 512/512 por M=1,4,8,16; 2.375.680 group dots aislados por ruta; cancelaci?n exacta y controles obligatorios. El gate detecta cobertura incompleta o cualquier fallo.

### Despacho plano observado fuera del timer

M?dulo cargado `ggml-cpu.dll`, SHA-256 `cfce3924a4de5ef82317089937edc1a4ae494515af1f1d9ee8ef05f6e921ba23`, base `0x7ffabad00000`.

- m=1: callback `ggml_vec_dot_q4_0_q8_0`, RVA `0xc8ad0`, retorno `0x7ffabad0455a`.
- m=4,8,16: breakpoint de entrada de `llamafile_sgemm`, RVA `0xc4dc0`, retorno `0x7ffabad02a92`; callback ausente.

El callsite prueba que AL=0 deriva al fallback. El retorno capturado sin callback resuelve la matriz por `llamafile_sgemm`; upstream asocia Q4_0?Q8_0 con tinyBLAS AVX2. La identificaci?n de s?mbolo COFF, m?dulo, hash, base, RVA, retorno y el desensamblado est?n en `adapter/dispatch_s14_evidence.txt` y `adapter/dispatch_s14_disassembly.txt`.

### Veredictos y memoria

| Candidata | QA_NUMERICAL | DISPATCH_QUALIFIED | PAGE_POLICY | REFERENCE_READY |
|---|---|---|---|---|
| ggml plana | PASS | PASS | PASS | PASS |
| ggml repack | PASS | PASS | PASS | PASS |

### Memoria efectiva y hashes gobernantes

El contexto ggml usa `no_alloc=true`; pesos, activaciones y el `dst` de `ggml_mul_mat` apuntan directamente a `t0common::Buffer`. El `dst` es `p.out.data()` y es la misma dirección que escribe ggml y que consulta `QueryWorkingSetEx` (sin copia posterior). Las 230 consultas informaron todas sus páginas válidas y grandes; por ejemplo, `flat_output` para m=1: `address=0x1d034400000`, `bytes=4096`, `large_pages=1/1`. El privilegio aparece `AVAILABLE` tras habilitarlo en el token del proceso. No se cambió ninguna política del sistema.

Gate ejecutado sobre `adapter/qa_output.txt`: ambas candidatas pasan QA numérica, despacho, política de páginas y `REFERENCE_READY`; pasan también S-12, exactitud, `EXACT_GROUP_DOT_ISOLATION`, barrido de coordenadas, cancelación y aislamiento de rutas. Las pruebas negativas del gate pasan.

Hashes actuales: `adapter/main.cpp` SHA-256 `79ca6c7990e87b7dbe15ce948968722fa22e7678695071a36e0a0459fe8e99d`; `adapter/adapter.exe` SHA-256 `06f001ad424e023b3ee540400dface0175032b8ee66ed4cbebcd2845292b8dae`; `adapter/qa_output.txt` SHA-256 `e05a2c000c8360eed8cad227973039f60801e1ed5fd0a88f25b99e5b83e971ee`; `adapter/qa_gate_results.txt` SHA-256 `d9c3b9f6c7fe01992a040d4812a24e6749f314004256df9a4246f6a4396216e0`.
