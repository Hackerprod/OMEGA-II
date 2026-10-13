# T0P harness R17 — especificación ejecutable

## Propósito y límite

R17 continúa la cadena de adquisición r16: plan congelado, preflight, QA de instrumentación/expulsión, cualificación, adquisición de residencia, controles finales y derivación. La etiqueta `SMOKE` conserva `numeric_metrics_used=false`; solo el ejecutable autorizado y el validador r17 pueden producir una decisión científica. No existe una sesión oficial autorizada en este paquete.

## Construcción y entrada

`build_r17.cmd` enlaza el kernel Q4, su QA standalone contra el mismo objeto final, topología, lector EMON, expulsión, proveedores OMEGA/ggml/Torch, BW_DRAM y P_instr. La salida integrada es `logs_r17/obj_r17/harness_r17.exe`. Al entrar, el proceso llama `enable_large_page_privilege` para habilitar `SeLockMemoryPrivilege`; si falla, se detiene antes de medir. `--official` requiere autorización R17 válida y mantiene el cierre por defecto.

La cadena invocable desde Python es `collector/official_run_r17.py`; su adaptador inicia el ejecutable integrado con la fase y evidencia previa, recoge QPC del padre y controla el cierre de EMON con `emon -stop`. El ejecutable admite `--instrument-qualification-r17 <dir> <plan> <schedule>` y la adquisición N=3 por `--residency-smoke-r17`.

## Trabajadores y región medida

Ocho workers persistentes se fijan a sus LP P-core. Las barreras de inicio, trabajo y salida son atómicas; el recorrido de espera hace busy-wait con `_mm_pause`. La barrera se mantiene desde el margen previo hasta el margen posterior. La cancelación publica la bandera, libera todas las barreras y une los threads; `--barrier-cancel-test-r17` comprueba que retorna antes del límite.

Dentro de `[t_pad_ini,t_pad_fin]` se guardan solo marcas y registros binarios en buffers reservados. Hashes, JSON, impresión, asignaciones y validaciones quedan fuera. La región incluye episodios de kernel, expulsión obligatoria, acondicionamiento idéntico y sincronización indispensable. La telemetría se etiqueta `frequency_of_condition`; cada ventana usa la duración real y exige `a_i,k = f_TSC * Δt / ΔREF_TSC` entre 0,98 y 1,02.

Cada calibración determina `R(W,m,K)` una vez, común a A/B/C, con duplicación hasta región ≥2,5 s (mínimo 2,0 s) y kernel acumulado ≥150 ms (mínimo 100 ms). La fila y los episodios conservan `R_unit_id`; R queda fijo para N=31.

## Sonda caliente y cualificación S-25-B

Al inicio y cierre de cada segmento se adquieren 31 bloques de sonda load-only: footprint 294.912 B por P, ocho P concurrentes, K=16, footprint de m=16, las mismas barreras y acondicionamiento de no-pesos. Los pesos se calientan una sola vez; no se refrescan durante la espera y A no se sondea ni precalienta justo antes de medirla. Cada bloque apunta ≥2,0 s y los controles exigen H2≤0,10, H3|2≤0,10 y D2≥10.000 por core. Sonda o actividad inválida implica `HOLD_INSTRUMENTATION`.

`--instrument-qualification-r17` acepta el schedule de diagnóstico N=1 o el de 36 bloques. N=1 es solo smoke de integración. El modo FULL codifica W288/O1024, m={1,16}, K={1,16}, A/B/C y tres réplicas; acondiciona 180 s, no rellena bloques y exige región ≥60 s. R se calibra por (m,K) y se comparte A/B/C. No se repite hasta PASS. Este paquete no ejecuta FULL; lo ejecuta el juez con Chrome cerrado.

## Plan, persistencia y QA

`collector/residency_plan_r17.py` congela plan, 31 órdenes réplica-mayor S-25-C, permutación interna y límites de pausa. Hay 72 unidades de cualificación (BW_DRAM junto con H-KQ/D′ en la unidad planificada), 140 calibraciones R de residencia y 31 segmentos, cada uno con los 16 W indivisibles.

Los crudos adquiridos se escriben una vez en `logs_r17/obj_r17/<attempt>`; telemetría y derivados llevan hashes de origen. El QA del kernel reutiliza la comprobación standalone sobre el objeto enlazado: igualdad exacta O=1024 y barrido de 512 coordenadas en los otros quince tamaños.

## Comandos de evidencia

```powershell
cmd /c T0P\harness\build_r17.cmd
python -B T0P\collector\residency_plan_r17.py self-test --output T0P\collector\logs_r17\obj_r17\residency_plan_self_test_r17.txt
python -B T0P\collector\instrument_qualification_r17.py self-test --output T0P\collector\logs_r17\obj_r17\instrument_qualification_self_test_r17.txt
python -B T0P\collector\official_run_r17.py --self-test --output T0P\collector\logs_r17\obj_r17\official_run_r17_qa.txt
```

Los smokes EMON se ejecutan solo con ≥4 GiB libres, límite `-l` calculado desde el schedule y presupuesto por intento <1 GiB. No se admite usar un smoke para adjudicar.

## Estado comprobado y límite actual

El smoke N=3 del 12-10-2026 terminó en `HOLD_INSTRUMENTATION` en R01/BEGIN, muestra 1. La sonda midió una región de 4,780637 s y reportó un overhead de coordinador de 7,014149. La causa fue un error de orden: el registro leía las marcas de worker después del margen final, que ya había sobrescrito las marcas de la región medida. R17 copia las seis marcas binarias por worker inmediatamente después de la región, antes de ese margen; deja afinidad, duración y validaciones para después de `t_pad_fin`. `verify_hot_probe_stamp_r17.py` prueba el orden fuente y distingue el fixture correcto del dato de margen obsoleto. No se repitió la captura física; S25-A permanece sin PASS físico.

Después del arreglo, `build_r17.cmd`, el QA kernel enlazado, la prueba de cancelación y el análisis de desensamblado pasaron. El primer smoke S25-B N=1, anterior al arreglo, quedó en `HOLD_INSTRUMENTATION_QUALIFICATION`; FULL de 36 bloques no se ejecutó. Por ello la aprobación instrumental y la sesión oficial siguen bloqueadas hasta una adquisición autorizada posterior.
