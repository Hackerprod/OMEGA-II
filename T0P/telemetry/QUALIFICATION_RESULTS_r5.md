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

## Estado anterior a R3 (histórico; corregido en sección R3 final)

| Criterio | Evidencia y causa observada | Corrección de implementación propuesta |
|---|---|---|
| D1, timestamp de API | El timestamp no fue monótono y el ajuste API→QPC tuvo residuo >1 s; además, el API rechaza los MSR STATIC requeridos. | No usar el timestamp de API como reloj. Mantener el mapeo CLI/QPC de la captura y, si se requiere API, encapsular cada lectura con QPC y no combinarla con canales STATIC que la API descarta. |
| D2, REF_TSC | En intervalos 13 y 28, los ocho cores tuvieron deltas REF_TSC anómalamente pequeños. La falla fue común a la lectura, no un desvío aislado de un P-core. | Añadir detección de deltas incompletos y descartar la ventana; obtener un nuevo set D2 desde el grupo CLI conjunto o corregir la adquisición acumulativa API para tomar snapshots coherentes antes de recalcular 31 intervalos válidos. |
| D2, INST_RETIRED | La región marcada quedó 1,7296 % por debajo del conteo estático conocido; excede ±1 %. | Revisar atribución de `EMONReadMarkerCounts` y alcance thread del contador; contrastar la región con deltas bracketed de `INST_RETIRED.ANY` sobre el mismo LP y límites de región. Mantener el umbral ±1 %. |
| D3, todos los canales | Un hueco de 1687,253 ms rompe el máximo de 20 ms aunque haya 2963 ventanas y cobertura de 97,28 %. | Conservar las cotas actuales; añadir timestamps QPC por fila al capturador/stream y detección de pausa para localizar la pérdida. Corregir el camino de captura que interrumpe la serie y repetir el bloque oficial cuando esté autorizado. |


## R3: causa raíz, corrección aplicada y repetición única (2026-10-11)

Esta sección reemplaza la calificación temporal previa. No se modificaron umbrales de S-20.

### Hueco D3: defecto de unidades en el analizador

`Measure-Coverage` convertía el hueco QPC a milisegundos y sumaba la incertidumbre en segundos; después el informe lo multiplicaba otra vez por 1000. También comparaba ese valor con `0.020` segundos. El mismo hueco real de 1,685994 ms se presentaba como 1685–1687 ms y se rechazaba. No era una pausa de EMON ni una pérdida de filas.

| Evidencia | Grupo anterior | Grupo siguiente | Resultado |
|---|---:|---:|---:|
| Índice | 1235 | 1236 | — |
| Marca civil local | 18:56:58.533 | 18:56:58.585 | delta entre marcas: 52 ms |
| UTC | 2026-10-12 01:56:58.533Z | 2026-10-12 01:56:58.585Z | — |
| QPC de intervalo | fin 182232940384 | inicio 182232957243.94 | hueco: 1,685994 ms |
| Duración inferida por THREAD | 62,169044 ms | 50,314006 ms | ambos grupos completos |
| Cota conservadora, U=0,6293 ms/extremo | — | — | 2,944594 ms, PASS ≤20 ms |

Se analizaron 3600 grupos: 3600 completos, 0 sin marca/eventos requeridos y 0 con duración no positiva. Grupos 32–2994 (2963) intersectan el bloque DUMMY. Los grupos 0–31 y 2995–3599 (637) quedan fuera de sus límites QPC; no se descartó ningún grupo interior. El mayor delta civil crudo fue 67 ms. El mapa previo tiene 11772 muestras; dentro de ±2 s de este hueco, el mayor intervalo adyacente QPC fue 20,344 ms, sin salto cercano a 1,7 s. Esto descarta pérdida EMON y pausa larga de DUMMY en ese instante.

El lector ahora calcula y compara `gap_upper_seconds` en segundos, exporta `gap_upper_ms` solo para presentación y produce CSV de auditoría con todos los grupos, índices, UTC, QPC, duración y razón de selección. La prueba `gap_1_687ms` reproduce el error anterior y pasa con cota 2,945853 ms.

