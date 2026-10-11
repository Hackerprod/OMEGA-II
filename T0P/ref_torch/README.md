# Referencia Torch CPU

Estado gobernante S-15 y hashes: [REF_FREEZE.md](REF_FREEZE.md), [REF_FREEZE.json](REF_FREEZE.json).

QA completa: `adapter/qa_s15_governing.txt`; salida del gate: `adapter/qa_gate_s15_governing.txt`; código de salida: `adapter/qa_s15_exit_code.txt`; unit tests: `adapter/verifier_unit_tests_s15.txt`. Todos los candidatos gobernantes pasan los cuatro estados. Y se asigna en common Buffer y las variantes `_out` conservan su dirección y storage. La cobertura de páginas incluye X y los pesos efectivos del operador, además de Y: 390/390 registros válidos y large-page.

Las pruebas negativas de cobertura están en `../common/qa_gate_tests_s15.txt`. S-12/S-13/S-14 y el HOLD previo por privilegio quedan como historial. Las lagunas de trazabilidad sobre `qa_gate.py_preS14` y `.lib/.exp` históricos se declaran en el freeze; no se reclama reproducción byte a byte.


## GOVERNING_S16

Reevaluaci?n con el parser S-16 de `adapter/qa_s15_governing.txt` intacto: `adapter/qa_gate_s16_governing.txt`. `torch_mm`, `torch_matmul`, `torch_linear` y `torch_mv`: QA_NUMERICAL, DISPATCH_QUALIFIED, PAGE_POLICY y REFERENCE_READY = PASS. Crudo SHA-256 b03332562ae5d65f1b3928680beb54c30550fdcd18ddd3627c353ba99d08c2de; adaptador sin cambios: `ref_torch.cpp` 213b8b99199e50ecd6c3523d4b0a638659eb7d167513ddbe7a3060eb0e04c9de y `ref_torch.exe` 55e43af54de839da52612257c78f6b78cdcfe99d1248d5c82e137ddbd0b3d1cf. Hashes del parser, pruebas y pol?tica: ver bloque GOVERNING_S16 de `../common/REF_FREEZE.md`; pruebas S-16 PASS.
