# Referencia ggml CPU

El estado gobernante S-14 y la evidencia de freeze est?n en [REF_FREEZE.md](REF_FREEZE.md) y [REF_FREEZE.json](REF_FREEZE.json). Resultados crudos: `adapter/qa_output.txt`; despacho observado fuera del timer: `adapter/dispatch_s14_evidence.txt` y `adapter/dispatch_s14_disassembly.txt`.

## Estado gobernante S-14

La candidata plana ejecuta `ggml_mul_mat` con `use_ref=false` y conserva las optimizaciones del build. `m=1` se resolvi? por `ggml_vec_dot_q4_0_q8_0` v?a callback; `m=4,8,16` por `llamafile_sgemm` AVX2/tinyBLAS Q4_0?Q8_0. Repack conserva llamadas upstream directas y objeto VNNI. Ambas candidatas pasan QA num?rica, cobertura del barrido, aislamiento de group dots y cancelaci?n exacta; el detalle por veredicto est? en el freeze.

Las asignaciones efectivas fueron v?lidas, pero `LargePage=0` porque el token de esta sesi?n no tiene `SeLockMemoryPrivilege`: `PAGE_POLICY=HOLD` y `REFERENCE_READY=HOLD`. La plana `use_ref=true` es ?nicamente diagn?stica. Los resultados anteriores S-12/S-13 son historial.
