# Referencia Torch CPU

El estado gobernante S-14 y la evidencia de freeze est?n en [REF_FREEZE.md](REF_FREEZE.md) y [REF_FREEZE.json](REF_FREEZE.json). QA cruda: `adapter/verify_common_all.txt`; pruebas unitarias: `adapter/verifier_unit_tests.txt`.

## Estado gobernante S-14

Los buffers efectivos X/W/Wt usan `from_blob` sobre regiones del asignador com?n con deleter, sin `clone` que abandone la regi?n. QA num?rica, barridos y controles obligatorios pasan para mm, matmul, linear y mv (m=1); el gate registra despacho cualificado.

`QueryWorkingSetEx` report? p?ginas v?lidas y `LargePage=0`. Al faltar `SeLockMemoryPrivilege` en el token, `PAGE_POLICY=HOLD` y `REFERENCE_READY=HOLD`. `mkldnn_linear` permanece diagn?stico/no comparable. S-13 queda como historial.
