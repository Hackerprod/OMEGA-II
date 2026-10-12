# T0P harness: motor de adquisición

## ABI y flujo

`Provider` define `prepare(cell,worker)`, `run_once(worker,m)`, `validate()` y `bytes_regions()`. Cada worker conserva su propio proveedor y asignaciones durante toda la celda. `omega_q4` llama `omega_q4::run`; `ggml_flat` ejecuta el grafo de `ggml_mul_mat` enlazado con GGML congelado, `use_ref=false`; `torch_out` llama directamente a LibTorch `mm_out`, `matmul_out`, `linear_out` o `mv_out` según `CellSpec.variant`. Torch corre dentro del mismo proceso e hilo afinado; no hay IPC en la región cronometrada. El SMOKE ejercita `mm_out`; las otras rutas se compilan, pero no se ejecutan en esta QA.

El orden de una campaña deberá resolverse a partir de los eventos y `order_key` de `T0P/collector/plan.json`; esta versión solo expone `--smoke` y no interpreta el plan de campaña. OMEGA recibe el O del contrato y el m de la celda; GGML/Torch congelados limitan O a 1024. Preparación, validación numérica y gate de páginas preceden a cada medición.

## Adquisición

Ocho hilos persistentes fijan afinidad a grupo 0 y LP 0,2,…,14, verifican máscara y LP actual, hacen cinco llamadas de warmup, ejecutan margen de trabajo ≥0,2 s, barrera, RDTSCP con LFENCE antes/después de la llamada, y margen posterior ≥0,2 s. La latencia paralela es `max(e_i)-min(s_i)` convertida mediante Hz TSC calibrado por QPC; se contrasta con QPC de pared. No se suman los ocho workers. Los JSONL guardan marcas crudas.

La prueba es SMOKE reducida: R=1 y no valida mínimo oficial de 2,0 s, N=3 ni estadísticas de competencia. El gate oficial de duración queda cerrado. Las fórmulas previstas son mediana, IQR=`Q(.75)-Q(.25)` con interpolación lineal y MAD=`mediana(|x-mediana(x)|)`.

## Telemetría y límites

`run_smoke.ps1` captura con EMON CLI conjunto mientras corre el ejecutable, mantiene mapa QPC/FILETIME y llama al analizador `T0P/evict/analyze_padded_blocks.ps1`. Convierte las marcas QPC por worker a `eviction_windows.csv`; el analizador conserva ventanas crudas y deriva cobertura, hueco, número/tamaño de ventanas y proporciones LLC/L2. Las muestras se etiquetan `INVALID_TELEMETRY` deliberadamente: son SMOKE, con spans menores de 2 s y no bastan para aceptar cobertura oficial. La f activa y potencia no se derivan por el analizador existente, así que siguen como límite de esta integración.

Las cuatro puertas numéricas son finitud, representación exacta, dots enteros exactos y cota S-12; cualquier fallo impide medir. `QueryWorkingSetEx` exige que todos los buffers de cada proveedor sean large pages válidas. La plataforma solo permite `O=1024` para los fixtures compartidos y para los dos proveedores de referencia.

## QA

`build.cmd` compila MSVC, enlaza kernel, GGML y LibTorch, ejecuta el SMOKE N=3 más seis pruebas negativas, captura EMON y produce análisis por bloque. No inicia sesión oficial, no crea `AUTHORIZED.json` ni declara umbrales de rendimiento. R=1, O de fixture limitado a 1024, captura con ventanas insuficientes, falta de lectura f activa/potencia, ausencia de modo oficial N=31/R calibrado y falta de integración del plan serializado impiden usarlo para adquisición oficial. El SHA-256 cubre todos los archivos de esta carpeta excepto el manifiesto que se actualiza.
