# QUALIFICATION RESULTS r7 - R1 INST_RETIRED

## Metodo y comando ejecutado

Se ejecutaron cinco capturas independientes. EMON recolecto el helper como aplicacion; `-t 0` termino al salir del helper. Un grupo `INST_RETIRED.ANY`, sin multiplexacion ni intervalos intercalados. El helper nativo r6 ejecuto una vez `known_retired_loop(200000000000)` en paralelo en LP 0,2,...,14; I0=600000000004 por LP. No hubo reintentos ni cambios de N_loop.

Comando efectivo (rutas de salida relativas al intento):

```text
C:\Program Files (x86)\Intel\oneAPI\vtune\2026.4\bin64\emon.exe -t 0 -C INST_RETIRED.ANY -m -c -u -f emon_raw.txt C:\Projects\OMEGA\Ejercise\T0P\telemetry\reader\inst_cli_helper_r6.exe helper_report.csv helper_clock_map.csv
```

Los cinco procesos EMON finalizaron con codigo 0 y cada crudo contiene un intervalo continuo unico. Los reportes helper confirman ocho retornos iguales a N_loop, afinidad PASS y region >=20 s. Cada intento tiene archivos write-once y manifiesto SHA256 por archivo.

## Matriz R1

Cada celda muestra `I_EMON / error relativo / estado`, donde error = `|I_EMON-I0|/I0` y PASS exige <=1 %. El valor EMON por LP se extrae de la columna de ese worker, con la columna comun inicial desplazada.

| Captura | Region minima | Afinidad | Estado captura |
|---:|---:|---|---|
| 1 | 39.334 s | PASS | FAIL |
| 2 | 39.343 s | PASS | FAIL |
| 3 | 39.335 s | PASS | FAIL |
| 4 | 39.338 s | PASS | FAIL |
| 5 | 39.338 s | PASS | PASS |

| Captura | LP0 | LP2 | LP4 | LP6 | LP8 | LP10 | LP12 | LP14 |
|---:|---|---|---|---|---|---|---|---|
| 1 | 608100033836 / 1.3500% / FAIL | 601570251471 / 0.2617% / PASS | 600634164495 / 0.1057% / PASS | 600286078243 / 0.0477% / PASS | 601386080994 / 0.2310% / PASS | 600685693754 / 0.1143% / PASS | 600134344803 / 0.0224% / PASS | 600097065785 / 0.0162% / PASS |
| 2 | 607695339797 / 1.2826% / FAIL | 601861409917 / 0.3102% / PASS | 600858104168 / 0.1430% / PASS | 600364522092 / 0.0608% / PASS | 601771701093 / 0.2953% / PASS | 601066231370 / 0.1777% / PASS | 600275208887 / 0.0459% / PASS | 600179721903 / 0.0300% / PASS |
| 3 | 607634205931 / 1.2724% / FAIL | 602102844583 / 0.3505% / PASS | 601014679440 / 0.1691% / PASS | 600271251895 / 0.0452% / PASS | 601367940929 / 0.2280% / PASS | 600595730117 / 0.0993% / PASS | 600112874319 / 0.0188% / PASS | 600096620357 / 0.0161% / PASS |
| 4 | 608448767704 / 1.4081% / FAIL | 602694914790 / 0.4492% / PASS | 601407647146 / 0.2346% / PASS | 600869324032 / 0.1449% / PASS | 601192870099 / 0.1988% / PASS | 601277161444 / 0.2129% / PASS | 600163420636 / 0.0272% / PASS | 600139900488 / 0.0233% / PASS |
| 5 | 605177526003 / 0.8629% / PASS | 602312040378 / 0.3853% / PASS | 600480340402 / 0.0801% / PASS | 600194870466 / 0.0325% / PASS | 600721482877 / 0.1202% / PASS | 601278167312 / 0.2130% / PASS | 600117392567 / 0.0196% / PASS | 600112828878 / 0.0188% / PASS |

**Estado R1: HOLD_INSTRUMENTATION.** Pasan todas las ocho celdas en 1/5 capturas. P0 excede el margen en capturas 1-4; captura 5 cumple los ocho LP. No se infiere conteo cero ni se reintenta.

## Evidencia por intento

Cada `attempt-00N/` conserva `emon_raw.txt`, `helper_report.csv`, `helper_clock_map.csv`, stdout/stderr, `invocation.txt` y `attempt_manifest.sha256`.
