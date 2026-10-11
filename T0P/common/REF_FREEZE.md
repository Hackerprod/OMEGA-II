# Freeze com?n

## HISTORIAL ? S-12/S-13

Las fixtures hist?ricas y nuevas permanecen congeladas byte por byte. Los estados anteriores se conservan como historial; el estado S-14 gobierna el criterio, la asignaci?n y el gate.

## GOBERNANTE ? S-14

`cancellation_high` usa S/|r| mayor o igual que 1024; cuando S=0 se exige resultado exactamente cero. El asignador usa `VirtualAlloc(MEM_RESERVE|MEM_COMMIT|MEM_LARGE_PAGES)`, redondea con `GetLargePageMinimum()` y solo intenta habilitar `SeLockMemoryPrivilege` mediante `AdjustTokenPrivileges` en el token propio. Si no est? concedido, informa HOLD y usa fallback alineado marcado; no cambia pol?ticas.

`qa_gate.py` requiere cada celda esperada, el barrido completo y cada control obligatorio. Emite por candidata `QA_NUMERICAL`, `DISPATCH_QUALIFIED`, `PAGE_POLICY` y `REFERENCE_READY`; HOLD se conserva separado de FAIL. Las pruebas negativas pasan: fallo inyectado de barrido, control obligatorio ausente y privilegio presente con p?ginas peque?as.


## GOVERNING_S16

S-16 corrige la cobertura de p?ginas por registro. `qa_gate.py` exige `queried_pages = ceil(((address mod p) + bytes) / p)`, `valid_pages = queried_pages` y `large_pages = queried_pages`; valida direcciones, tama?os positivos y suma sin overflow de 64 bits. La pol?tica congelada `PAGE_QUERY_STEP_S16.json` registra `dwPageSize=4096` de `GetSystemInfo` en esta m?quina (`dwAllocationGranularity=65536`, no utilizado). Los logs hist?ricos S-15 no registran el paso; para ambos se declara expl?citamente el uso del paso verificado de esta m?quina.

Hashes SHA-256: `qa_gate.py` a96728870e3c7604c916215d196838830327bc47024bb0a722698ee4c05f9416; `qa_gate_tests.py` 2c1050c35d8a3bf0fc90619ca9a84422187b5e5aabbb0ce376910e8f2a567ca4; `PAGE_QUERY_STEP_S16.json` 5472a44352fddaeb7796da33766e9d4e4d8103764991bc72adb38e48b272cab8. Pruebas: `qa_gate_tests_s16.txt` PASS. Los dos logs crudos no cambiaron: Torch b03332562ae5d65f1b3928680beb54c30550fdcd18ddd3627c353ba99d08c2de; ggml a736fc9201eb5e0b24b1fc36efd992d6a999136940cf5f069dd995149713cea5.
