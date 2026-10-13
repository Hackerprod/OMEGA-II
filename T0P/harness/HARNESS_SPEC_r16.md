# HARNESS_SPEC_r16

## Alcance

`harness_r16.exe` es el ejecutable integrado de adquisición S-24. `build_r16.cmd` enlaza kernel, common, mapa de topología, residencia, expulsión, proveedores OMEGA/ggml/Torch (fronteras prepared y dynamic), BW_DRAM y P_instr. No modifica los árboles fuente de kernel, evict ni validator. Todos los binarios y salidas de ejecución viven bajo `T0P/harness/logs_r16/obj_r16` o `T0P/collector/logs_r16/obj_r16`.

La entrada `--acquire-r16` implementa las fases preflight, QA/instrumentos/expulsión, cualificación, residencia, controles finales y derivación/veredicto. Cada fase valida la identidad del plan, topología y hash de la evidencia previa inmediata antes de producir su propio registro. `--synthetic-self-test` prueba el corte de transiciones; `--plan-qa-r16`, `--provider-qa-r16`, `--instrument-qa-r16` y `--qa-r16` son pruebas etiquetadas, no resultados científicos. `--official` requiere una sesión autorizada y no se usa en smoke.

## Construcción y pruebas

- Construcción: `T0P\\harness\\build_r16.cmd`.
- Plan y orden: `python -B T0P\\collector\\residency_plan_r16.py self-test`.
- QA de plan ejecutable: `harness_r16.exe --plan-qa-r16 <plan> <schedule>`.
- QA integrado de proveedores/kernel: `harness_r16.exe --provider-qa-r16` y `harness_r16.exe --qa-r16`.
- Cualificación auxiliar: `harness_r16.exe --instrument-qa-r16 <obj_r16> <disassembly.json>`.
- Smoke real con EMON: `T0P\\harness\\run_residency_smoke_r16.ps1 -AttemptName <nombre>`; exige al menos 4 GiB libres en C:, impone tope de 1 GiB por intento y detiene EMON de forma controlada tras la adquisición.
- Pruebas de gate: `python -B T0P\\collector\\official_run_r16.py --self-test`.

La entrada habilita `SeLockMemoryPrivilege` en su propio token antes de reservar páginas grandes. Los ocho workers P se enlazan al mapa de topología; el coordinador queda en otro LP. Se registra y comprueba la correspondencia worker→LP, incluidos los LP observados por cada aplicación. La calibración TSC restaura la afinidad del coordinador y no cambia IRQ.

## Contrato de residencia

El ejecutable lee el plan oficial y el schedule completo suministrado. Comprueba hash, IDs, orden, fases, multiplicidad y adyacencia de unidades A/B/C, pares A/A y bloques completos. Un grupo A/B/C y cada par A/A se ejecutan sin pausas entre componentes; el orden interno procede de la semilla congelada. Las condiciones de A y C reutilizan por worker el mismo buffer vivo de pesos y las mismas regiones de activación; B comprueba K buffers disjuntos. Las variantes despachan el proveedor efectivo por worker y frontera.

La ruta adquirida calibra R antes de medir y fija el mismo R para A/B/C durante N=31. Cada dispatch agrupa sus repeticiones en el worker, con buffers de evidencia preasignados y sin callback atómico por línea. Por muestra se verifica la región continua de condición (objetivo 2,5 s, mínimo 2,0 s), kernel-only por worker (objetivo 150 ms, mínimo 100 ms), intercambio del coordinador ≤1 %, márgenes pre/post ≥0,200 s, sink y timestamps QPC/TSC enteros. No se añade espera ni trabajo sintético para rellenar duración.

La expulsión medida usa solo `omega_evict::evict_range` y las regiones de pesos; la QA de instrumentación se adquiere en fase separada. Las llamadas observadas se contrastan con variante, K y R. Los hashes, persistencia y serialización se escriben fuera de las ventanas medidas. El mismo mapa de topología se entrega a workers, proveedores, QA y validadores.

## BW_DRAM y P_instr

BW_DRAM barre el buffer contiguo de 256 MiB en secuencia desenrollada ×4 con ocho workers concurrentes; la fase adquirida registra páginas, sink, calentamiento, calibración y duración por worker. P_instr adquiere nueve variantes, N=31 en cualificación completa, conserva el desensamblado y exige calibración de duración para todos los workers. Los datos auxiliares de smoke llevan `numeric_metrics_used=false`.

## Salidas y límites

Los JSON/JSONL incluyen eventos, episodios por worker, calibración, marcas y márgenes, reloj, afinidades, manifiesto de expulsión, configuración efectiva de providers y hashes. El runner cierra EMON con `emon -stop`, guarda QPC del proceso padre y rechaza capturas ausentes o de 1 GiB o más; el footer EMON es diagnóstico.

El smoke usa el plan congelado, N=3 y etiqueta `SMOKE`; no autoriza resultados numéricos ni una sesión oficial. Las fases de revalidación D8 desde crudos y derivación D11 dependen del validador/derivador, pendientes D10/D11. AUTH real, sesión oficial, ensayo de 36 bloques y estimación permanecen fuera de esta ejecución.
