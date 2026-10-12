# Harness R13

## Construcción y preflight

Todos los binarios, objetos, crudos, capturas EMON y registros de R13 se guardan en `T0P\harness\logs_r13\obj_r13`.

```powershell
cmd /c T0P\harness\build_r13.cmd
T0P\harness\logs_r13\obj_r13\harness_r13.exe --plan-qa T0P\collector\plan.json 39856a658f1ba7e70846870fc372c7f82042680d9d508f54805a4a433b78290c
powershell -NoProfile -ExecutionPolicy Bypass -File T0P\harness\qa_disasm_r13.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File T0P\harness\run_smoke_r13.ps1
```

`run_smoke_r13.ps1` inicia EMON con el EDP de telemetría y ejecuta el harness como procesos separados. El EDP R13 amplía el límite a 7.200 intervalos de 50 ms; el runner exige que ambos procesos concluyan, el crudo tenga 864 filas, el resumen 266 filas, la preflight JSON sea válida y EMON incluya `INST_RETIRED.ANY` y su footer. La sesión es `SMOKE`; sus métricas no se usan en gates.

El plan se valida por SHA-256 canónico antes de calibrar. El mapa P-core/LP procede de `GetLogicalProcessorInformationEx(RelationProcessorCore)`, queda en `preflight_r13.json` y se verifica en cada worker. El trabajo MAC/call es `512·O·m`.

## Privilegio de páginas grandes

El harness llama directamente a `t0common::enable_large_page_privilege()` para activar `SeLockMemoryPrivilege` en el token del proceso. Un estado `Deshabilitado` antes de esa llamada no es bloqueo. No se cambia la asignación del derecho ni la política de seguridad. Si `AdjustTokenPrivileges` devuelve `ERROR_NOT_ALL_ASSIGNED`, el preflight registra el código Win32 y la medición queda bloqueada por ese error del sistema.

## Celdas y fronteras

El smoke N=3 mide OMEGA, ggml y Torch con m=1 y m=4, en `prepared` y `dynamic`; `torch_mv` solo se ejecuta con m=1. Cada celda calibra un R común para los ocho workers y conserva tres réplicas. `prepared` reutiliza la activación preparada antes del cronómetro. `dynamic` vuelve a empaquetarla en cada llamada y registra sus bytes por llamada. Los registros incluyen las dos operaciones distintas, el trabajo `512·O·m`, la afinidad observada y la ruta S-12.

- Torch conserva Wt transpuesta contigua, política interna de hilos 1/1 por worker y despacho integrado. `mm`, `matmul` y `linear` usan rutas `_out`; `mv` solo corre para m=1.
- ggml plano verifica `use_ref=false`, entrada contigua y traits Q4_0/Q8_0; usa `vec_dot` para m=1 y `llamafile_sgemm` para m≥4 cuando está disponible.
- ggml repack conserva `ggml_gemv/gemm_q4_0_8x8_q8_0` y valida el empaquetado de activación en ambas fronteras.

## BW_DRAM y P_instr

BW_DRAM reserva 256 MiB por worker en páginas grandes. Cada pasada lee los cuatro cuartos completos, con cuatro cargas vectoriales de 256 bits y acumulación modular. El R común fija el número de pasadas completas. El sink de cada cuarto se compara con el valor esperado. La QA cubre las 4.194.304 líneas de 64 B y detecta la mutación de la última línea de cada cuarto.

P_instr mide nueve variantes: cadenas 1/2/4/8/10/12/14 con U=8 y 12/14 con U=16. Usa cinco warmups, R fijo por configuración, márgenes de la misma carga y sink `uint32_t` modular. `qa_disasm_r13.ps1` verifica el desensamblado enlazado, el número de instrucciones por acumulador y las dependencias; guarda desensamblado, conteos y SHA-256.

## S-20 y D13

`telemetry_r13.py` delega la validación al S-20 único de `T0P\validator\s20_validator.py`. El harness archiva crudos de escritura única mediante `SessionAttemptStore` D13 en un directorio de intento nuevo. Los derivados y lineage usan nombres separados. `SMOKE` queda fuera de los gates.

## Evidencia R13

- El plan y el desensamblado real se validan con los comandos de preflight anteriores.
- El smoke de proveedores queda en `logs_r13\obj_r13\smoke_N3_r13_emon_final2`; la captura de EMON correspondiente es `emon_smoke_N3_r13_edp.txt`. Para repetir solo BW_DRAM y P_instr contra el QA del binario final: iniciar `emon.exe -collect-edp edp_file=<EDP R13>` y ejecutar `harness_r13.exe --aux-smoke <directorio nuevo> <logs_r13\obj_r13\disassembly_qa_r13.json>`. Evidencia final: `aux_N3_r13_emon_final\harness_raw.jsonl` y `emon_aux_N3_r13.txt`; las nueve variantes llevan el SHA del desensamblado QA vigente.
- El manifiesto R13 cubre todos los archivos de harness con sufijo `_r13`; el manifiesto de collector cubre los archivos `qualify_r13.py`, `COLLECTOR_SPEC_r13.md` y `hashes_r13.ps1`.