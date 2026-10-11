# INCIDENT — preflight crudo del segmento 2 sobrescrito

- Incidente: `PREFLIGHT_SEGMENT_2_RAW_OVERWRITTEN`.
- Estado: **NO RECUPERABLE**. El stdout crudo original del preflight del segmento 2 ya no está en disco; solo sobrevive su registro de comando/hash en `logs/COMMANDS.json` y los valores asentados en el informe de sesión. No se reconstruye ni se sustituye por el preflight de otro segmento.
- Entrada preservada en `COMMANDS.json`: `phase1_resume_official_session_preflight`, comenzó `2026-10-11T02:06:05.150470+00:00`, terminó `2026-10-11T02:06:25.156639+00:00`, exit code 0; hash SHA-256 del stdout original `E2FB5B40107DC86FA3B4FC1E215B17B40DA21B9A81B42DD0812B1509FE844A6A`.
- El archivo compartido `logs/phase1_resume_official_session_preflight.stdout.txt` fue sobrescrito por el preflight del segmento 3, registrado en el mismo `COMMANDS.json` con SHA-256 `92F4E2DF797F442F3EC9949381D65B58E9B6DC6403FE8835ECA9923D0FE525F`.
- Valores del segmento 2 conservados en el informe contemporáneo: media CPU 5.545 %, pico 11 %, 175 procesos. No se recuperan de una fuente cruda.
- La pérdida afecta la evidencia cruda ambiental del preflight, no los datos de rendimiento. La carga de fondo fue observacional, no un gate; ninguna muestra fue seleccionada, invalidada ni filtrada en función de esos valores. La entrada manifestada tuvo exit code 0, pero el stdout bloqueante no es verificable hoy.
- Desde esta corrección, el runner nombra logs de preflight, `phase1_execution_status` y `phase1_session_summary` con identificador único por segmento, evitando reutilizar esos destinos.
