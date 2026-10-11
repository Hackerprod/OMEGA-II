# Freeze com?n

## HISTORIAL ? S-12/S-13

Las fixtures hist?ricas y nuevas permanecen congeladas byte por byte. Los estados anteriores se conservan como historial; el estado S-14 gobierna el criterio, la asignaci?n y el gate.

## GOBERNANTE ? S-14

`cancellation_high` usa S/|r| mayor o igual que 1024; cuando S=0 se exige resultado exactamente cero. El asignador usa `VirtualAlloc(MEM_RESERVE|MEM_COMMIT|MEM_LARGE_PAGES)`, redondea con `GetLargePageMinimum()` y solo intenta habilitar `SeLockMemoryPrivilege` mediante `AdjustTokenPrivileges` en el token propio. Si no est? concedido, informa HOLD y usa fallback alineado marcado; no cambia pol?ticas.

`qa_gate.py` requiere cada celda esperada, el barrido completo y cada control obligatorio. Emite por candidata `QA_NUMERICAL`, `DISPATCH_QUALIFIED`, `PAGE_POLICY` y `REFERENCE_READY`; HOLD se conserva separado de FAIL. Las pruebas negativas pasan: fallo inyectado de barrido, control obligatorio ausente y privilegio presente con p?ginas peque?as.
