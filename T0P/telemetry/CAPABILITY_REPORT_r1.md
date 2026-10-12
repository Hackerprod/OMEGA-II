# Informe de capacidad de telemetría — S-19

**Resultado:** la vía PMU ya instalada es Intel EMON/SEP 2026.4, con los drivers `sepdrv5` y `sepdal` activos. Puede leer ciclos y eventos P-core sin instalar nada ni alterar seguridad. La captura de 10 ms, sin embargo, pierde grupos y no entrega marcas temporales suficientes; hoy no cumple la cualificación temporal de S-19. El plan de energía sí queda registrado con `powercfg`. PL1/PL2, sus ventanas, temperatura CPU y flags completos no quedan cualificados.

## Equipo y permisos

- `Win32_Processor`: Intel Core i7-13700F; identificador `BFEBFBFF000B0671`. CPUID leaf 1 EAX `0x000B0671` ⇒ family 6, model `0xB7` (183), stepping 1. EMON identifica Raptor Lake-DT, P-core Raptor Cove y E-core Gracemont; topología: 8 P físicos/16 LP, 8 E/8 LP. El mapa LP↔core está en [emon_version_topology.txt](logs/emon_version_topology.txt).
- Windows 11 Enterprise LTSC, 10.0.26100, build 26100.
- `whoami /groups`: `BUILTIN\Administradores` habilitado y etiqueta de integridad `High`; proceso elevado/administrador: **sí**. Evidencia: [whoami_groups.txt](logs/whoami_groups.txt).
- VTune Profiler 2026.4.0, build 632893; EMON V11.61, SEP 5.61 público. `sc query sepdrv5` y `sc query sepdal`: ambos `RUNNING`, exit code 0. No se instaló ni cargó ningún driver. Evidencia: [inventario](logs/intel_tools_inventory.json), [SEP](logs/sc_sepdrv5.txt), [SEP DAL](logs/sc_sepdal.txt).
- No se encontró Intel XTU, Intel DTT ni PCM en el inventario de software. `wpr.exe` sí existe; `xperf.exe` y `pmcsources.exe` no están en PATH. Evidencia: [tools](logs/intel_tools_inventory.json), [WPR](logs/wpr_profiles.txt), [PMC sources](logs/wpr_pmcsources.txt), [xperf](logs/xperf_where.json).

## Matriz (a)–(d) × interfaz

Cada celda usa uno de los tres estados solicitados. Las limitaciones y la acción del propietario para resolver las celdas no cualificadas se detallan tras la matriz.

