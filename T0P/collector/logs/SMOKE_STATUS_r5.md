# Estado SMOKE r5 — 2026-10-11

Todas las ejecuciones de esta carpeta son SMOKE/QA, no son mediciones oficiales ni sustentan una decisión de rendimiento.

## Comprobaciones ejecutadas

| Fase | Ejecución | Estado |
|---|---|---|
| Plan y corte existente | `collector.exe --self-test`; `--plan-check plan.json` | PASS sintético; no ejercita proveedores ni hardware |
| Kernel OMEGA | `qualify_kernel_controls.exe` | PASS; `m={1,4,8,16}`, resultado numérico y páginas grandes |
| ggml | `qualify_ggml_qa.exe` con `OMP_NUM_THREADS=1`, `OMP_DYNAMIC=FALSE` y DLLs locales | PASS; flat/repack, páginas grandes, `timed=false` |
| Torch | `qualify_torch_qa.exe` con DLLs Torch en `PATH` | PASS; `*_out`, página grande y despacho observado, `timed=false` |
| Telemetría | `telemetry_reader.exe --negative-tests` | PASS; negativos del validador únicamente |
| Gate oficial | `collector.exe --official` | HOLD esperado, exit 20; AUTHORIZED no existe y no se inició captura |
| Expulsión | `evict_qa.exe` | PASS local: N=31, CLFLUSHOPT, afinidad/cobertura 8 shards, large pages; eventos LLC `AWAITING_EMON_WINDOW_BINDING` |
| QA kernel completa | `qa_kernel.exe <T0P\common\data>` | Detenida: ejecutaba barrido extenso de O, no el SMOKE reducido pedido; no se computa resultado |

Los comandos completos y salidas están en `smoke_r5_*.log`. La falta inicial de DLL/variables de entorno se corrigió en las ejecuciones PASS; los logs conservan el estado final de cada proveedor.

## Bloqueo para completar la cualificación física

S-20 y `T0P\telemetry\READER_SPEC_r5.md` registran que EMON CLI es la vía oficial; su CLI de lectura API no es una alternativa válida. En la repetición conjunta, `REF_TSC` devolvió deltas obsoletos en los intervalos 13 y 28, con cocientes mínimos 0,0000571 y 0,051107 frente a 0,98–1,02. `INST_RETIRED` tuvo error −1,323413 %, fuera de ±1 %. PL1, PL2, ventanas de potencia y `PERF_LIMIT_REASONS_0x64F` figuran como `NOT_READABLE_WITH_AVAILABLE_INTERFACE`.

Comando de adquisición definido por S-20 (la repetición no produjo un canal de frecuencia válido):

```powershell
& 'C:\Program Files (x86)\Intel\oneAPI\vtune\2026.4\bin64\emon.exe' -collect-edp 'edp_file=C:\Projects\OMEGA\Ejercise\T0P\telemetry\reader\emon_joint_qualification.edp'
```

Error de calificación registrado: `D2_API ... verdict=FAIL/HOLD`; REF_TSC ratios `0.0000571` y `0.051107` (esperado 0.98–1.02); `INST_RETIRED` déficit `1.323413%` (máximo 1%). `-help` no es un parámetro válido para este CLI (exit 41: `The -help switch is invalid`), por lo que no se utilizó como sonda. No se inició una captura oficial.

El QA local de expulsión acabó con `EXECUTION=PASS QA=PASS CACHE_QUALIFICATION=AWAITING_EMON_ANALYSIS`; no acredita los criterios LLC sin enlazar sus ventanas con EMON. Hasta corregir y volver a calificar esos canales con la misma configuración y umbrales, no se pueden aceptar ventanas por core/canal, potencia, frecuencia activa, acondicionamiento de 180 s ni bloques de competencia. Por tanto no se puede declarar preflight físico, adquirir competencias reales, completar el gate de páginas consumidor contra la salida de adquisición del colector ni producir un smoke extremo a extremo válido. No se modificó ningún umbral.

## Alcance pendiente

La implementación de colector con proveedores y gates integrados sigue pendiente, al igual que completar el SMOKE extremo a extremo. La parte 2 solo podrá iniciarse cuando la cualificación de la parte 1 esté autorizada y pase. No se creó `AUTHORIZED.json` ni se modificó ningún archivo fuera de `T0P\collector\` y `T0P\qualify\`.
