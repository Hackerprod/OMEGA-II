# Harness de cualificación OMEGA Q4

`build.cmd` compila el launcher y tres ejecutables de QA desde las fuentes congeladas de `common`, `kernel`, `ref_ggml` y `ref_torch`. Todos los `.obj`, temporales del compilador, logs y binarios quedan dentro de esta carpeta.

```cmd
build.cmd
qualify.exe --dry-run
qualify.exe --self-test
qualify.exe --timer-smoke
```

El dry-run ejecuta una sola campaña de controles sin cronómetro: QA congelada de ggml y LibTorch, y QA S-12/S-17 del kernel. Captura cada salida bajo `logs\dryrun_*.log` y crea `logs\dryrun_summary.json`; los campos de duración y métricas de rendimiento permanecen nulos. Cualquier código distinto de cero produce fallo terminal.

`--self-test` ejecuta únicamente datos inyectados deterministas marcados `SYNTHETIC`, incluida estadística, orden reproducible, selector y gates, además de negativas. `--timer-smoke` usa solo un bucle entero trivial; sus medidas QPC se etiquetan `DUMMY` y no entran en informes oficiales.

`--official` requiere `AUTHORIZED.json` con los hashes SHA-256 del ejecutable y `QUALIFY_PROTOCOL.md`. No crear ni simular ese archivo. El recolector de mediciones oficiales todavía no está integrado; por eso el modo falla cerrado incluso con hashes coincidentes. No se debe tratar la estadística sintética como adquisición oficial.

El dry-run tambien aplica los controles de S-17 Q1/Q2 desde esta carpeta: valida bit a bit los planos FP32/INT32 y la copia FP16 de activaciones, rechaza mutaciones independientes de un byte y comprueba tamanos contractuales y cobertura de las 212 consultas PAGE de OMEGA.

El estado de las páginas grandes se toma de los controles reales de cada adaptador. No hay fallback a páginas pequeñas. Consulta [QUALIFY_PROTOCOL.md](QUALIFY_PROTOCOL.md) para el protocolo, las reglas de selección y las ambigüedades abiertas.
