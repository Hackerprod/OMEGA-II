# Especificación de adquisición del harness R10

## R10: duración EMON y proveedores físicos

El runner estima duración desde el plan validado y el perfil de calibración asociado al mismo SHA-256: `N × Σ(R_common / mediana(iteraciones/s por worker y proveedor))`, más 45 s de preparación; para SMOKE N=3 suma 30 s reservados a los proveedores auxiliares. Calcula `-l=ceil(estimación × 1,50 × 3600/220)` (margen del 50 %, superior al mínimo requerido del 20 %); 3600 grupos equivalen a 220 s según la captura R9. N=31 debe superar 3600 grupos y N grupos. Antes de EMON comprueba espacio: estimación por grupo a partir de la captura R9, máximo 100 MiB, y espacio libre de al menos tres veces la salida estimada más 1 GiB.

El runner escribe un EDP derivado en `logs_r10\<captura>` y conserva intacto el lector base. Comprueba al terminar el límite `-l`, el número de grupos, el pie temporal y el tamaño crudo. Si EMON termina antes de terminar el plan, mantiene el harness hasta terminar y conserva sus filas; registra el último QPC con EMON activo y marca explícitamente `INVALID_TELEMETRY` con `EMON_PROCESS_STOPPED_BEFORE_PLAN_END` en cada bloque posterior. Sin marca QPC confiable, invalida todos los bloques. Ejecuta como máximo un reintento global con plan, semilla y orden iguales. Un segundo intento inválido termina `HOLD_INSTRUMENTATION`, campaña `CLOSED_Q4_256_THIS_I7`, residencia `NOT_STARTED`.

El SMOKE N=3 agrega dos proveedores nativos, siempre fijados a LP `0,2,…,14`:

- `BW_DRAM`: ocho buffers de 256 MiB, large pages obligatorias verificadas con `QueryWorkingSetEx`, pre-toque antes de medir y lectura load-only secuencial. El bucle usa cuatro cargas AVX2 de 256 bits por grupo; no tiene stores dentro del área medida.
- `P_instr`: primitiva `VPDPBUSD`, U=8, cadenas `{1,2,4,8,10,12,14}`, 3 réplicas y al menos 2,05 s medidos por bloque. Registra QPC/TSC, worker, afinidad, contadores de trabajo y márgenes QPC de 200 ms.

Los registros auxiliares comparten `raw.jsonl` y `summary.jsonl` con los cuatro proveedores de competencia. Se derivan con la misma captura y validación S-20; SMOKE nunca aporta métricas a los umbrales. La única calificación desde estos archivos para SMOKE es `SMOKE_ONLY`, residencia `NOT_STARTED`.

## Alcance y orden

El harness usa ocho workers persistentes, uno por cada LP par P-core `0,2,4,6,8,10,12,14`, comprueba afinidad efectiva y fija buffers de operandos, salida y preparación a large pages. Los preparadores crean operandos Q4 con la semántica del fixture común y la entrada equivalente de referencia. El caso O=1024 debe ser idéntico al generador común.

Rutas medidas:

- OMEGA: kernel Q4 por O×512.
- ggml flat: `ggml_mul_mat`, pesos Q4_0 e entrada Q8_0.
- ggml repack: Q4_0x8 y Q8_0x4; para m=1 usa entrada Q8_0 plana.
- Torch: `mm_out`, `matmul_out`, `linear_out`, `mv_out`; salida Y queda en large pages. En m>1, mv ejecuta una llamada por fila de salida.

La cuadrícula completa usa `O={912,1024,1824,2728,3640,4552,5464,6368,7280,9104,10920,14560,21848,29128,43688,58256}` y `m={1,4,8,16}`. El smoke geométrico ejecutado selecciona O={912,1024,58256}, m={1,16} y N=3. Antes de calibrar se valida `T0P\collector\plan.json` con `collector.exe --plan-check`; se usa su SHA256 y los primeros eventos compatibles de cada ruta ordenan el despacho dentro de cada m. La calibración registra evento, índice, tasa por ruta/worker y el único `R_common` de cada unidad O,m.

## Calibración y muestras

El piloto de calibración mide 200 ms por worker y ruta. Se elige `R_common=ceil(max(tasa_iteraciones/s)×2,5 s)` de toda la unidad comparativa. Ese R se reutiliza para todos sus workers y rutas; la muestra de cada worker se comprueba ≥2,0 s. El objetivo de 2,5 s aplica a la ruta más rápida que fija el mínimo R. No se reajusta el resultado según el tiempo observado.

Los modos son `--smoke` (N=3) y `--n31` (N=31). Ambos ejecutan cinco llamadas de warmup, guardan crudos, comprueban QPC/TSC, afinidad, páginas y S-12 numérico. Para N muestras, cuantiles usan interpolación lineal con `h=(N−1)q`; se registran mediana, `IQR=q75−q25`, MAD sin reescalar, mínimo y máximo por proveedor/celda.

