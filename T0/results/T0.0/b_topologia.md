# T0.0-b — Topología

Base: Roadmap §5.1; `T0.md` §3, T0.0-b. El helper consulta `GetLogicalProcessorInformationEx(RelationProcessorCore)` y `RelationCache`; enlaza `EfficiencyClass` con CPUID leaf 1Ah leído con afinidad temporal al logical processor observado. No se presume qué número de CPU lógica corresponde a P/E.

Resultado: Processor Group 0; 16 registros de núcleo físico y 24 logical processors. En el cruce observado, `EfficiencyClass=1` coincide en los ocho núcleos con CPUID leaf 1Ah type 64 (Intel Core/P) y dos LP por núcleo; `EfficiencyClass=0` coincide en ocho núcleos con type 32 (Intel Atom/E) y un LP por núcleo.

- LP 0–15 aparecen como ocho parejas SMT P-core, cada pareja con su propio grupo L2 de 2 MiB.
- LP 16–23 aparecen como ocho E-cores, agrupados en dos conjuntos L2 de 4 MiB, cuatro E-cores por grupo.
- La relación LLC unificada observada comparte 30 MiB entre los 24 logical processors.
- El CSV tiene una fila por LP y columnas para Processor Group, LP, clave de núcleo de API, `EfficiencyClass`, tipo CPUID leaf 1Ah, tipo P/E, grupo L2, hermanos SMT y LP que comparten L2.

Comando exacto: `C:\Projects\OMEGA\Ejercise\T0\build\out\t0_probe.exe --topology` (`T0\logs\topology_api.command.txt`); stdout crudo: `T0\logs\topology_api.stdout.txt`; tabla: `T0\results\T0.0\b_topologia.csv`.