| Interfaz (versión/permisos) | (a) Frecuencia activa/core | (b) Fallos de L2 y LLC por P-core | (c) PL/térmico | (d) Plan y rendimiento |
|---|---|---|---|---|
| **Intel EMON/SEP** — VTune 2026.4.0 / EMON 11.61; admin y SEP activos | **DISPONIBLE_CON_LIMITACIONES** — `CPU_CLK_UNHALTED.THREAD`/`REF_TSC`; 261/300 grupos (87,0 % proxy), 39 descartados. No timestamps para cobertura temporal/huecos. [A1] | **DISPONIBLE_CON_LIMITACIONES** — eventos de demanda y ciclos leídos por LP P; captura del mapa 270/300 grupos (90,0 % proxy), 30 descartados. Agregación LP→P-core y ventanas válidas no cualificadas. [B1] | **DISPONIBLE_CON_LIMITACIONES** — `STATIC_TEMPERATURE_TARGET` decodifica provisionalmente 100 °C en 23/24 LP; un LP difiere. `IA32_PACKAGE_THERM_STATUS` queda bruto; `IA32_PKG_POWER_LIMIT` se rechazó. Sin PL1/PL2/ventanas ni temperatura actual CPU cualificados. [C1] | **NO_DISPONIBLE_SIN_CAMBIAR_SEGURIDAD** — EMON no informa del plan ni de la configuración de Windows. Acción: consultar `powercfg`/WMI, disponibles abajo. [D1] |
| **Contadores Windows / PDH** — `Get-Counter`, build 26100; usuario elevado | **DISPONIBLE_CON_LIMITACIONES** — `Processor Performance\Processor Frequency` = 2100 MHz en 24 LP y `% of Maximum Frequency` = 100 %; no demuestran frecuencia efectiva/APERF. Cmdlet limita el intervalo a 1 s. [A2] | **NO_DISPONIBLE_SIN_CAMBIAR_SEGURIDAD** — los conjuntos presentes no exponen un par de eventos L2 y LLC por P-core con los denominadores requeridos. Acción: usar la PMU Intel verificada o un recolector Windows que exponga esos eventos. [B2] | **DISPONIBLE_CON_LIMITACIONES** — `Processor State Flags` devuelve 1, sin decodificación que pruebe PROCHOT/PL1/PL2; no hay sensor CPU en estos conjuntos. [C2] | **NO_DISPONIBLE_SIN_CAMBIAR_SEGURIDAD** — Perflib no es la fuente de política de plan consultada. Acción: consultar `powercfg` o WMI/CIM. [D2] |
| **ETW/PMC (WPR)** — WPR 10.0.26100; `wpr -pmcsources`, lectura | **DISPONIBLE_CON_LIMITACIONES** — fuentes `UnhaltedCoreCycles` y `UnhaltedReferenceCycles`, pero WPR presenta fuentes de muestreo con intervalo mínimo indicado de 4096 eventos, no snapshots APERF/MPERF cada 10 ms. [A3] | **DISPONIBLE_CON_LIMITACIONES** — fuentes genéricas `CacheMisses`, `LLCReference` y `LLCMisses`; no aparece una pareja L2/LLC con denominadores demand-only y atribución P-core. [B3] | **DISPONIBLE_CON_LIMITACIONES** — WPR lista perfil `Thermal`, pero la enumeración no ofrece lecturas PL1/PL2, ventanas, sensores de paquete/core y flags solicitados. [C3] | **NO_DISPONIBLE_SIN_CAMBIAR_SEGURIDAD** — la consulta PMC/ETW no es una interfaz de configuración de energía. Acción: `powercfg`/WMI. [D3] |
| **WMI/CIM** — Windows 11; lectura | **NO_DISPONIBLE_SIN_CAMBIAR_SEGURIDAD** — `Win32_Processor.MaxClockSpeed=2100` es nominal y no se usa como frecuencia efectiva. No apareció una clase APERF/MPERF por core. Acción: usar la interfaz PMU verificada. [A4] | **NO_DISPONIBLE_SIN_CAMBIAR_SEGURIDAD** — los namespaces/clases revisados no exponen contadores PMU L2/LLC. Acción: usar la interfaz PMU verificada. [B4] | **DISPONIBLE_CON_LIMITACIONES** — `MSAcpi_ThermalZoneTemperature` entrega `TZ00_0=3010` (30,10 °C); no se mapea al paquete/core CPU ni a un límite térmico de CPU. No hay clase WMI PL1/PL2 encontrada. [C4] | **DISPONIBLE_CON_LIMITACIONES** — `Win32_PowerPlan` y `Win32_PowerSettingDataIndex` leen el plan activo y los índices de rendimiento; cobertura menos directa que `powercfg`. [D4] |
| **Intel DTT/XTU** — no instalado | **NO_DISPONIBLE_SIN_CAMBIAR_SEGURIDAD** — el producto no está instalado. Acción propietaria: verificar primero que una versión firmada y compatible con HVCI exponga APERF/MPERF por core; instalarla solo si puede conservar las políticas actuales. [A5] | **NO_DISPONIBLE_SIN_CAMBIAR_SEGURIDAD** — no instalado y no hay evidencia de que DTT/XTU exponga estos eventos PMU por P-core. Acción propietaria: usar EMON/SEP ya disponible; la instalación de XTU por sí sola no resuelve esta celda. [B5] | **NO_DISPONIBLE_SIN_CAMBIAR_SEGURIDAD** — no instalado. Acción propietaria: si desea esa vía, verificar antes que la versión firmada para este equipo lea PL1/PL2, ventanas, CPU package/core y flags con Secure Boot/HVCI intactos; instalarla solo tras esa verificación. [C5] | **NO_DISPONIBLE_SIN_CAMBIAR_SEGURIDAD** — no instalado y no es necesario para consultar el plan. Acción: usar `powercfg`/WMI, ya disponibles. [D5] |
| **powercfg** — Windows 11, lectura | **NO_DISPONIBLE_SIN_CAMBIAR_SEGURIDAD** — no mide frecuencia efectiva por core. Acción: usar una fuente de ciclos efectiva cualificada, no WMI nominal. [A6] | **NO_DISPONIBLE_SIN_CAMBIAR_SEGURIDAD** — no programa ni lee eventos L2/LLC. Acción: EMON/SEP o recolector PMU compatible y cualificado. [B6] | **NO_DISPONIBLE_SIN_CAMBIAR_SEGURIDAD** — `/energy` genera diagnóstico energético, no lecturas de PL1/PL2/temperaturas CPU. Acción: interfaz de sensores/MSR verificada y compatible. [C6] | **DISPONIBLE_Y_CUALIFICABLE** — `powercfg /getactivescheme`, `/q SCHEME_CURRENT SUB_PROCESSOR` y `/energy /duration 3`; plan Alto rendimiento y valores registrados. No se cambió ningún ajuste. [D0] |

