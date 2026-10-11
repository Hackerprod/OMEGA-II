# N?cleo com?n de referencia

El criterio com?n conserva el or?culo FP64 de producto por grupos y la cota S-12. Para cancelaci?n casi exacta se exige `S/|r| ? 1024` (S/|r| mayor o igual que 1024); el caso S=0 requiere resultado exacto cero. Las entradas congeladas y sus hashes est?n en `NEW_FIXTURES_FROZEN.json` y los manifiestos SHA-256.

## Estado gobernante S-14

El asignador com?n usa `VirtualAlloc(MEM_RESERVE|MEM_COMMIT|MEM_LARGE_PAGES)`, redondea a `GetLargePageMinimum()` e intenta habilitar `SeLockMemoryPrivilege` solo con `AdjustTokenPrivileges` en el token actual. Si el derecho no est? concedido, retorna HOLD de recurso y marca el fallback alineado normal; no modifica pol?ticas.

`qa_gate.py` verifica la cobertura completa de celdas, barrido de coordenadas y controles obligatorios. Emite `QA_NUMERICAL`, `DISPATCH_QUALIFIED`, `PAGE_POLICY` y `REFERENCE_READY` por candidata. Las pruebas negativas de barrido fallido, control ausente y privilegio presente con p?ginas no grandes pasan. Los resultados anteriores S-12/S-13 son historial.
