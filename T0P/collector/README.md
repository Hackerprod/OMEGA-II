# Colector T0P

`collector.exe --smoke` inicia la QA real de los proveedores congelados (`qualify.exe --dry-run`), la QA de expulsión, la captura EMON CLI conjunta con acondicionamiento DUMMY de 180 s, las pruebas negativas de telemetría, el gate consumidor de páginas desde `PAGE_EXPECTED_COLLECTOR.json`, el plan/digest y la prueba de corte oficial. Todos los logs de la ejecución llevan `smoke_` y quedan en `logs\`; ninguna cifra de rendimiento se emite ni se usa para decidir.

```cmd
build.cmd
collector.exe --self-test
collector.exe --plan-check plan.json
collector.exe --smoke
collector.exe --official
```

`--official` requiere `AUTHORIZED.json` y siempre falla cerrado en este build. El smoke no crea ese archivo. Los cuatro límites numéricos se cubren en el auto-test del motor de decisión; la QA de proveedores es sin cronometraje. Para los criterios, fórmulas y alcance pendiente, véase [COLLECTOR_SPEC.md](COLLECTOR_SPEC.md).

Las salidas SMOKE no son resultados oficiales. La emisión de una competencia con muestras N=3 y telemetría de márgenes por proveedor queda supeditada a completar la integración temporal descrita en la especificación.
