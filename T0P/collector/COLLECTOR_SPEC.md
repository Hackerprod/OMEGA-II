# Especificación del colector T0P

## Lo que integra este build

- Preregistro canónico S-19, SHA-256, validación estricta del plan, selección estadística y máquina de estados con `--self-test`.
- Proveedores congelados llamados mediante `T0P\qualify\qualify.exe --dry-run`: kernel Q4, ggml flat/repack y Torch `_out`. Estos ejecutables validan rutas numéricas, dispatch y páginas; la ruta dry-run no cronometra.
- Contrato de proveedor comprobado contra `SHA256SUMS_qualify.txt`; `dryrun_summary.json` debe tener 61 registros, estado PASS, `timed=false` y métricas de rendimiento nulas. Se guarda SHA-256 del contrato en `logs\smoke_*_provider_contract.txt`.
- Gate físico de páginas sobre registros `PAGE` reales: `dwPageSize` de Windows, fórmula S-16, `QueryWorkingSetEx`, `Valid=1` y `LargePage=1` en todas las páginas informadas. Contrato: `PAGE_EXPECTED_COLLECTOR.json`.
- QA de topología/afinidad/páginas/expulsión mediante `T0P\evict\evict_qa.exe`; QA negativa de telemetría mediante `telemetry_reader_r3.exe`.
- Smoke: proveedores y expulsión bajo EMON CLI conjunto, acondicionamiento DUMMY 180 s con mapa QPC, verificación de plan/digest y corte oficial. Los datos llevan etiqueta SMOKE y no son rendimiento oficial.
- `--official` rechaza si no existe `AUTHORIZED.json` (exit 20). No inicia mediciones oficiales.

La decisión numérica se aplica antes de medir una sesión autorizada: `G_ggml ≥ 0,90`, `G_Torch ≥ 1,20`, `H-KQ ≥ 33,1 MAC/ns/worker`, `D′ ≥ 2,00`; plateau no cualificado da `INVALID_IMPLEMENTATION`. Los cuatro casos de gate y los casos HOLD/cierre se ejercitan en `--self-test`. `PL1`, `PL2`, ventanas de potencia y `0x64F` se registran como `NOT_READABLE_WITH_AVAILABLE_INTERFACE` y no bloquean el colector.

## Telemetría y límites de esta integración

Lector oficial único: `emon -collect-edp edp_file=T0P\telemetry\reader\emon_joint_qualification.edp`. La API EMON no es oficial. La cualificación vigente de referencia aporta D1/D2 REF_TSC/D3 PASS y el método de instrucciones `(a)` con etiqueta `INST_RETIRED_METHOD=a_PENDING_SOL_RATIFICATION`; ambos métodos deben conservarse en los resultados de sesión.

Los criterios por ventana son cobertura ≥95 %, hueco superior ≤20 ms, ≥20 ventanas, ventana ≤100 ms, U≤1 ms y márgenes de la misma condición ≥0,2 s antes y después. El smoke actual captura EMON y ejecuta el acondicionamiento, pero todavía no enlaza marcas de inicio/fin por cada proveedor para emitir análisis acolchado S-20 A ni deriva potencia/frecuencia/flags térmicos por bloque. Tampoco ejecuta competencias N=3, `P_instr` plateau, `BW_DRAM` ni H-KQ cronometrados. La QA de expulsión existente está cualificada como `EVICTION_QUALIFIED`; el smoke vuelve a ejecutar su QA local, sin atribuir su salida a ventanas EMON.

Por tanto, este build no declara `PREFLIGHT=PASS` ni habilita una campaña oficial. Trabajo pendiente de la parte 1: proveedores cronometrados (incluidos `P_instr`, `BW_DRAM` y H-KQ), competencia N=3, integración de bloques medidos y parser/análisis estricto de ventanas acolchadas y derivación S-20 A/B/C/E. La parte 2 continúa siendo residencia A/B/C, barrido W, derivador final y parser estricto integrado de `AUTHORIZED.json`.

## Contratos y ejecución

`build.cmd` compila y corre QA determinista, plan y corte cerrado. `collector.exe --smoke` genera evidencias nuevas `logs\smoke_*`. `SHA256SUMS_collector.txt` resume los archivos de entrega y evidencias generadas, excepto el propio manifiesto. No se deben inferir throughput ni calificación oficial de los logs SMOKE.
