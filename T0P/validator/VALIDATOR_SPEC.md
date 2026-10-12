# Validador S-20 único (D10, R2)

`T0P/validator/s20_validator.py` es la implementación de referencia para el EMON CLI crudo. No consume etiquetas de estado de las entradas, no inventa QPF ni rellena canales ausentes. Rechaza claves JSON repetidas/desconocidas, columnas CSV duplicadas/desconocidas o filas mal formadas, grupos efectivos múltiples, eventos faltantes/duplicados/no configurados, marcas fuera de orden, relojes inválidos y conteos que no sean enteros. El mapa efectivo del host procede de EMON `-M`: Core físico `0,4,…,28`, LP de worker `0,2,…,14` y hermano SMT inmediato `LP+1`.

## Entradas y ejecución

Para S-20, proporcionar captura EMON, mapa QPC/UTC, marcas (`t_pad_ini`, `t_ini`, `t_fin`, `t_pad_fin` por muestra) y configuración efectiva. El mapa de eventos, EDP y SHA256 de EDP, QPF leída/registrada, `f_TSC` cualificada con su QPF de cualificación, zona horaria y LP↔core P son datos obligatorios. QPF no tiene valor por defecto.

```powershell
python -B T0P/validator/s20_validator.py validate `
  --emon-raw T0P/telemetry/logs/conditioning_joint_raw.txt `
  --clock-map T0P/telemetry/logs/conditioning_qpc_utc_map.csv `
  --block-marks T0P/validator/artifacts_r6/qualification_inputs/conditioning_joint/attempt-002/conditioning_joint_marks_r6.csv `
  --config T0P/validator/artifacts_r6/qualification_inputs/conditioning_joint/attempt-002/effective_config.json `
  --mode temporal
```

La salida JSON incluye un estado y motivos tipados por muestra, core/canal y ventana. El código de salida es `0` para `VALID` o `PASS`; es `1` para una validación completada que no pasa (incluido `NOT_ESTIMABLE`), y `2` para entradas/configuración inválidas. Los ejemplos de integración están en `adapter_example.py`; se debe usar un directorio de intento nuevo por ejecución para conservar la procedencia D13.

API Python del adaptador (snapshot write-once de las cuatro entradas principales y EDP, validación y resultado derivado con lineage):

```python
import sys
sys.path.insert(0, r"T0P\validator")
from adapter_example import validate_attempt

run = validate_attempt(
    emon_raw=r"T0P\evict\capture_blocks_r2\emon_stdout.txt",
    clock_map=r"T0P\evict\capture_blocks_r2\clock_map.csv",
    block_marks=r"T0P\evict\capture_blocks_r2\eviction_windows.csv",
    effective_config=r"T0P\validator\artifacts_r6\qualification_inputs\capture_blocks_r2\attempt-002\effective_config.json",
    attempt_directory=r"T0P\validator\artifacts_r6\new_session\attempt-001",
    session_id="new_session", attempt_id="attempt-001", mode="temporal",
)
assert run["result"]["state"] == "VALID"
```

El perfil `full` activa las comparaciones emparejadas: A/B/C por core y réplica; dispersión de los ocho P por condición/réplica ≤5 %; deriva de cada réplica contra la mediana de al menos dos réplicas ≤3 %. Faltan pares o cores ⇒ `CONTROL_PEERS_MISSING`. En cualquier perfil, cada ventana se contrasta contra la frecuencia activa media del bloque con excursión máxima ±3 %.

## Reglas temporales R2

- Ventana nominal: 50 ms. Su duración real se deriva del elapsed-TSC EMON dividido por el `f_TSC` configurado, no del REF_TSC por LP. Duración real >100 ms invalida esa ventana. Una sola EDP/grupo y `multiplexing=false`; cualquier período perdido o reinicio distinto de cero invalida la entrada.
- El bloque R2 conserva al menos 200 ms de padding de la misma condición antes y después de la medición nominal.
- El EMON de la ventana `k` aporta evidencia solo si `[s_k-U_k,e_k+U_k]` está dentro del bloque acolchado de la misma condición. Si cruza a otra condición se marca `EXCLUDED/CROSS_CONDITION` y no contribuye a la cobertura.
- Cada ventana elegible se contrae individualmente a `[s_k+U_k,e_k-U_k]`, se intersecta con la medición `[a,b]` y luego se une con las otras ventanas. `C_low = medida(unión)/(b-a)`. Nunca se contrae una unión ya fusionada. `U≤1 ms`, cobertura `≥95 %`, hueco máximo `≤20 ms` incluidos ambos extremos y al menos 20 ventanas válidas por core/canal.
- El mapa QPC/UTC calcula QPF por regresión de puntos explícitos y acota la incertidumbre con residuo, bracket y redondeo civil. La discrepancia entre `f_TSC` cualificada y `f_TSC` derivada del mapa debe ser `≤0,001`; no se aplica el margen de ±3 % usado para frecuencia activa.
- `f_activo = f_TSC × ΣΔTHREAD / ΣΔREF_TSC`; primero se suman los contadores del mismo LP/alcance y luego se toma el cociente. Los hermanos SMT se mantienen como LP distintos; no se suman ciclos duplicados.
- Fracciones L2/LLC suman numeradores y denominadores del mismo LP y las mismas ventanas. Denominador cero produce `NOT_ESTIMABLE`, no cero.
- Energía de paquete usa `0x611` con semántica configurada `INTERVAL_DELTA`, sin reset ni lectura estática de acumulado. También se admite `SNAPSHOT32`: delta modular `(después−antes) & 0xffffffff`. `P_pkg = u_E × ΔE / Δt`; el elapsed de `Δt` proviene del intervalo EMON. Es una magnitud de paquete: no se suman entre cores las filas que la presentan.
- Térmica: para `0x19C`, bit 31 valida la lectura digital; margen TCC menor de 5 °C invalida. Estado actual, flags históricos thermal/PROCHOT y flags de límite de potencia se presentan por separado. PROCHOT/thermal actual y logs históricos no limpiados invalidan; una indicación de límite de potencia se informa como `power_limit_observed` y no se convierte por sí sola en prueba de frecuencia ni en fallo térmico. `0x1B1` no hereda la semántica de validez del bit 31 de `0x19C`. Una frecuencia estable no demuestra ausencia de limitación.

## Perfil R1 — D2-INST_RETIRED

El mismo módulo ofrece `validate-r1`. Requiere un grupo único `INST_RETIRED.ANY`, colección de aplicación hasta salida (`-t 0`), sin multiplexación, pérdidas ni resets; verifica que el mapa QPC/UTC cubra de forma conservadora los ocho bucles y que las ventanas de EMON sean contiguas. Del reporte del helper valida exactamente LP 0,2,…,14, `N_loop=200000000000`, resultado del bucle, afinidad, región de al menos 20 s, QPF y `I0=3N+4=600000000004`. Suma los conteos EMON crudos de todas las muestras del único intervalo; no normaliza ni resta actividad de fondo.

```powershell
python -B T0P/validator/s20_validator.py validate-r1 `
  --emon-raw <intento>\emon_raw_r6.txt `
  --clock-map <intento>\helper_clock_map_r6.csv `
  --helper-report <intento>\helper_report_r6.csv `
  --config <intento>\r1_effective_config_r6.json