### D2 y contador de instrucciones

| Medición | Resultado | Evidencia |
|---|---|---|
| D1 (captura existente) | PASS | 31×8; fTSC medio 2112,003 MHz; error relativo máximo 1,486×10⁻⁶ frente a 2112 MHz (<0,1 %). |
| D2 anterior | FAIL, 29/31 por P | Índices 13 y 28 fuera de 0,98–1,02 en los 8 P; razones ~0,009055 y ~0,086833. |
| D2 repetición única | FAIL, 29/31 por P | Índice 13: QPC 2,00556 s, ΔAPI=242992, ratio 0,0000571–0,0000572. Índice 28: QPC 2,00175 s, ΔAPI=216058909, ratio 0,051107–0,0511071. Los mismos dos índices fallan en los 8 P. |
| INST_RETIRED previo | FAIL | 294811093/300000004; déficit 5188911 (1,729637 %). |
| INST_RETIRED repetición única | FAIL | 296029764/300000004; déficit 3970240 (1,323413 %); afinidad LP0 correcta, marker_status=0 y sink=100000000. |

En D2, QPC sigue avanzando durante ~2 s, pero tanto los deltas REF_TSC de los ocho LP como el timestamp EMON avanzan solo una fracción pequeña. La coincidencia entre cores descarta un error de mapeo de un P individual y el bug de `all_ok` ya corregido. Es evidencia de pausa/preempción global del sistema virtualizado o pérdida global de conteos en EMON; estos crudos no distinguen ambas causas. El mapa DUMMY no cubre esos instantes D2, por lo que no atribuyo la causa a una interrupción concreta. Los dos intervalos se marcan inválidos, no se ocultan ni se promedian.

El conteo esperado sí cuadra con `known_loop.asm`: `mov` + `xor` + `add/dec/jnz` por iteración + `mov` + `ret` = 3N+4 = 300000004; N=100000000 y el sink confirma N. El desensamblado no está desenrollado y no contiene CPUID. Los cuatro movimientos de entrada/salida ya están incluidos; aun excluyéndolos cambiaría solo 4 instrucciones. Interrupciones/kernel, si las contara `INST_RETIRED.ANY`, sumarían y no explicarían un déficit. La región está delimitada directamente por `EMONMarkerEnter/Leave` y la API devuelve marker_status=0. El contador de marker/API sigue subcontando; con estas dos capturas no se puede aislar si es cobertura interna del marker, PMU virtualizado o pérdida de lectura. **Causa específica de INST_RETIRED no demostrada; criterio queda FAIL/HOLD**, sin otra repetición por el límite fijado.

### Repetición D3 conjunta, 180 s

Crudos nuevos: `conditioning_joint_raw_r3.txt` y `conditioning_qpc_utc_map_r3.csv`. DUMMY: 180,005004 s, ocho LP fijados, afinidad PASS, sin throughput. EMON entregó 3600 grupos y pie temporal; 3600 completos, 0 rechazados; 2910 intersectan el bloque y 690 quedan fuera (grupos 0–31 y 2942–3599). No se ajustó ningún criterio.

| Canal obligatorio | Estado en P0–P7 | Ventanas/core | Cobertura inferior mínima | Hueco superior máximo | Ventana máxima |
|---|---|---:|---:|---:|---:|
| THREAD/REF_TSC | PASS | 2910 | 97,3176 % | 2,882612 ms | 80,391745 ms |
| L2 demand data read | PASS | 2910 | 97,3176 % | 2,882612 ms | 80,391745 ms |
| LLC demand data read | PASS | 2910 | 97,3176 % | 2,882612 ms | 80,391745 ms |
| 0x19C térmico | PASS | 2910 | 97,3176 % | 2,882612 ms | 80,391745 ms |
| 0x611 energía | PASS | 2910 | 97,3176 % | 2,882612 ms | 80,391745 ms |
| 0x1B1 térmico paquete | PASS | 2910 | 97,3176 % | 2,882612 ms | 80,391745 ms |

