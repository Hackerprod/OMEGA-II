# QUALIFICATION_RESULTS — S-20 D1–D3

Todas las cargas son DUMMY nativas de enteros, afinidad fija a P-core, sin OMEGA, ggml ni Torch; no se midió throughput. Registros crudos y comandos están en `logs\`.

## D1 — frecuencia TSC y correspondencia temporal

- `f_TSC` consultada en EMON: 2112,000 MHz; calibración RDTSCP/QPC del DUMMY: media 2112,003049 MHz. N=31 intervalos por cada LP P 0,2,…,14 (248 muestras), cada duración ≥2 s.
- Media de error relativo: 1,443589×10⁻⁶; error máximo: 1,486423×10⁻⁶, umbral 0,001. D1 de calibración TSC: PASS en los ocho cores. Detalle: `D1_summary.csv`, `D1_raw.csv`, `D1_console.txt`.
- La correspondencia API→QPC falla: endpoints no monótonos, residuo máximo 1031,972 ms, bracket máximo 1,006 ms, cota conservadora 1032,475 ms. El CLI con mapeo QPC/UTC sí dio U=0,6293 ms en la ejecución conjunta. Véase `D2_reanalysis.txt` y `conditioning_analysis_temporal_status.txt`.

## D2 — REF_TSC e instrucciones retiradas

Con ocho DUMMY continuos fijados a los ocho P, 31 intervalos de ≥2 s/core. `D2_raw.csv` contiene los deltas y QPC/API brackets; `D2_reanalysis.csv` corrige la interpretación de la consola original.

| P-core / LP | REF_TSC dentro de [0,98;1,02] | mínimo | máximo | media |
|---|---:|---:|---:|---:|
| P0 / 0 | 29/31 | 0,00905532 | 0,999944 | 0,938416 |
| P1 / 2 | 29/31 | 0,00905532 | 0,999943 | 0,938416 |
| P2 / 4 | 29/31 | 0,00905541 | 0,999943 | 0,938416 |
| P3 / 6 | 29/31 | 0,00905549 | 0,999944 | 0,938416 |
| P4 / 8 | 29/31 | 0,00905547 | 0,999944 | 0,938417 |
| P5 / 10 | 29/31 | 0,00905550 | 0,999944 | 0,938417 |
| P6 / 12 | 29/31 | 0,00905545 | 0,999943 | 0,938417 |
| P7 / 14 | 29/31 | 0,00905555 | 0,999944 | 0,938417 |

Fallan los intervalos 13 y 28 en los ocho cores (ratios aproximados 0,009055 y 0,086833). Resultado D2 REF_TSC: FAIL. `D2_console.txt` dijo PASS porque el primer código reiniciaba el acumulador del veredicto en cada intervalo; se corrigió la implementación y el análisis de filas brutas es el resultado autoritativo.

El bucle conocido tiene N=100.000.000 iteraciones; desensamblado guardado en `known_loop_disassembly.txt`: inicialización de 2 instrucciones, cuerpo `add/dec/jne` de 3 instrucciones por iteración y retorno de 2; esperado 300.000.004 retiradas. Medido por `INST_RETIRED.ANY`: 294.811.093; error relativo 0,01729637 (1,7296 %), límite 1 %. FAIL. Región delimitada con marcador EMON en LP0; los ocho DUMMY ocuparon los LP hermanos SMT. Logs: `known_instruction_result.txt`, consola y stderr.

## D3 — bloque conjunto y canales

Configuración conjunta de una sola pasada, 3600 grupos, `-t0.050`; EMON terminó con 3600 grupos, footer de tiempo real y stderr vacío. La salida `FREERUN` del 0x611 se registró como `INTERVAL_DELTA`. Acondicionamiento ejecutado una vez durante 180,000507 s; ocho afinidades correctas.

| Canal obligatorio, por cada P-core | Ventanas | Cobertura inferior | Hueco máximo superior | Ventana máxima | Estado |
|---|---:|---:|---:|---:|---|
| THREAD + REF_TSC | 2963 | 97,2800 % | 1687,253 ms | 66,758 ms | FAIL: hueco >20 ms |
| Par L2 demand (miss/all) | 2963 | 97,2800 % | 1687,253 ms | 66,758 ms | FAIL: hueco >20 ms |
| Par LLC demand (miss/demand) | 2963 | 97,2800 % | 1687,253 ms | 66,758 ms | FAIL: hueco >20 ms |
| IA32_THERM_STATUS 0x19C | 2963 | 97,2800 % | 1687,253 ms | 66,758 ms | FAIL: hueco >20 ms |
| MSR_PKG_ENERGY_STATUS 0x611 | 2963 | 97,2800 % | 1687,253 ms | 66,758 ms | FAIL: hueco >20 ms |
| IA32_PACKAGE_THERM_STATUS 0x1B1 | 2963 | 97,2800 % | 1687,253 ms | 66,758 ms | FAIL: hueco >20 ms |

Alineación CLI QPC/UTC U=0,6293 ms, ventanas ≥20, cobertura ≥95 % y ventana máxima ≤100 ms: pasan; el hueco medido de 1,687 s hace fallar D3 para todos los canales/core. No se infiere ni rellena el hueco. CSV por core/canal: `conditioning_analysis_coverage.csv`.

### Acondicionamiento: últimos 90 s en tres tramos de 30 s

DUMMY=TRUE. Por P-core, `f_activo` medio quedó entre 5089,350 y 5089,842 MHz; la mayor excursión `(f_max−f_min)/f_media` fue 2,3174×10⁻⁵ (P4), límite 0,03: PASS los ocho. `a_i` quedó entre 1,000011 y 1,000066; `f_pared,i` entre 5089,468 y 5090,157 MHz, solo diagnósticos.

Con `u_E=2^-14 J`, potencia de paquete de los tres intervalos: 84,30656 W, 83,67990 W, 84,04435 W. Media 84,01027 W; excursión relativa 0,0074593, límite 0,05: PASS. No se dividió por 50 ms nominales; se usó el tiempo observado. Fracciones demand-only (agregación de numerador y denominador por el par LP del mismo P-core; DUMMY, diagnóstico; no certifican el bloque porque D3 falla):

| P-core | L2 miss/all demand | LLC miss/demand |
|---|---:|---:|
| P0 | 0,214807 | 0,302467 |
| P1 | 0,216996 | 0,310560 |
| P2 | 0,268117 | 0,182347 |
| P3 | 0,303367 | 0,145258 |
| P4 | 0,250137 | 0,187212 |
| P5 | 0,288835 | 0,287327 |
| P6 | 0,403378 | 0,088690 |
| P7 | 0,414315 | 0,068074 |

Datos: `conditioning_analysis_stability.csv`, `conditioning_analysis_cache_fractions.csv` y `conditioning_analysis_temporal_status.txt`.

En la serie térmica: `0x19C` válido por bit31, flags actuales e históricos separados y a cero; margen digital mínimo 55 °C (requisito ≥5 °C). `0x1B1` flags se conservan por separado y sin atribuir validez a bit31. `TCC=100 °C`; `TCC−margen` da estimaciones de 38–45 °C, no temperaturas calibradas.

## Pruebas negativas y calificación global

Las 12 pruebas negativas deterministas pasan: canal/core ausente, denominador cero (`NOT_ESTIMABLE`), no finito, intervalo duplicado, orden temporal inválido, hueco >20 ms, cobertura <95 %, <20 ventanas, ventana >100 ms, confusión snapshot/delta y flag térmico activo con margen <5 °C. La prueba positiva de unión de intervalos solapados pasa sin doble conteo. Registro: `negative_tests.txt`.

Estados por canal: los seis obligatorios son `FAIL` por hueco temporal; ninguno queda `HOLD_INSTRUMENTATION` porque la alineación CLI cumple U≤1 ms. D1 TSC pasa; D2 REF_TSC falla en dos ventanas/core e INST_RETIRED falla 1,73 %; D3 falla el hueco máximo. Clasificación del bloque: **condición no válida por fallo instrumental temporal**. A título diagnóstico, frecuencia y potencia DUMMY fueron estables, pero no hay causa de limitación determinada y las interfaces disponibles no exponen:

- `PL1 = NOT_READABLE_WITH_AVAILABLE_INTERFACE`
- `PL2 = NOT_READABLE_WITH_AVAILABLE_INTERFACE`
- `POWER_TIME_WINDOWS = NOT_READABLE_WITH_AVAILABLE_INTERFACE`
- `PERF_LIMIT_REASONS_0x64F = NOT_READABLE_WITH_AVAILABLE_INTERFACE`

No se atribuye el plateau a un límite concreto. El estado del proceso EMON no expuso `Process.ExitCode` en PowerShell; la captura se verifica por 3600 grupos, footer de tiempo real y stderr vacío (`conditioning_run_status_full.txt`). El primer wrapper reportó un falso fallo por ese valor nulo; su comprobación se cambió a validar los grupos/footer.

