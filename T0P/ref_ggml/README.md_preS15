# Referencia ggml CPU

El estado gobernante S-14 y la evidencia de freeze est?n en [REF_FREEZE.md](REF_FREEZE.md) y [REF_FREEZE.json](REF_FREEZE.json). Resultados crudos: `adapter/qa_output.txt`; despacho observado fuera del timer: `adapter/dispatch_s14_evidence.txt` y `adapter/dispatch_s14_disassembly.txt`.

## Estado gobernante S-14

La candidata plana ejecuta `ggml_mul_mat` con `use_ref=false` y conserva las optimizaciones del build. `m=1` se resolvi? por `ggml_vec_dot_q4_0_q8_0` v?a callback; `m=4,8,16` por `llamafile_sgemm` AVX2/tinyBLAS Q4_0?Q8_0. Repack conserva llamadas upstream directas y objeto VNNI. Ambas candidatas pasan QA num?rica, cobertura del barrido, aislamiento de group dots y cancelaci?n exacta; el detalle por veredicto est? en el freeze.

Ambas candidatas pasan los cuatro estados: `QA_NUMERICAL=PASS`, `DISPATCH_QUALIFIED=PASS`, `PAGE_POLICY=PASS` y `REFERENCE_READY=PASS`. El `dst` efectivo de `ggml_mul_mat` apunta directamente a `p.out.data()` del asignador común; `QueryWorkingSetEx` confirmó páginas grandes para ese buffer y para el resto de las regiones consultadas. Pasan los controles S-12, exactos, aislamiento de group dots, barrido, cancelación y despacho; también pasan las pruebas negativas del gate. La ruta `use_ref=true` es solo diagnóstica y S-12/S-13 anteriores son historial.

Hashes gobernantes tras la compilación y QA: `adapter/main.cpp` `79ca6c7990e87b7dbe15ce948968722fa22e7678695071a36e0a0459fe8e99d`; `adapter/adapter.exe` `06f001ad424e023b3ee540400dface0175032b8ee66ed4cbebcd2845292b8dae`; resultados crudos `adapter/qa_output.txt` `e05a2c000c8360eed8cad227973039f60801e1ed5fd0a88f25b99e5b83e971ee`; gate `adapter/qa_gate_results.txt` `d9c3b9f6c7fe01992a040d4812a24e6749f314004256df9a4246f6a4396216e0`.
