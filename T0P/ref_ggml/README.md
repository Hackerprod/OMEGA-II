# Referencia ggml CPU

Estado gobernante S-15 y hashes: [REF_FREEZE.md](REF_FREEZE.md), [REF_FREEZE.json](REF_FREEZE.json).

QA completa: `adapter/qa_s15_governing.txt`; salida del gate: `adapter/qa_gate_s15_governing.txt`; código de salida: `adapter/qa_s15_exit_code.txt`. ggml plana y repack pasan los cuatro estados. El `dst` de `ggml_mul_mat` y la salida repack permanecen directamente en common Buffer; 240/240 registros confirman páginas válidas y large-page.

Las pruebas negativas están en `../common/qa_gate_tests_s15.txt`. La ejecución S-14 y los HOLD de privilegio anteriores son historial. Las lagunas declaradas sobre `qa_gate.py_preS14` y `.lib/.exp` históricos no se presentan como reproducciones byte a byte.
