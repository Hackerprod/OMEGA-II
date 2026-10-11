# Núcleo común de referencia

El criterio conserva el oráculo FP64 de producto por grupos y la cota S-12. Para cancelación casi exacta se exige `S/|r| >= 1024`; el caso `S=0` requiere resultado exactamente cero. Las entradas congeladas y sus hashes están en `NEW_FIXTURES_FROZEN.json` y los manifiestos SHA-256.

## Estado gobernante S-15

El asignador común usa `VirtualAlloc(MEM_RESERVE|MEM_COMMIT|MEM_LARGE_PAGES)`, redondea a `GetLargePageMinimum()` e intenta habilitar `SeLockMemoryPrivilege` solo con `AdjustTokenPrivileges` en el token actual. No modifica las políticas del sistema.

`qa_gate.py` consume `PAGE_EXPECTED_S15.json`, exige cobertura completa de cada registro por fixture, m, candidata y buffer, y valida tamaño, alias, páginas consultadas, `Valid` y `LargePage`. Emite `QA_NUMERICAL`, `DISPATCH_QUALIFIED`, `PAGE_POLICY` y `REFERENCE_READY` por candidata. Las pruebas negativas para ggml y Torch rechazan región ausente, cobertura de una sola fila, duplicado contradictorio, cero páginas, página no válida y la eliminación de 119 de 120 registros.

Las ejecuciones gobernantes están documentadas por separado en `ref_torch/REF_FREEZE.md` y `ref_ggml/REF_FREEZE.md`; los HOLD por privilegio ausente son historial sustituido por S-15.


## GOVERNING_S16

S-16 corrige la cobertura de p?ginas por registro. `qa_gate.py` exige `queried_pages = ceil(((address mod p) + bytes) / p)`, `valid_pages = queried_pages` y `large_pages = queried_pages`; valida direcciones, tama?os positivos y suma sin overflow de 64 bits. La pol?tica congelada `PAGE_QUERY_STEP_S16.json` registra `dwPageSize=4096` de `GetSystemInfo` en esta m?quina (`dwAllocationGranularity=65536`, no utilizado). Los logs hist?ricos S-15 no registran el paso; para ambos se declara expl?citamente el uso del paso verificado de esta m?quina.

Hashes SHA-256: `qa_gate.py` a96728870e3c7604c916215d196838830327bc47024bb0a722698ee4c05f9416; `qa_gate_tests.py` 2c1050c35d8a3bf0fc90619ca9a84422187b5e5aabbb0ce376910e8f2a567ca4; `PAGE_QUERY_STEP_S16.json` 5472a44352fddaeb7796da33766e9d4e4d8103764991bc72adb38e48b272cab8. Pruebas: `qa_gate_tests_s16.txt` PASS. Los dos logs crudos no cambiaron: Torch b03332562ae5d65f1b3928680beb54c30550fdcd18ddd3627c353ba99d08c2de; ggml a736fc9201eb5e0b24b1fc36efd992d6a999136940cf5f069dd995149713cea5.
