# Harness R10

R10 mide operadores de referencia y el kernel OMEGA en workers fijados a los ocho P-cores. La compilación y los resultados R10 se escriben en `obj_r10\` y `logs_r10\`.

## Ejecutar

- `cmd /c T0P\harness\build_r10.cmd` compila y ejecuta el smoke N=3, con EMON y derivación de telemetría.
- `powershell -NoProfile -ExecutionPolicy Bypass -File T0P\harness\run_smoke_r10.ps1 -SampleCount 31` ejecuta N=31 con cinco warmups por proveedor en la celda smoke O=1024, m=1.
- `powershell -NoProfile -ExecutionPolicy Bypass -File T0P\harness\run_geometry_smoke_r10.ps1` prueba N=3 para O={912,1024,58256}, m={1,16}, con OMEGA, ggml flat/repack y Torch mm/matmul/linear/mv.
- `powershell -NoProfile -ExecutionPolicy Bypass -File T0P\harness\run_official_r10.ps1` exige `T0P\collector\AUTHORIZED.json` S-18 A6 válida antes de iniciar EMON. Si se autoriza, ejecuta N=31, warmup=5 y la matriz de los 16 tamaños O con m={1,4,8,16}.

El modo directo `harness_r10.exe --official` solo valida la autorización. La variante de adquisición con argumentos solo se habilita desde el runner que mantiene la captura conjunta EMON/QPC. En este paquete no hay autorización y no se inició una sesión oficial.

## Salidas

Cada captura de smoke contiene `raw.jsonl`, `summary.jsonl`, `calibration.json`, plan y reloj QPC/EMON. `postprocess_r10.ps1` valida ambos JSONL antes y después de derivar telemetría y conserva resultados dentro de la captura en `logs_r10\`. Cada captura geométrica contiene `geometry_raw.jsonl`, `geometry_summary.jsonl` y una calibración por celda. La geometría usa el mismo generador Q4; O=1024 se contrasta exactamente con el fixture común. El validador estricto corre antes y después de la derivación.

`telemetry_r10.py --self-test` prueba rechazos deterministas. `validate_r10.py --self-test` prueba JSON estricto. `validate_r10.py --raw <archivo>` y `--summary <archivo>` validan los esquemas descritos en [HARNESS_SPEC_r10.md](HARNESS_SPEC_r10.md).