## Captura de telemetría

`run_smoke_r10.ps1` verifica el plan, estima `-l`, levanta EMON y registra correspondencia QPC/FILETIME durante el proceso. `postprocess_r10.ps1` valida los JSONL antes y después de derivar y mantiene los artefactos junto a la captura en `logs_r10\`. `telemetry_r10.py` combina grupos EMON completos con los intervalos worker. Por P-core deriva `f_TSC` de la sesión, cotejada con EMON a ±3 %; `f_activo=f_TSC·ΣTHREAD/ΣREF_TSC`, `a=ΣREF_TSC/(f_TSC·Δt)` y frecuencia de pared son diagnósticos. La energía de paquete usa deltas 0x611 con unidad 0x606 y Δt real. 0x19C actual, 0x1B1 actual/histórico y margen TCC se informan por separado; margen menor de 5 °C, flags térmicos activos, frecuencias fuera de ±3 %, canales ausentes u otra condición no estimable invalidan la muestra con motivos. Fracciones L2/LLC suman numeradores y denominadores del mismo alcance; denominador cero produce `NOT_ESTIMABLE`.

La derivación actualiza el estado por muestra a `VALID` o `INVALID_TELEMETRY` y adjunta diagnósticos al JSONL. El harness de geometría de N=3 no captura telemetría; su archivo `geometry_capture_status.txt` lo declara. El runner de captura SMOKE N=31 conecta el JSONL a EMON/QPC antes de derivarlo; no es una sesión oficial.

## Autorización S-18 A6

`--official` busca `T0P\collector\AUTHORIZED.json`. Sin archivo o con JSON inválido, esquema incorrecto o hash discrepante imprime rechazo explícito, devuelve 20 y no mide. El parser rechaza claves duplicadas, UTF-8 inválido, bytes finales, propiedades ausentes/extra, tipos incorrectos, hashes con formato incorrecto y hashes de plan/ejecutable/protocolo/manifiesto que no coinciden.

Esquema exacto R10:

```json
{
  "schema": "T0P_S18_A6_AUTHORIZED_V1",
  "campaign": "texto no vacío",
  "session_id": "texto no vacío",
  "issued_at_utc": "YYYY-MM-DDTHH:mm:ssZ",
  "judge": "texto no vacío",
  "sol_decision_id": "texto no vacío",
  "scope": "alcance no vacío",
  "master_seed": "4F4D454741543031",
  "plan_sha256": "64 hex minúsculas",
  "artifacts": {
    "executable_sha256": "64 hex minúsculas",
    "protocol_sha256": "64 hex minúsculas",
    "manifest_sha256": "64 hex minúsculas"
  },
  "usage_policy": {
    "session_count": 1,
    "technical_retry": "one_global_technical_retry_same_plan_seed_order"
  }
}
```

Se exige el conjunto exacto de once propiedades de nivel superior y los conjuntos exactos de tres/tres propiedades anidadas. `--official` solo comprueba autorización y sigue cerrado en R10: no existe runner de sesión oficial. No se creó `AUTHORIZED.json` ni se inició una sesión oficial; las pruebas cubren fallo cerrado por ausencia y rechazo de fixtures JSON inválidos.

## JSONL estricto

`validate_r10.py` define la frontera de lectura del derivador. Cada línea debe ser un único objeto JSON UTF-8; rechaza líneas vacías, claves repetidas, NaN/Infinity, overflow a infinito, datos finales, campos ausentes y extras. Enteros, booleanos, cadenas, arrays, nulos permitidos y números finitos se validan por campo.

`raw.jsonl` tiene el conjunto exacto `SMOKE` de R10 y permite los campos de derivación `execution_status`, `telemetry_status`, `telemetry_reason`, `telemetry_detail`, `telemetry`. `geometry_raw.jsonl` usa `GEOMETRY_SMOKE` o `OFFICIAL` con proveedor/evento/celda O,m, worker/LP, R/R_common, marcas QPC/TSC y gates booleanos/string. `summary.jsonl` contiene un objeto por proveedor/celda con sus estadísticas y estado; `geometry_summary.jsonl` contiene lo mismo más O,m, plan y gates. Tras derivación, los resúmenes pueden añadir `telemetry`, `telemetry_status`, `telemetry_reason`, `telemetry_blocks`. No se reinterpretan claves desconocidas. Los campos y tipos exactos están implementados en `validate_r10.py`; el runner valida crudos/resúmenes antes y después de derivar.

## Estado científico

El smoke y los resultados geométricos están etiquetados y no se consideran evidencia de autorización ni de competencia oficial. Los umbrales científicos no se modifican. Una telemetría faltante invalida la muestra y conserva los datos crudos y la razón.
