# Referencia Torch CPU

Estado gobernante S-15 y hashes: [REF_FREEZE.md](REF_FREEZE.md), [REF_FREEZE.json](REF_FREEZE.json).

QA completa: `adapter/qa_s15_governing.txt`; salida del gate: `adapter/qa_gate_s15_governing.txt`; código de salida: `adapter/qa_s15_exit_code.txt`; unit tests: `adapter/verifier_unit_tests_s15.txt`. Todos los candidatos gobernantes pasan los cuatro estados. Y se asigna en common Buffer y las variantes `_out` conservan su dirección y storage. La cobertura de páginas incluye X y los pesos efectivos del operador, además de Y: 390/390 registros válidos y large-page.

Las pruebas negativas de cobertura están en `../common/qa_gate_tests_s15.txt`. S-12/S-13/S-14 y el HOLD previo por privilegio quedan como historial. Las lagunas de trazabilidad sobre `qa_gate.py_preS14` y `.lib/.exp` históricos se declaran en el freeze; no se reclama reproducción byte a byte.