## Frecuencia efectiva (a)

La fórmula ensayada con los contadores PMU es `f_activa = f_ref × ΔCPU_CLK_UNHALTED.THREAD / ΔCPU_CLK_UNHALTED.REF_TSC`. EMON informa `TSC Freq=2112 MHz`; Intel define `REF_TSC` como ciclos de referencia que aumentan a la frecuencia TSC, y `THREAD` como ciclos efectivos mientras el LP no está detenido. La frecuencia sale del cociente de ambos contadores; el TSC no se usa por sí solo como sustituto. Los eventos son contadores fijos P-core y quedan documentados para Raptor Cove.

La captura de 3 s pidió 300 ventanas de 10 ms, produjo 261 grupos y EMON descartó 39 por discrepancia temporal. La cobertura **por cantidad de grupos** es 87,0 %, no cobertura temporal; faltan timestamps por grupo y no se puede calcular el hueco máximo real. El campo inicial compartido por grupo, interpretado provisionalmente como ticks de intervalo a 2112 MHz, da media 15,695 ms, mínimo 10,607 ms y máximo observado 26,162 ms; al no ser una marca temporal documentada por EMON, sirve solo como indicio adicional de que 10 ms no queda cualificado. El CSV ofrece `f_activa` por LP; resumen de medias por pareja SMT/P-core:

| P-core | LP | Media EMON (MHz) |
|---:|---:|---:|
| P0 | 0–1 | 5.072 |
| P1 | 2–3 | 5.073 |
| P2 | 4–5 | 5.072 |
| P3 | 6–7 | 5.072 |
| P4 | 8–9 | 5.082 |
| P5 | 10–11 | 5.080 |
| P6 | 12–13 | 5.072 |
| P7 | 14–15 | 5.068 |

