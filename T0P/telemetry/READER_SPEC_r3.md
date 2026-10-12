# READER_SPEC — lector conjunto de telemetría S-20

## Estado y uso

Implementación nativa C++17 para Windows x64/MSVC. El harness puede invocar `reader\telemetry_reader.exe` para las rutinas DUMMY, D1, D2, los marcadores de instrucciones y las pruebas negativas, o lanzar el CLI de EMON con `reader\emon_joint_qualification.edp` y leer las filas del log. `build.cmd` produce el ejecutable, enlaza `prog_api.lib`, copia `prog_api.dll` a `reader\` y guarda el desensamblado de la rutina conocida. La adquisición operativa se hace por CLI porque la API no admite el conjunto completo.

Comandos principales: `--dummy seconds clock-map.csv`, `--d1 configured_MHz raw.csv`, `--api-probe config.xml raw.csv seconds`, `--api-d2 config.xml raw.csv N calibrated_MHz`, `--inst-test config.xml iterations raw.txt` y `--negative-tests`. El acondicionamiento corre ocho workers enteros DUMMY, fijados a LP P 0,2,…,14; no registra throughput.

## Grupo de eventos y reparto

El archivo fijado es `reader\emon_joint_qualification.edp`: `-t0.050`, un grupo, 3600 lecturas, sin multiplexación. Incluye en la misma adquisición:

- `CPU_CLK_UNHALTED.THREAD`, `CPU_CLK_UNHALTED.REF_TSC`, `INST_RETIRED.ANY`;
- `L2_RQSTS.DEMAND_DATA_RD_MISS`, `L2_RQSTS.ALL_DEMAND_DATA_RD`;
- `OFFCORE_REQUESTS.L3_MISS_DEMAND_DATA_RD`, `OFFCORE_REQUESTS.DEMAND_DATA_RD`;
- `MSR 0x606 STATIC/PACKAGE`, `0x611 FREERUN/PACKAGE`, `0x19C STATIC/THREAD`, `0x1B1 STATIC/PACKAGE`.

Los cuatro eventos L2/LLC comparten grupo, período y alcance. Los cuatro contadores programables de caché caben en los ocho selectores P-core disponibles; THREAD, REF_TSC e INST_RETIRED usan contadores fijos. No se extrapola ninguna fracción multiplexada. El preflight lee `0x1A2` aparte, solo para decodificar TCC: añadirlo al grupo mezcló las filas térmicas de salida.

EMON entrega vectores por LP; los P-cores del equipo son LP 0,2,…,14 y sus hermanos SMT se conservan aparte. Las entradas N/A de E-core no se usan como datos P-core. El paquete es un único alcance package, repetido en la tabla de cobertura por cada core para que cada canal/core tenga estado explícito.

## API y tiempo

Se inspeccionaron `emon_api.h`, `EMON_API_README.txt`, `prog_api.dll` y `prog_api.lib` de VTune 2026.4. La API ofrece `EMONReadCounts`, `EMONReadCountsOnCurrentCPU`, `EMONDataGetTimestamp` y lecturas de entradas. Su README describe lecturas acumulativas cuyo delta se obtiene por resta, pero no fija época/unidad temporal de `EMONDataGetTimestamp`. Además, EMON descarta los MSR STATIC al usar acumulación (`static events are invalid in EMON accumulation mode`), así que no puede adquirir este bloque completo.

En 62 endpoints de D2, el ajuste afín empírico timestamp API→QPC tuvo pendiente 0,0050327927 ticks QPC/tick API, tasa implícita 1.986.968.388 ticks/s y offset 172.273.998.701 ticks QPC; el orden no fue monótono. Residuo máximo 1.031,972 ms, bracket QPC máximo 1,006 ms y cota residuo+medio-bracket 1.032,475 ms. Falla D1 y la API no se usa como reloj de adquisición.

El CLI conserva hora civil con resolución de 1 ms. El lector la alinea a QPC monotónico mediante muestras emparejadas con `GetSystemTimePreciseAsFileTime`, con bracket QPC antes/después. En la ejecución conjunta: residuo lineal máximo 0,08845 ms, bracket máximo 0,0817 ms y redondeo civil 0,5 ms; cota conservadora por extremo 0,6293 ms. Esa correspondencia sí queda por debajo de 1 ms. Mapa y cálculo: `logs\conditioning_qpc_utc_map.csv`, `scripts\analyze_joint_conditioning.ps1` y `logs\conditioning_analysis_temporal_status.txt`.

## Intervalos y fórmulas

El inicio y fin del bloque completo vienen de los marcadores QPC BEGIN/END del mapa, no del primer/último registro conservado. Cada ventana reconstruida conserva su intervalo real; la unión de intervalos se recorta a `[t_inicio,t_fin]` y los solapamientos no se cuentan dos veces:

`coverage = measure(union(I_k ∩ [t_inicio,t_fin])) / (t_fin - t_inicio)`.

La incertidumbre U por extremo se aplica conservadoramente: la cota inferior reduce dos U por ventana y amplía el denominador del bloque en 2U; la cota superior del hueco añade 2U. Para aceptar: cobertura inferior ≥95 %, hueco superior ≤20 ms, ≥20 ventanas/core/canal, ventana real ≤100 ms y U≤1 ms. Una ventana no se crea por interpolación.

Para el worker fijado al LP P `i`, con frecuencia TSC consultada y calibrada:

- `f_activo,i = f_TSC × ΣΔTHREAD_i / ΣΔREF_TSC_i` (cociente de sumas, no promedio de cocientes);
- `a_i = ΣΔREF_TSC_i / (f_TSC × ΣΔt_i)` y `f_pared,i = ΣΔTHREAD_i / ΣΔt_i`, solo diagnósticos;
- L2 y LLC: se suman numerador y denominador del mismo alcance/intervalos; denominador cero produce `NOT_ESTIMABLE`.

La energía usada en esta ejecución es `INTERVAL_DELTA`: EMON `FREERUN` ya entrega el delta y no se vuelve a diferenciar. `P_pkg,k = u_E × ΣΔE / ΣΔt_real` sobre las ventanas completas observadas en k. `0x606=658947` da exponente 14 y `u_E=2^-14 J`; configuración y salida se vinculan en el `.edp` y en el log. Para una entrada de snapshot absoluto de 32 bits, la regla separada es `(final−inicial) mod 2^32`; no se confunde con `INTERVAL_DELTA`.

Para `0x19C`, bit 31 valida la lectura; bits de estado actual pares y sus históricos impares se guardan separados. Margen TCC debe ser ≥5 °C. La temperatura `TCC−margen` es una estimación. En `0x1B1` se conservan flags actuales/históricos, pero bit 31 nunca se interpreta como el indicador de validez de `0x19C`.

## Estados y clasificación

Cada canal/core informa `PASS`, `FAIL` o `HOLD_INSTRUMENTATION` y motivo en `logs\conditioning_analysis_coverage.csv`. HOLD se usa si la alineación instrumental supera 1 ms; en el resto se evalúan los umbrales temporales. Los límites `PL1`, `PL2`, `POWER_TIME_WINDOWS` y `PERF_LIMIT_REASONS_0x64F` quedan registrados como `NOT_READABLE_WITH_AVAILABLE_INTERFACE`; no se infieren de potencia estable.

Clasificación S-20 B: flag causal de limitación activo + frecuencia estable + térmica válida → limitación de potencia observada estable; potencia/frecuencia estables sin causa observable → régimen estable sin causa determinada; datos térmicos inválidos o fallo de instrumentación → condición no válida. Un plateau por sí solo no identifica PL1/PL2.