El máximo hueco se ubica entre grupos 127 y 128 (UTC 02:19:15.308Z / 02:19:15.373Z): QPC fin 195600690000 e inicio 195600706214.12; hueco 1,621412 ms. U=0,6306 ms produce 2,882612 ms. El mapa nuevo tiene 10917 muestras; el mayor intervalo QPC dentro de ±2 s fue 30,8 ms, sin salto largo de DUMMY.

| P | f activa media (MHz) | rango de tres bins/ media | margen TCC mínimo |
|---:|---:|---:|---:|
| P0 | 5089,398905 | 0,000280 % | 60 °C |
| P1 | 5089,857295 | 0,000850 % | 59 °C |
| P2 | 5089,598436 | 0,000415 % | 58 °C |
| P3 | 5089,630328 | 0,000120 % | 56 °C |
| P4 | 5089,584272 | 0,000392 % | 58 °C |
| P5 | 5089,705739 | 0,000386 % | 58 °C |
| P6 | 5089,616702 | 0,000244 % | 56 °C |
| P7 | 5089,714280 | 0,000044 % | 59 °C |

Potencia de paquete por bins de 30 s: 79,905925 / 80,471747 / 80,021938 W; media 80,133203 W, rango/ media 0,7061 % (PASS ≤5 %). Margen TCC mínimo 56 °C; flags actuales e históricos térmicos 0x0000; controles térmicos PASS. Acondicionamiento PASS.

### Estado final

S-20 D1 PASS; D2 FAIL por ventanas parciales globales en 13 y 28 de los 8 P, e INST_RETIRED FAIL por 1,323413 %; D3 temporal/canales, frecuencia, energía observable y térmica PASS. La calificación global de telemetría sigue **HOLD_INSTRUMENTATION** por D2 e INST_RETIRED. No se hizo otra repetición ni se cambiaron umbrales.
La clasificación por fila/LP de D2 repetida está en `D2_intervals_r3_repeat_v2.csv` y `D2_summary_r3_repeat_v2.csv`: los intervalos 13 y 28 quedan como `INVALID_PARTIAL_DELTA_REF_TSC_OUTSIDE_0.98_1.02`; no se excluyen del denominador 31 ni se presentan como PASS.


## Decision de lector y cierre de una pasada R4 (2026-10-11)

**Lector oficial unico:** EMON CLI conjunto con `-collect-edp`, configuracion `reader\emon_joint_qualification.edp` (`-t0.050`, 3600 grupos) y alineacion civil/QPC de U=0,6293 ms por extremo. La captura conserva un solo grupo conjunto y no interpola ni extrapola contadores. El canal API queda **NO OFICIAL** y no se usa en sesiones oficiales: en dos ejecuciones independientes fallo a la vez en los ocho P-cores en los intervalos 13 y 28. Alrededor de 6:41/7:18, QPC avanzo ~2,006/~2,002 s mientras `api_timestamp_delta` fue 242.992/~216.058.909 frente a ~4,23e9 normal y los ratios REF_TSC fueron 0,000057/~0,051107. La repeticion cada ~15 intervalos (~30 s) y la coincidencia en ocho cores identifican una lectura acumulativa API rancia; no una pausa del sistema ni un defecto de core. Evidencia: `logs\D2_raw.csv`, `logs\D2_raw_r3_repeat.csv`, `logs\D2_intervals_r3_repeat_v2.csv` y `logs\D2_summary_r3_repeat_v2.csv`. S-20 D1 puede usar cualquiera de los dos canales; esta decision aplica al lector oficial de la sesion.

### D2 REF_TSC con el lector oficial

Se analizaron 31 intervalos por cada LP P 0,2,...,14, construidos con 40 ventanas EMON consecutivas y completas por intervalo. Se descartaron los primeros 40 grupos de arranque; los limites temporales son marcas de grupo mapeadas a QPC y cada intervalo dura 2,385000-2,503002 s. Los deltas REF_TSC suman solo ventanas completas entre esos limites; no se interpolaron ni extrapolaron valores. U=0,6293 ms por extremo; la tabla CSV incluye los cocientes y los limites conservadores al propagar +/-2U al denominador. Todos los 248 cocientes quedan dentro de 0,98-1,02, sin tocar el umbral.

