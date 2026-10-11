# Referencia ggml CPU

Estado gobernante S-15 y hashes: [REF_FREEZE.md](REF_FREEZE.md), [REF_FREEZE.json](REF_FREEZE.json).

QA completa: `adapter/qa_s15_governing.txt`; salida del gate: `adapter/qa_gate_s15_governing.txt`; código de salida: `adapter/qa_s15_exit_code.txt`. ggml plana y repack pasan los cuatro estados. El `dst` de `ggml_mul_mat` y la salida repack permanecen directamente en common Buffer; 240/240 registros confirman páginas válidas y large-page.

Las pruebas negativas están en `../common/qa_gate_tests_s15.txt`. La ejecución S-14 y los HOLD de privilegio anteriores son historial. Las lagunas declaradas sobre `qa_gate.py_preS14` y `.lib/.exp` históricos no se presentan como reproducciones byte a byte.


## GOVERNING_S16

Reevaluaci?n con el parser S-16 de `adapter/qa_s15_governing.txt` intacto: `adapter/qa_gate_s16_governing.txt`. `ggml_flat` y `ggml_repack`: QA_NUMERICAL, DISPATCH_QUALIFIED, PAGE_POLICY y REFERENCE_READY = PASS. Crudo SHA-256 a736fc9201eb5e0b24b1fc36efd992d6a999136940cf5f069dd995149713cea5; adaptador sin cambios: `main.cpp` 11bc6be35d0c1cfca2afc02015a3c825d9981f2dc7400d42ecfeb371c55e585c y `adapter.exe` 3dcbad85a709739474bff7a176be4c5b7e85e7939c14db67316843aa071109c7. Hashes del parser, pruebas y pol?tica: ver bloque GOVERNING_S16 de `../common/REF_FREEZE.md`; pruebas S-16 PASS.
