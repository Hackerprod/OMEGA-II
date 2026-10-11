# T0.0-g — Herramientas de contadores candidatas

Base: Roadmap §§2.2 y 5.1; `T0.md` §3, T0.0-g. Enumeración de nombres/eventos/versiones únicamente: no hubo colección, workload, benchmark ni integración de contadores. La presencia en una lista no se declara `VALID`.

## Herramientas

- VTune se ejecutó desde su ruta absoluta: `C:\Program Files (x86)\Intel\oneAPI\vtune\2026.4\bin64\vtune.exe`; `-version` devuelve `2026.4.0 (build 632893)`. `-help collect memory-access` reconoce Memory Access.
- `sep.exe -version`: SEP user mode 5.61; SEP driver 5.61 (public), PAX 1.0. `sep -platform-info` reporta driver 5.61.22 y PAX 1.0.2; discrepancia preservada.
- SEP enumera PMU disponibles: p-core, e-core, cbo, ncu, imc y power; plataforma: Raptor Lake-DT, model 0xB7; el literal EMON database `alderlake` se registra tal como lo reporta la herramienta, sin inferir una correspondencia de base de datos.
- PCM no se encontró por resolución de comandos, `where.exe`, búsqueda de ejecutables PCM en `Program Files`, `Program Files (x86)`, `ProgramData` ni registro Uninstall consultado. No se instaló ni desinstaló nada.

## Eventos enumerados

| Uso candidato | Nombres / descripción literal | Observación |
|---|---|---|
| DRAM agregado IMC, SEP | `UNC_MC0_RDCAS_COUNT_FREERUN`, `UNC_MC1_RDCAS_COUNT_FREERUN`: requests de lectura de 64 B a DRAM; `UNC_MC0_WRCAS_COUNT_FREERUN`, `UNC_MC1_WRCAS_COUNT_FREERUN`: requests de escritura de 64 B; la descripción menciona coalescencia de escrituras de la misma cache line. | Candidatos libres/freerun enumerados en el listado SEP de la plataforma; no colectados ni validados. |
| DRAM agregado, VTune Memory Access atype | `UNC_M_CAS_COUNT_RD` = read CAS command sent to DRAM; `UNC_M_CAS_COUNT_WR` = write CAS command sent to DRAM. | Candidato; el texto enumerado no fija conversión CAS→bytes, no se inventa. |
| L2 P-core | `L2_LINES_IN.ALL` = L2 cache lines filling L2; `L2_REQUEST.MISS` = read requests with true-miss in L2 (el catálogo declara alias `L2_RQSTS.MISS`); `L2_REQUEST.ALL` = accesses. | Nombres y descripciones aparecen en `sep -el p-core -desc`; no se midió. |
| LLC P-core | `LONGEST_LAT_CACHE.MISS` y `LONGEST_LAT_CACHE.REFERENCE`, con descripciones de requests core-originated que fallan/consultan LLC. | Candidatos enumerados, no validados. |

Comandos exactos y crudos: `vtune_version.command.txt`/`.stdout.txt` (`vtune.exe -version`), `vtune_memory_access_analysis_help.command.txt`/`.stdout.txt`, `vtune_runsa_event_enumeration.command.txt`/`.stdout.txt`, `sep_version.command.txt`/`.stdout.txt`, `sep_pmu_types_available.command.txt`/`.stdout.txt`, `sep_pcore_events.command.txt`/`.stdout.txt`, `sep_platform_events.command.txt`/`.stdout.txt` y `sep_memory_access_atype.command.txt`/`.stdout.txt`, todos en `T0\logs\`. Los comandos de enumeración no iniciaron colectas. En las salidas de lista no se obtuvo un raw event code independiente para cada símbolo. Si la calificación posterior no valida eventos finos, Roadmap §5.1 da como vía alternativa: tiempo de pared, tráfico DRAM agregado, working set, expulsión controlada y curva latencia vs bytes residentes; esa vía queda escrita, no ejecutada.