| P-core / LP | n | Ratio minimo | Ratio maximo | Rango conservador con U | Resultado |
|---|---:|---:|---:|---:|---|
| P0 / 0 | 31 | 0,993993116 | 0,994890543 | 0,993476639-0,995404880 | PASS |
| P1 / 2 | 31 | 0,993985339 | 0,994880111 | 0,993468867-0,995393765 | PASS |
| P2 / 4 | 31 | 0,993991449 | 0,994894963 | 0,993474973-0,995408625 | PASS |
| P3 / 6 | 31 | 0,993993955 | 0,994897066 | 0,993477478-0,995410729 | PASS |
| P4 / 8 | 31 | 0,993996526 | 0,994900259 | 0,993480048-0,995413924 | PASS |
| P5 / 10 | 31 | 0,993997537 | 0,994900729 | 0,993481058-0,995414394 | PASS |
| P6 / 12 | 31 | 0,993997053 | 0,994900376 | 0,993480574-0,995414041 | PASS |
| P7 / 14 | 31 | 0,993998193 | 0,994902448 | 0,993481714-0,995416114 | PASS |

La matriz completa intervalo x core (248 cocientes), grupos fuente y duraciones esta en `logs\D2_cli_ref_tsc_r4.csv`; resumen en `logs\D2_cli_ref_tsc_summary_r4.csv`.

### INST_RETIRED.ANY, una ejecucion CLI

Se ejecuto una sola vez el ensamblador existente, N=100.000.000 y esperado exacto 3N+4=300.000.004, con EMON CLI conjunto activo. LP0 quedo reservado al bucle y DUMMY corrio en LP 2,4,6,8,10,12,14; afinidades y `sink=100000000` pasaron. EMON entrego 240/240 grupos y pie temporal. QPC delimito una region de 19,857900 ms; U de alineacion local estimada 0,5026 ms.

El intervalo de captura que solapa la region dura 63,000 ms, de los cuales la region ocupa 19,857900 ms (factor de cobertura 31,5205 %); es la unica ventana solapada y ninguna ventana queda completamente contenida en la region. Su `INST_RETIRED.ANY` de LP0 fue 306.408.787: si se atribuyera entero al bucle daria +2,136261 % (fuera de +/-1 %). El escalado proporcional por cobertura da 96.581.509 y -67,806164 %, pero no es un estimador valido: el bucle es una rafaga, no una tasa uniforme en la ventana. Por ello el resultado queda **HOLD_INSTRUMENTATION**, no PASS.

Ruido de fondo: en las otras 239 ventanas LP0 la mediana fue 83.328 instrucciones y el maximo 959.523; para los 43,1421 ms externos a la region, la mediana proyecta ~57.063 instrucciones, muy por debajo de los ~6,35 millones que separan el conteo bruto del esperado. Esto descarta que el fondo tipico observado explique por si solo la diferencia, pero una unica ventana de 63 ms no permite separar interferencia transitoria/SEP de conteo dentro del bucle. Los fallos API previos eran deficits de 1,323413 % y 1,729637 %; esta captura CLI no los replica como deficit, pero tampoco califica el evento: muestra una ventana parcial y sobreconteo no atribuido. Detalle reproducible: `logs\INST_RETIRED_CLI_r4.txt`, `logs\inst_cli_window_audit_r4.csv`, `logs\inst_cli_raw_r4.txt`, `logs\inst_cli_qpc_utc_map_r4.csv`.

### Estado

D1 PASS, D2 REF_TSC CLI PASS, D3 conjunto permanece PASS segun R3, pero INST_RETIRED no cumple +/-1 % con resolucion/cobertura demostrada. Estado global: **HOLD_INSTRUMENTATION**. Bloqueo concreto: la region de 19,858 ms es menor que la ventana CLI observada de 63 ms; el factor de cobertura no permite atribuir las instrucciones de una rafaga. No se hizo una segunda captura ni se cambio ningun umbral.
