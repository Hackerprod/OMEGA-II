# Núcleo común de referencia

El criterio conserva el oráculo FP64 de producto por grupos y la cota S-12. Para cancelación casi exacta se exige `S/|r| >= 1024`; el caso `S=0` requiere resultado exactamente cero. Las entradas congeladas y sus hashes están en `NEW_FIXTURES_FROZEN.json` y los manifiestos SHA-256.

## Estado gobernante S-15

El asignador común usa `VirtualAlloc(MEM_RESERVE|MEM_COMMIT|MEM_LARGE_PAGES)`, redondea a `GetLargePageMinimum()` e intenta habilitar `SeLockMemoryPrivilege` solo con `AdjustTokenPrivileges` en el token actual. No modifica las políticas del sistema.

`qa_gate.py` consume `PAGE_EXPECTED_S15.json`, exige cobertura completa de cada registro por fixture, m, candidata y buffer, y valida tamaño, alias, páginas consultadas, `Valid` y `LargePage`. Emite `QA_NUMERICAL`, `DISPATCH_QUALIFIED`, `PAGE_POLICY` y `REFERENCE_READY` por candidata. Las pruebas negativas para ggml y Torch rechazan región ausente, cobertura de una sola fila, duplicado contradictorio, cero páginas, página no válida y la eliminación de 119 de 120 registros.

Las ejecuciones gobernantes están documentadas por separado en `ref_torch/REF_FREEZE.md` y `ref_ggml/REF_FREEZE.md`; los HOLD por privilegio ausente son historial sustituido por S-15.
