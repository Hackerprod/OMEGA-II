# Especificación del collector R11

## Alcance y artefactos

`official_run_r11.py` es el punto único de entrada para el plan, el parser S-18 A6, la derivación y el orquestador de prueba. Lee el plan canónico `T0P\collector\plan.json` solo como referencia de cualificación existente; no lo reescribe. La residencia usa `residency_plan_r11.json`, congelado con 420 condiciones, N=31, warmup=5, salida ordenada por SHA-256 de la clave de condición y política R común por (W,m).

El barrido contiene 60 condiciones arquitectónicas A/B/C × m={1,4,8,16} × K={1,2,4,8,16}, O=1024, y 360 condiciones de los otros quince W × A/B/C × m × K={1,16}. O se calcula con `8*round_half_up(W*1024/(8*288))`. Los umbrales de S-19 y controles S-20 quedan fijados en `PAGE_EXPECTED_COLLECTOR_r11.json`; `--page-gate` los compara campo por campo.

## Derivación

El derivador consume JSONL estricto. Agrupa filas por condición y réplica, exige exactamente ocho workers fijados a LP 0,2,…,14, un registro por worker, direcciones positivas y no alias, R común por W,m, N=31, 31 réplicas y al menos 2,0 s de kernel medido por muestra. Para B exige K direcciones independientes y materializadas por worker; para A/C, una dirección por worker. Usa el tiempo de equipo `T_X(K)` registrado por el harness.

Calcula por réplica `(T_X(16)-T_X(1))/15`, luego la mediana de las 31 réplicas; `rho=c_A/c_B`, `delta_BC`, S_m, P95 nearest-rank de A/A, rodilla sobre los tres puntos iniciales y atribución L2 con eventos L2/LLC de igual alcance. Conserva todos los puntos del barrido. No aplica cortes científicos cuando la cualificación anterior falla: clasifica `FALLO_COMPETENCIA`, `FALLO_RESIDENCIA_VALIDO`, `L2_NOT_ESTABLISHED` o `NO_ADJUDICABLE_CONTROLES`.

## A6 y modos de ejecución

El parser A6 rechaza claves JSON repetidas, UTF-8/BOM inválidos, constantes no finitas, propiedades ausentes o adicionales, tipos distintos, versión equivocada y hashes que no coinciden byte por byte. Compara también flags, versión de plan, semilla y política de reintento. `--auth-self-test` construye una fixture solo en `test_r11`; `--dry-accept-fixture` la acepta como `READY_TO_RUN` sin mediciones. Ninguna fixture se escribe como `AUTHORIZED.json`.

`--dry-run-synthetic` recorre las nueve fases simuladas; `--cut` inyecta un cierre en cada corte. `--smoke-real` ejecuta solo cinco condiciones etiquetadas `SMOKE_ONLY`, N=3, con EMON continuo reducido, equivalencia numérica A/B/C y derivación que informa `numeric_metrics_used=false`. `--official` falla cerrado antes de adquirir datos si falta A6; con A6 válida, por el estado del entorno actual, también se detiene en preflight porque la cualificación conjunta del lector EMON/TSC de S-20 está pendiente.

## Reproducibilidad y límites

Los manifiestos r11 cubren archivos de entrega y objetos compilados; los logs de ejecución y la fixture A6 de prueba se cubren con `SHA256SUMS_evidence_r11.txt` para evitar ciclos de hash. `logs_r11` y `obj_r11\tmp` son rutas de salida; los manifiestos base no se regeneran. No se crea autorización real ni se inicia sesión oficial. El smoke verifica integración de hardware, no umbrales ni estado científico.
