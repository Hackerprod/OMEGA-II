# Procedimiento de revisión y ejecución R11

## Estado actual

Todavía no se debe ejecutar una sesión oficial: no existe `T0P\collector\AUTHORIZED.json` real y el certificado de cualificación conjunta del lector EMON/TSC que exige S-20 está pendiente. El runner debe cerrar antes de medir en ambos casos. La fixture A6 solo se guarda con nombre de prueba en `test_r11`.

## Comandos de revisión

Desde `C:\Projects\OMEGA\Ejercise`, en PowerShell:

```powershell
cmd /c call T0P\harness\build_r11.cmd
python T0P\collector\official_run_r11.py --plan-check
python T0P\collector\official_run_r11.py --test-orchestrator
python T0P\collector\official_run_r11.py --auth-self-test
python T0P\collector\official_run_r11.py --dry-accept-fixture T0P\collector\test_r11\authorized_fixture_valid.json
python T0P\collector\official_run_r11.py --smoke-real
```

El smoke es reducido, aparece como `SMOKE_ONLY` y arranca EMON durante la adquisición. Comprobar en su carpeta correspondiente de `T0P\collector\logs_r11` que haya 360 filas crudas, salida A=B=C, `TIMER_OVERHEAD status=PASS`, `CLOCK_QUALIFICATION`, `EMON=COMPLETE` y derivación `SMOKE_ONLY numeric_metrics_used=false`.

## Preparación de juez para sesión oficial

1. Cerrar Chrome y aplicaciones que puedan generar carga de CPU. Conectar el equipo a alimentación y dejar el perfil de energía ya aprobado por el protocolo; no modificar límites de potencia ni umbrales.
2. Verificar el plan, page gate, hashes de entrega y evidencia. Confirmar que el certificado S-20 está presente y válido y que el preflight no devuelve HOLD.
3. Revisar la fixture A6 en `test_r11` como prueba del parser. El juez coloca la autorización A6 real en `T0P\collector\AUTHORIZED.json` solo después de la revisión correspondiente.
4. Ejecutar `python T0P\collector\official_run_r11.py --official` solo cuando A6 y el certificado S-20 existan. En el estado entregado, esta orden no inicia mediciones: sin A6 devuelve `HOLD_AUTHORIZATION`; con A6 correcta pero sin certificado lector, devuelve `HOLD_INSTRUMENTATION`. La secuencia de nueve fases, cada corte y la política de un único reintento técnico están probados en `--test-orchestrator`/`--dry-run-synthetic`; no se ejecutó una secuencia oficial.
5. Si se desbloquea el runner oficial en una revisión posterior, comprobar `RESULT_r11.json`, capturas en `T0P\harness\logs_r11` y espejo en `T0P\collector\logs_r11`, pie EMON, finalización de barrido y controles finales de expulsión/LLC. Regenerar y verificar el manifiesto de evidencia.

## Comprobaciones posteriores

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File T0P\harness\hashes_r11.ps1 -Verify
powershell -NoProfile -ExecutionPolicy Bypass -File T0P\collector\hashes_r11.ps1 -Verify
powershell -NoProfile -ExecutionPolicy Bypass -File T0P\collector\hashes_evidence_r11.ps1 -Verify
```

No borrar `logs_r11` ni los crudos antes de verificar. Los temporales de compilación viven en `obj_r11\tmp`; después de revisar la compilación se comprueba que ese directorio esté vacío. Los logs y objetos publicados se conservan y se incluyen en su manifiesto respectivo.
