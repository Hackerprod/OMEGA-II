# Freeze de referencia Torch CPU

## HISTORIAL

S-12, S-13 y S-14 quedan como historial en `REF_FREEZE.json`. El HOLD por privilegio ausente describe corridas anteriores y está sustituido por S-15. Se conservan estas lagunas: `qa_gate.py_preS14` no es byte a byte idéntico al gate anterior; los `.lib/.exp` históricos no se recuperaron. No se afirman reproducciones byte a byte. Los fixtures congelados no se modificaron.

## GOBERNANTE — S-15

La QA gobernante terminó con código 0. `mm`, `matmul`, `linear` y `mv` (solo m=1) pasan los cuatro estados. `Y` se preasigna en common Buffer con forma y strides finales y se entrega a `at::mm_out`, `at::matmul_out`, `at::linear_out` o `at::mv_out`. En cada llamada se comprueban `data_ptr`, storage pointer, forma, strides y bytes; no hay copia posterior. Las consultas incluyen `Y` y sus páginas reportan `Valid=1` y `LargePage=1`.

S-12, representaciones exactas, barridos de 512 posiciones, cancelación exacta, aislamiento `EXACT_GROUP_DOT_ISOLATION` y despacho de las variantes `_out` pasan. Hay 390/390 registros de memoria esperados. `mkldnn_linear` sigue diagnóstico/no comparable. Las pruebas unitarias del verificador S-12 también pasan (`adapter/verifier_unit_tests_s15.txt`).

| Candidata | QA_NUMERICAL | DISPATCH_QUALIFIED | PAGE_POLICY | REFERENCE_READY |
|---|---|---|---|---|
| mm | PASS | PASS | PASS | PASS |
| matmul | PASS | PASS | PASS | PASS |
| linear | PASS | PASS | PASS | PASS |
| mv (m=1) | PASS | PASS | PASS | PASS |

Logs gobernantes: [`adapter/qa_s15_governing.txt`](adapter/qa_s15_governing.txt), SHA-256 `b03332562ae5d65f1b3928680beb54c30550fdcd18ddd3627c353ba99d08c2de`; gate [`adapter/qa_gate_s15_governing.txt`](adapter/qa_gate_s15_governing.txt), SHA-256 `4bd5036988a36bf813f63607e4f20d91c0e0c0fa95d373a4a4b7e0f26e4acb5b`. Hashes de `ref_torch.cpp`: `213b8b99199e50ecd6c3523d4b0a638659eb7d167513ddbe7a3060eb0e04c9de`; `qa_gate.py`: `be7d608d3d03bcfaaeb9cca175129d8ba296e98c96bdda982b0c46b221b2dabe`; `PAGE_EXPECTED_S15.json`: `aa8e25c5f1b0e93b0741ca32169757a9da1f980211151a91ffc383276c52b027`. Detalles e identidades adicionales están en `REF_FREEZE.json`.


## GOVERNING_S16

Reevaluaci?n con el parser S-16 de `adapter/qa_s15_governing.txt` intacto: `adapter/qa_gate_s16_governing.txt`. `torch_mm`, `torch_matmul`, `torch_linear` y `torch_mv`: QA_NUMERICAL, DISPATCH_QUALIFIED, PAGE_POLICY y REFERENCE_READY = PASS. Crudo SHA-256 b03332562ae5d65f1b3928680beb54c30550fdcd18ddd3627c353ba99d08c2de; adaptador sin cambios: `ref_torch.cpp` 213b8b99199e50ecd6c3523d4b0a638659eb7d167513ddbe7a3060eb0e04c9de y `ref_torch.exe` 55e43af54de839da52612257c78f6b78cdcfe99d1248d5c82e137ddbd0b3d1cf. Hashes del parser, pruebas y pol?tica: ver bloque GOVERNING_S16 de `../common/REF_FREEZE.md`; pruebas S-16 PASS.