```

La ejecución EMON real está documentada, pero no se inició en esta tarea: contradice la prohibición explícita de escribir MSR. La prueba `--dry-run -m` de este host muestra solo `Event Set 0` con `INST_RETIRED.ANY` (Fixed Counter 0, `0x309`); no programa el PMU. Sintaxis que el `emon_help.txt` local documenta para una colección continua ligada al proceso: `emon -t 0 -C "INST_RETIRED.ANY" -m -c -u -f <emon_raw_nuevo> <inst_cli_helper_r6.exe> <helper_report_nuevo> <helper_clock_map_nuevo>`. `-f` crea un archivo nuevo, `-F` añade; usar siempre directorio D13 fresco. No se usa `-l`: `-t 0` con aplicación llega hasta su salida y desactiva el comportamiento de límite de loops/espera documentado. La lista tiene un grupo y un evento, sin multiplexación. Ejecutar el comando real requiere retirar primero el bloqueo MSR.

## Puntos exactos de integración pendientes

No se modificaron harness ni collector. La integración externa debe llamar al validador en:

1. `T0P/harness/telemetry_r11.py::_fit_clock_map()` — sustituir su ajuste/validación local por el mapa y QPF explícitos del validador.
2. `T0P/harness/telemetry_r11.py::_read_emon()` — conservar la captura cruda y validar el parser/event set.
3. `T0P/harness/telemetry_r11.py::_derive_block()` — consumir ventanas/canales ya validados y sus estados por core.
4. `T0P/harness/telemetry_r11.py::_check()` — hacer gating por canal/core/ventana y controles `full`.
5. `T0P/collector/qualify_r11.py::telemetry_valid()` — llamar el mismo API/CLI; retirar la decisión separada de “algún sensor visto”.

Los contratos de entrada efectivos están en `effective_config.schema.json`; Python aplica además las invariantes dependientes del perfil.

## D13: crudos inmutables

`artifact_store.py` ofrece `SessionAttemptStore`: `create` rechaza si la ruta de intento existe; `write_raw`/`ingest_raw` hacen creación exclusiva (`xb`), flush/fsync y SHA256 sidecar; `register_external_raw` registra la salida nueva de EMON (`-f` también crea nuevo); `verify_raw` recalcula el hash; `write_derived` crea otro archivo con hash propio y `.lineage.json` que enlaza hashes fuente. No hay `os.replace`, append de crudos ni reapertura truncante. Toda repetición de captura/validación necesita una ruta de intento nueva.

Pruebas negativas: escribir otra vez el mismo crudo produce `ArtifactError: artifact already exists; write-once refusal`; recrear una ruta de intento existente produce `ArtifactError: attempt directory already exists; refusing reuse`. La prueba de ingesta externa confirma que el mismo rechazo ocurre al copiar una fuente preexistente.
