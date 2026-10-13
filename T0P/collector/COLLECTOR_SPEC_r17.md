# T0P collector R17 — especificación ejecutable

## Alcance

El collector r17 conserva la adquisición y deriva únicamente desde crudos inmutables más configuración registrada. Los datos de smoke no son adjudicables. `--official` exige un `AUTHORIZED.schema_r17.json` válido; esta tarea no lee un AUTHORIZED real ni abre sesión oficial.

## Plan y schedule

`residency_plan_r17.py` enlaza 32.102 eventos: 72 unidades de cualificación, 420 condiciones de residencia, 140 calibraciones `R(W,m,K)`, 31 segmentos S-25-C y 124 pares A/A. Incluye BW_DRAM en el segmento de cualificación que comparte con H-KQ/D′. El plan contiene 31 órdenes S-25-C con hash serializado y limita pausas a los límites entre réplicas.

`instrument_qualification_r17.py` congela schedule N=1 diagnóstico y schedule FULL de 36 bloques. La prueba FULL incluye 12 condiciones × 3 réplicas, acondicionamiento 180 s por segmento, objetivo de región 75 s/mínimo 60 s, R común por (m,K), sin relleno ni reintento hasta PASS. Solo se ejecutan pruebas sintéticas y smoke N=1; FULL queda reservado al juez.

## S-20 y D13

`telemetry_r17.py` calcula `-l` a partir del schedule real, configura los eventos y captura límites QPC del padre. EMON se termina con `-stop` tras el último margen; `-l` es un límite de seguridad, no el criterio de fin. El footer EMON se guarda solo como diagnóstico.

El validador gobernante es `s20_validator_r17.py`. En modo FULL, los grupos y pares se derivan de IDs completos del plan/schedule y de las condiciones adquiridas; se registra `frequency_of_condition`. S-20 recalcula frecuencia y límites desde ventanas válidas de los crudos, con cobertura y comparación por fase. La validación no acepta una lista JSON de IDs declarados válidos.

`artifact_store_r17.py` aplica escritura única a cada crudo, guarda SHA-256 y liga cada derivado a sus crudos fuente. `_phase_science` y `_phase_derivation` deben consumir esa liga. Constantes de energía, TCC, topología y UTC se registran como configuración de sesión antes del análisis y se verifican contra el preflight; una captura de smoke no establece su validez oficial.

## Derivación S-19/D11

`residency_derive_r17.py` verifica la unión exacta plan/ejecutado/derivado, episodios por worker, márgenes, afinidad, regiones de pesos, N=31 y calibración común A/B/C. Cada marginal debe ser positivo y finito. L2 se atribuye a las 31 réplicas gobernantes de cada W (fase residency, variante A, m=1, K=16); cada P/LP conserva contadores, fracciones, ID de evento y hashes de crudo/validación. No requiere pareja Q/R para este control.

La atribución exige coincidencia causal de δBC y del par de puntos de elevación temporal fuera del plateau inicial. Fixtures cubren marginal no positivo, fracción no finita, falta de procedencia, controles de ruido y falta de knee.

## Controles D8 y autorización D14

`official_run_r17.py` exige para acceso frío fracción de fallos LLC `cold_M3 / OFFCORE ≥0,90`; el umbral no es LLC-hit. La prueba sintética acepta expulsión válida (0,996) y retiene la inversión (0,001). El resto de D8 comprueba H2/H3, denominadores, LP y ventanas Q/R.

`AUTHORIZED.schema_r17.json` fija fases completas, hash del ejecutable, hash de `torch.dll`, `torch_cpu.dll` y `c10.dll` bajo `C:\Projects\OMEGA\.venv_cuda\Lib\site-packages\torch\lib`, plan, segmentación, 72 unidades, 31 órdenes S-25 y validador. El documento de autorización declara razones reintentables de duración, frecuencia y pérdidas transitorias EMON/telemetría, con presupuesto técnico global de uno; las pausas planificadas no lo consumen. Las pruebas mutan cada campo. El runner r17 aún no ejecuta ese reintento global: el enlace de autorización está probado sintéticamente y `--official` cierra antes de medir en esta tarea.

## Salidas, comandos y límites

Cada intento y su `raw_manifest_r17`/`derived_lineage_r17` se ubican bajo `logs_r17/obj_r17`. Los hashes de publicación excluyen crudos, logs, temporales y `__pycache__`. Antes de cada smoke se exige ≥4 GiB libres en C: y se estima el tamaño con schedule y tasa EMON medida; cada intento debe quedar bajo 1 GiB.

```powershell
python -B T0P\collector\residency_plan_r17.py self-test
python -B T0P\collector\instrument_qualification_r17.py self-test --output T0P\collector\logs_r17\obj_r17\instrument_qualification_self_test_r17.txt
python -B T0P\collector\residency_derive_r17.py self-test --output T0P\collector\logs_r17\obj_r17\residency_deriver_qa_r17.txt
python -B T0P\collector\official_run_r17.py --self-test --output T0P\collector\logs_r17\obj_r17\official_run_r17_qa.txt
```

El smoke N=3 y el smoke S-25-B N=1 solo acreditan integración. La campaña oficial, el schedule FULL de 36 bloques y su resultado dependen del juez; no se declaran cerrados desde estos smokes.

## Estado comprobado y límite actual

El smoke N=3 ejecutó preflight, QA de kernel/instrumentación/expulsión, cualificación `SMOKE_ONLY` con BW_DRAM y P_instr, corte automático y entrada al modo de residencia. Se detuvo en la primera sonda S25 de R01 por `coordinator_overhead_fraction=7.014149`; no produjo muestras de residencia y EMON se detuvo con `emon -stop`. El runner r17 ahora deja S-20 en HOLD explícito cuando falta el preflight gobernante, en vez de intentar derivar desde evidencia incompleta. No hay adquisición S-20 FULL real ni derivación adjudicable.

Las pruebas sintéticas de D10 cubren cuatro mutaciones S-20 y procedencia D13; D11 cubre 13 ramas. Estas no reemplazan la prueba desde crudos de la campaña completa. La autorización r17, los hashes de Torch, plan y validador se prueban con fixtures; no se abrió un AUTHORIZED real. El detalle de tiempos medidos, estimaciones por segmento y hashes publicados está en `SESSION_ESTIMATE_r17.md` y `STATUS_r17.md`.
