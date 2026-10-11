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
| ggml plana | PASS | PASS | HOLD | HOLD |
| ggml repack | PASS | PASS | HOLD | HOLD |

El token de esta sesi?n no tiene `SeLockMemoryPrivilege`. `QueryWorkingSetEx` confirm? p?ginas v?lidas en las regiones efectivas y `LargePage=0`; por eso PAGE_POLICY queda HOLD de recurso. No se cambi? ninguna pol?tica del sistema. Al concederse el privilegio al usuario y abrir una sesi?n nueva, el mismo ejecutable intenta las p?ginas grandes.