Estas medias describen solo la captura exploratoria, no pasan S-19: la cobertura temporal no está demostrada, hay grupos descartados y la actividad de fondo varió. Windows `Processor Performance` recogió cuatro muestras en aproximadamente 3 s: la frecuencia reportada fue 2100 MHz y 100 % de máximo en los 24 LP; `% Processor Performance` varió 0–349. Microsoft describe los contadores de utilidad como trabajo normalizado por estados de rendimiento, no como APERF/MPERF. Por ello no se usan como frecuencia efectiva. [Definición Windows](https://learn.microsoft.com/en-us/troubleshoot/windows-client/performance/cpu-usage-exceeds-100), [estructura Perflib](https://learn.microsoft.com/en-us/windows/win32/perfctrs/using-the-perflib-functions-to-consume-counter-data).

El lector reproducible es [capture_emon_idle.ps1](scripts/capture_emon_idle.ps1). Reporta cobertura por grupos, duración interna observada, hueco máximo (no observable), monotonía (no aplicable a deltas) y rollover (no observable porque EMON no expone valores acumulativos ni anchura). Salidas: [emon_reader_report.json](logs/emon_reader_report.json) y [emon_effective_samples.csv](logs/emon_effective_samples.csv). **Resultado: hoy no se puede cualificar cadencia de 10 ms, cobertura temporal ≥95 % ni huecos ≤20 ms con este volcado de EMON.**

## Mapa de eventos L2/LLC (b)

El CPUID reportado es `GenuineIntel-6-B7` (family 6, model B7, stepping 1); la hoja de datos Intel de 13.ª generación identifica `0xB0671` como CPUID B0 de Raptor Lake. El `mapfile.csv` oficial de Intel asigna `06_B7` a los eventos P-core de Alder Lake; la página Intel de esos eventos incluye explícitamente microarquitecturas Golden Cove **y Raptor Cove**. Por tanto, las definiciones P-core están documentadas para la microarquitectura de este modelo y el mapa oficial incluye su CPUID. [CPUID Intel 13.ª gen](https://edc.intel.com/content/www/us/en/design/products/platforms/details/raptor-lake-s/13th-generation-core-processors-datasheet-volume-1-of-2/005/cpuid/), [mapfile Intel](https://raw.githubusercontent.com/intel/perfmon/main/mapfile.csv), [eventos P-core Intel](https://perfmon-events.intel.com/platforms/alderlake/core-events/p-core/).

Mapa predefinido y demanda-only:

| Magnitud | Numerador | Denominador | Interpretación/prefetch |
|---|---|---|---|
| Fracción de solicitudes de datos con fallo L2 | `L2_RQSTS.DEMAND_DATA_RD_MISS` | `L2_RQSTS.ALL_DEMAND_DATA_RD` | Ambos son solicitudes de lectura de datos de demanda; el miss es true-miss, excluye merges y cuenta cada acceso una vez. `L2_RQSTS.ALL_HWPF` y `L2_RQSTS.SWPF_HIT/MISS` separan prefetch; no incluirlos en el denominador de demanda. Prefetch de hardware puede influir en el estado de caché y queda sin alterar. |
| Fracción de lecturas de demanda que fallan LLC | `OFFCORE_REQUESTS.L3_MISS_DEMAND_DATA_RD` | `OFFCORE_REQUESTS.DEMAND_DATA_RD` | Numerador y denominador son lecturas de datos de demanda enviadas al uncore; no usar `OFFCORE_REQUESTS.DATA_RD`, que incluye demanda y prefetch. Prefetch de hardware permanece sin cambios; prefetch software se mantiene desactivado según S-19. |

El mapa EMON asigna las parejas LP 0–1, 2–3, …, 14–15 a P0–P7. Para obtener una fracción por P-core se suman primero los dos contadores numeradores y los dos denominadores de la pareja, y después se divide; no se promedian dos porcentajes LP. La captura prueba disponibilidad de contadores en las 16 LP P, pero su cobertura temporal insuficiente impide cualificar la métrica por P-core para S-19.

Alternativa descriptiva compatible: `MEM_LOAD_RETIRED.L2_MISS/(L2_MISS+L2_HIT)` y análogo L3 con `L3_MISS/(L3_MISS+L3_HIT)`. Son eventos de instrucciones de carga retiradas, no solicitudes de demanda mutuamente exclusivas garantizadas para instrucciones con varios uops; no los usaría como el denominador gobernante si están disponibles los eventos `L2_RQSTS`/`OFFCORE_REQUESTS` anteriores. En la documentación Intel, L2 miss/hit son `EventSel=D1H`, umask `10H/02H`; L3 miss/hit `D1H`, umask `20H/04H`. La documentación de L2 demanda diferencia prefetch y demanda. [Eventos Intel y descripciones](https://perfmon-events.intel.com/platforms/alderlake/core-events/p-core/).

La captura directa [emon_event_map_idle_3s.txt](logs/emon_event_map_idle_3s.txt) incluye los cuatro eventos del mapa y ciclos; los contadores L2/LLC son `N/A` en LP E-core y presentes en los LP P. Se obtuvieron 270/300 grupos (90,0 % proxy), 30 descartados. EMON emite cuentas por LP; para la métrica de P-core se agregan LP hermanos correspondientes según el mapa de topología. **Los eventos se documentan y se pueden leer, pero esta captura exploratoria no cualifica su ventana/cobertura S-19 ni la cobertura de todos los P-core bajo la sesión gobernante.** Reproducción: [capture_emon_event_map.ps1](scripts/capture_emon_event_map.ps1); resumen: [emon_event_map_capture_summary.txt](logs/emon_event_map_capture_summary.txt).

## Potencia, termales y throttling (c)

- EMON `-C IA32_PACKAGE_THERM_STATUS,STATIC_TEMPERATURE_TARGET,STATIC_PKG_POWER_SKU_UNIT,IA32_PKG_POWER_LIMIT` rechazó el último evento: `invalid or the device does not exist`. `STATIC_TEMPERATURE_TARGET` da `0x00641400` en 23/24 LP; bits [23:16] son `0x64`, que Intel define para Raptor Lake como objetivo de activación TCC de 100 °C. Un LP da `0x000A0E03`, así que la lectura no queda cualificada por core. `IA32_PACKAGE_THERM_STATUS` se leyó como bruto `0x88440000` y no lo interpreto como flags sin validación de alcance/modelo. No hay temperatura actual CPU. [Hoja Intel 13.ª gen: Adaptive Thermal Monitor](https://edc.intel.com/content/www/us/en/design/products/platforms/details/raptor-lake-s/13th-generation-core-processors-datasheet-volume-1-of-2/004/adaptive-thermal-monitor/). Archivo crudo: [emon_power_3s.txt](logs/emon_power_3s.txt).
- WMI/CIM devuelve `ACPI\ThermalZone\TZ00_0 = 3010`, equivalente a 30,10 °C; el nombre y la clase no lo vinculan con CPU package/core. No aparecieron clases WMI Intel de PL ni flags CPU. [wmi_cim_snapshot.json](logs/wmi_cim_snapshot.json).
- `Processor State Flags=1` fue idéntico en los 24 LP durante cuatro muestras; sin definición para esta plataforma no se interpreta como ausencia de throttling. [windows_counters_idle_3s.csv](logs/windows_counters_idle_3s.csv).
- `powercfg /energy /duration 3` completó y guardó el informe HTML; indicó 14 errores, 3 advertencias y 58 informativos. El informe es de eficacia energética, no de límites térmicos/PL. [salida](logs/powercfg_energy_command.txt), [HTML](logs/powercfg_energy.html).

## Plan y configuración (d)

`powercfg /getactivescheme`: Alto rendimiento (`8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c`). `powercfg /q SCHEME_CURRENT SUB_PROCESSOR`: estado mínimo AC 100 %, DC 5 %; máximo AC/DC 100 %; umbral de sobreutilización de parada de P-core AC/DC 60 %. WMI confirma el plan y esos tres índices. Todo es lectura; no se cambió el plan. Evidencia: [plan](logs/powercfg_active.txt), [valores completos](logs/powercfg_processor_settings.txt), [WMI](logs/wmi_power_plan_settings.json).

## Bloqueos y acciones legítimas del propietario

| Celdas | Bloqueo preciso | Acción que podría resolverlo; impacto/condición |
|---|---|---|
| A1, B1 (EMON) | 39/300 grupos de frecuencia y 30/300 del mapa de eventos se descartan; faltan timestamps de grupo, valores acumulativos y ancho de contador. No se prueban huecos, rollover ni ≥95 % de cobertura temporal. | Usar una versión/herramienta Intel compatible que entregue snapshots con timestamps/cobertura por core cada 10 ms, o un lector APERF/MPERF/ciclos-referencia soportado por el propietario. Mantener Secure Boot, HVCI y firma habilitados; si el soporte exige desactivarlos o un driver no firmado, esa vía sigue bloqueada y no se debe hacer bajo este protocolo. |
| A2 (Windows freq) | `Processor Frequency` es 2100 MHz y `% Maximum Frequency` 100 % en esta captura; el contador de utilidad no es un cociente APERF/MPERF. `Get-Counter` tiene mínimo 1 s. | El propietario puede implementar/usar una fuente documentada de APERF/MPERF o ciclos efectivos/referencia con polling 10 ms y timestamps; Perflib actual solo queda como diagnóstico. |
| A3 (ETW/PMC freq/events) | WPR solo lista PMC de muestreo, incluidas fuentes genéricas `LLCMisses`/`LLCReference`; no hay fuente de conteo separado L2/LLC con denominadores adecuados, ni lectura APERF por core. `xperf.exe` no está disponible en PATH. | El propietario puede instalar/usar Windows Performance Toolkit si necesita `xperf`, pero eso por sí solo no resuelve la semántica/cobertura; debe producir y validar un perfil de conteo con los eventos del mapa y marcas temporales. No requiere cambiar seguridad si usa herramientas firmadas/compatibles. |
| A4, B2/B4, A6, B6, D1–D3, D5, D6 | WMI para A/B, `powercfg` para A/B/c y herramientas de política para PMU no exponen el dato requerido en esa interfaz. | No existe ajuste de política que habilite esos datos en esas interfaces. El propietario debe elegir la interfaz ya disponible que sí los expone (EMON para PMU, `powercfg` para plan). |
| A5, B5, C5 | Intel DTT/XTU no está instalado; tampoco se acreditó que exponga frecuencia/eventos por P-core/PL+termales requeridos. | Si quiere evaluar esta vía, el propietario debe confirmar primero en documentación del fabricante que una versión firmada para este modelo entrega los campos; instalarla solo si mantiene Secure Boot/HVCI/firma. XTU no se considera solución para PMU hasta demostrar esos eventos. |
| C1, C2/C3/C4/C6 | Faltan límites PL1/PL2, ventanas, CPU package/core temperature, límite térmico mapeado y flags decodificados. El ACPI WMI es una zona genérica, no CPU. | Consultar una interfaz de sensores/MSR oficial de placa/CPU ya instalada; o el propietario puede instalar una herramienta firmada y compatible con HVCI que documente esos campos. Si para leer MSR exige cambiar políticas de seguridad, queda bloqueado, sin excepciones aquí. |
| D0, D4 | Sin bloqueo: `powercfg` y WMI leen plan/valores; D0 cualifica el objetivo (d). | Ninguna acción adicional; conservar el estado observado Alto rendimiento y los valores registrados. |

## Reproducción y archivos

- Captura de frecuencia/caché: `powershell -ExecutionPolicy Bypass -File T0P\telemetry\scripts\capture_emon_idle.ps1 -CaptureSeconds 3 -SampleMilliseconds 10`.
- Captura del mapa demanda-only: `powershell -ExecutionPolicy Bypass -File T0P\telemetry\scripts\capture_emon_event_map.ps1 -CaptureSeconds 3 -SampleMilliseconds 10`.
- Windows/privilegios/WMI/plan/ETW: `powershell -ExecutionPolicy Bypass -File T0P\telemetry\scripts\capture_windows_counters.ps1 -CaptureSeconds 3` y `capture_readonly_evidence.ps1`.
- Crudos: carpeta [logs](logs/), incluidos EMON, `Get-Counter`, `whoami /groups`, `sc query`, `driverquery`, WPR/PMC, WMI y `powercfg`.
- Ningún archivo se creó fuera de `T0P\telemetry\`. No se instaló/cargó driver, no se cambió política, seguridad, energía, PL1/PL2 ni otra configuración; no se ejecutó OMEGA, ggml ni Torch.


## Integridad de artefactos

SHA256SUMS_telemetry.txt incluye los 31 archivos de este directorio salvo el propio manifiesto; los 31/31 hashes se recalcularon y coincidieron (100 %).
