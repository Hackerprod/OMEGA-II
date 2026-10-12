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


## Corrección R3: unidades de hueco y auditoría sin descarte silencioso

Causa raíz hallada en `scripts\analyze_joint_conditioning_r2g.ps1`: la cota de hueco se construía como milisegundos (`1000 × QPC_delta / QPC_frequency`) más incertidumbre expresada en segundos; luego se multiplicaba otra vez por 1000 para exportarla y se comparaba el valor intermedio con `0,020` segundos. Esto inflaba el hueco y el rechazo por un factor 1000. La corrección conserva internamente segundos (`gap_upper_seconds = QPC_delta / QPC_frequency + 2U/1000`), compara contra 0,020000001 s, y convierte a ms solo al exportar.

El analizador genera `*_interval_audit.csv` con todos los grupos completos, no solo los usados: índice crudo, hora civil, UTC, QPC de inicio/fin, duración derivada de THREAD, hueco frente al grupo anterior, `overlaps_DUMMY_block` y motivo. Los grupos fuera del bloque monotónico se explicitan; grupos sin timestamp, eventos requeridos o duración positiva se cuentan como rechazo con la razón. Un fallo D2 de delta global también queda señalado por intervalo/LP, no se promedia para hacerlo desaparecer.

La regresión negativa `gap_1_687ms` reproduce el fallo previo: gap observado 1,687253 ms más 2×0,6293 ms de incertidumbre da 2,945853 ms, por debajo del máximo de 20 ms. La prueba pasa; las 12 pruebas negativas anteriores siguen pasando. R3 en la captura D3 nueva: por cada core/canal, 2910 ventanas, cobertura inferior 97,3176 %, hueco superior 2,882612 ms, ventana máxima 80,391745 ms; todos pasan sin cambiar los límites.

La repetición única deja D2 en FAIL (índices 13 y 28 en los ocho P; cocientes mínimos globales 0,0000571 y 0,051107 frente a 0,98–1,02) e INST_RETIRED en FAIL (déficit 1,323413 % frente a máximo 1 %). La relación estática del bucle 3N+4 fue comprobada contra el desensamblado. La API de marker devolvió estado 0, pero no entregó el número esperado; el origen interno del subconteo no queda aislado y el criterio permanece HOLD. D1 y D3 cumplen.



## Decision oficial R4: solo EMON CLI conjunto

El lector oficial de las sesiones de telemetria es EMON CLI por `-collect-edp` con `reader\emon_joint_qualification.edp`: grupo conjunto de `THREAD`, `REF_TSC`, `INST_RETIRED.ANY`, cache/LLC y los registros MSR de lectura requeridos; periodo `-t0.050`; alineacion de hora civil por mapa QPC/UTC, U=0,6293 ms por extremo. La API acumulativa queda **NO OFICIAL** y no se utiliza durante sesiones oficiales. Dos ejecuciones independientes dieron lecturas rancias deterministas en los intervalos 13 y 28, simultaneamente en ocho P-cores: deltas de timestamp 242.992 y 216.058.909 en intervalos QPC de ~2 s (normal ~4,23e9), ratios REF_TSC 0,000057 y 0,051107. La recurrencia ~cada 15 intervalos es coherente con un snapshot periodico sin refrescar.

Para D2, los limites temporales se toman de marcas de ventanas EMON completas alineadas a QPC. Cada intervalo agrega 40 ventanas consecutivas (2,385-2,503 s); se omiten grupos de arranque y no se interpolan/extrapolan contadores. Se aplica incertidumbre de +/-0,6293 ms por extremo. Matriz completa: `logs\D2_cli_ref_tsc_r4.csv`.

La prueba de instrucciones debe mantener el grupo conjunto CLI y las marcas QPC de inicio/fin del codigo conocido. Si una ventana EMON cruza un limite, no se presenta como conteo exacto de la region: se conserva y reporta el factor de cobertura. Una ventana parcial no se convierte en estimacion valida multiplicando por cobertura cuando la carga no es uniforme. La ejecucion R4 tuvo 19,858 ms de region dentro de una ventana de 63 ms (31,5205 %); resultado `HOLD_INSTRUMENTATION`, hasta tener una resolucion que encierre la region con ventanas completas o instrumentacion temporal que mida la region completa. Umbral de INST_RETIRED: +/-1 %, sin cambios.
