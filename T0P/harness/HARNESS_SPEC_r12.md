# Harness R12

## Ejecución y contrato

Todo binario, objeto, desensamblado y log de R12 se genera en `T0P\\harness\\logs_r12\\obj_r12`.

```powershell
cmd /c T0P\\harness\\build_r12.cmd
T0P\\harness\\logs_r12\\obj_r12\\harness_r12.exe --plan-qa T0P\\collector\\plan.json 39856a658f1ba7e70846870fc372c7f82042680d9d508f54805a4a433b78290c
powershell -NoProfile -ExecutionPolicy Bypass -File T0P\\harness\\qa_disasm_r12.ps1
```

El plan se valida por SHA-256 canónico antes de calibrar. La matriz R12 conserva cada réplica r1–r31 para las fases competency y reference_selection, las dos fronteras, m=1/4/8/16 y P0–P7/8P. `torch_mv` se omite para m>1. El preflight registra el mapa obtenido con `GetLogicalProcessorInformationEx(RelationProcessorCore)`; cada hilo se fija al LP del mapa.

El trabajo por llamada es `512·O·m`. El QA compara el tamaño real de W/X con la geometría y verifica MAC/call. H-KQ queda restringido por el cualificador a O=1024, m=1, prepared, 8P y 31 réplicas. La matriz usa un R común calibrado por unidad y candidato. Cada réplica mide el intervalo de pared de los hilos activos; las marcas TSC son por hilo y el equipo 8P se agrega desde el primer inicio hasta el último fin. Cinco warmups y márgenes de la misma carga rodean cada muestra.

## Rutas y fronteras

`prepared` reutiliza el formato de activación construido antes del intervalo; `dynamic` vuelve a empaquetar la activación en cada llamada cronometrada. El QA ejecuta ambas rutas, verifica S-12 y registra los bytes dinámicos por llamada.

- Torch conserva Wt transpuesta contigua, comprueba configuración intra/inter-op 1/1 por worker y despacho integrado. `mm`, `matmul` y `linear` usan rutas `_out`; `mv` solo corre para m=1.
- ggml plano verifica `use_ref=false`, entrada contigua y trait Q4_0 y Q8_0; usa `vec_dot` para m=1 y `llamafile_sgemm` para m≥4 cuando la build lo ofrece.
- ggml repack conserva `ggml_gemv/gemm_q4_0_8x8_q8_0`. El QA de m=1 detecta y cubre el índice de cada grupo Q8 en la frontera dinámica.

## BW_DRAM y P_instr

BW_DRAM reserva 256 MiB por worker en páginas grandes. Cada pasada hace cuatro cargas de 256 bits por iteración, una por cuarto, con acumuladores y sumas modulares. El sink de cada cuarto se compara con el contenido esperado. La QA recorre 4.194.304 líneas de 64 B, verifica una visita lógica por línea y muta la última línea de cada cuarto; detecta 4/4 mutaciones. El R de pasadas completas se calibra y queda común para los ocho workers.

P_instr usa cadenas 1/2/4/8/10/12/14 con U=8 y 12/14 con U=16. Tiene cinco warmups, R común fijo por configuración y márgenes de la misma carga. El sink es uint32 modular. `qa_disasm_r12.ps1` cuenta el desensamblado real de las nueve variantes, acumuladores independientes, dependencias y pares de entradas; guarda texto, conteos y SHA-256.

## S-20, crudos y evidencia

`telemetry_r12.py` delega validación al S-20 único de `T0P\\validator\\s20_validator.py` y archiva harness/EMON por `SessionAttemptStore` D13 en directorios nuevos. Derivados y lineage usan nombres distintos de los crudos; los crudos son de escritura única. SMOKE no aporta cifras a los gates.

Ejecuciones finales:

- Build: `HARNESS_R12_BUILD PASS`.
- Plan: `PLAN_MATRIX_QA PASS groups=216 active_events=25668 replicas_per_candidate=31 ...`.
- Desensamblado real: nueve variantes PASS; SHA-256 `6cadf8fbbc920c33164fa1d6c2f8ae047d795928e6cbc6767384d7a008c7b8e0`.
- QA por proveedor: nueve rutas/geometrías y ocho workers; ambas fronteras S-12 PASS y `work_shape=PASS`. GGML m=1/m=4, Torch mm/matmul/linear/mv y OMEGA están incluidos.
- EMON V11.61, `INST_RETIRED.ANY`, N=3: P_instr emitió 216 filas en nueve configuraciones. BW_DRAM QA: cobertura completa y mutaciones 4/4 PASS. Crudo EMON de 509 B archivado en un intento D13 nuevo.
- Smoke N=3 de proveedores: el preflight se detuvo antes de medir con `SeLockMemoryPrivilege not already enabled`; el raw de bloqueo se archivó y el cualificador devolvió `SMOKE_ONLY numeric_metrics_used=false`.
- Bloqueo real del host: `VirtualAlloc(MEM_LARGE_PAGES)` falló con Win32 1314 (el cliente no dispone de un privilegio requerido). No se cambió seguridad.
