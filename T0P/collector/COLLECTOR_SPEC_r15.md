# Especificación del collector R15

## Alcance e identidades

R15 queda aislado en archivos con sufijo `_r15` y `logs_r15/obj_r15`. El plan S-19 contiene 420 condiciones (60 de arquitectura y 360 de barrido), 140 unidades de calibración del R común, 124 pares A/A y 13,688 eventos únicos y ordenados. El SHA-256 del plan y el hash de su lista ordenada de IDs se ligan a la autorización A6.

La entrada integrada es `harness_r15.exe`. La opción `--official --session-id ID` delega al único parser AUTH y a la máquina de nueve fases de `official_run_r15.py`. Una sesión real exige un intento D13 write-once bajo `official_sessions_r15/<session_id>/attempt_01` y permite `attempt_02` solo tras un HOLD técnico tipado que admita el único reintento global. El gate no devuelve un PASS por defecto ni abre una captura oficial. Si falta AUTH, devuelve HOLD antes de medir.

Cada fuente real debe verificarse mediante el almacén D13 antes de usarla. Los crudos esperados incluyen preflight, lector, cualificación de referencias/ciencia y S-20, QA, residencia/eventos/episodios, márgenes/cierre y capturas D8. Los resultados pasan por las mismas funciones de fase que el rehearsal sintético; el veredicto se escribe como salida derivada con hashes de fuentes y ejecutable. El harness también ofrece `--synthetic-self-test`, `--qa-r15` y el smoke N=3 separado.

## Máquina de nueve fases

Cada proveedor debe devolver `status` y `label` explícitos. Excepciones, proveedor ausente, resultado mal formado o estado desconocido fallan cerrados.

| # | Fase | Comprobaciones principales |
|---|---|---|
| 1 | Preflight | Hash y geometría exactos del plan (13,688 eventos), topología LP y recursos requeridos. |
| 2 | Instrumentación/lector | Canales S-20, cobertura, gaps, frecuencia, REF_TSC y temperatura en los ocho P workers. |
| 3 | Kernel/referencias | Identidad de eventos de competencia y selección independiente de referencias ggml/Torch. |
| 4 | Cualificación científica | G_ggml, G_Torch, H-KQ, D′ y plateau con réplicas calificadas N=31; corte automático. |
| 5 | QA | Ejecución real de `omega_q4::run`, coordenadas y barrido verificados con S-12. |
| 6 | Residencia A/B/C | Unión exacta de eventos y A/B/C con R común por condición. |
| 7 | Márgenes/cierre | Carga continua y margen pre/post de la misma condición ≥0.200 s; plan completo. |
| 8 | Revalidación D8 | Capturas Q/R, IDs de ventana comunes, ocho LP, denominador ≥10,000, H2 y H3/D2 ≤0.10 por LP. |
| 9 | Derivación/veredicto | Datos válidos S-20, unión exacta, derivación y veredicto explícito. |

`HOLD_INSTRUMENTATION` detiene la adjudicación y no cierra la línea científica. Un gate científico fallido con instrumentos válidos devuelve `FALLIDA_CUALIFICACION`, marca `CLOSED_Q4_256_THIS_I7` y corta antes de residencia. Una residencia fallida válida produce `FALLO_RESIDENCIA_VALIDO`. También se distinguen `RESIDENCIA_PASS`, `L2_NOT_ESTABLISHED` y `NO_ADJUDICABLE_CONTROLES`.

El rehearsal sintético etiqueta los fixtures y ejecuta la ruta positiva completa, inyección en cada fase, corte automático de cualificación, las cuatro clases de veredicto, negativos D8 y reglas de reintento. Ningún fixture puede autorizar medidas oficiales.

## Restricción del lector S-23

`QUALIFIED_FOR_REQUIRED_CHANNELS_WITH_DECLARED_LIMITATION` requiere el conjunto completo de ocho LP P `0,2,4,6,8,10,12,14` y todos los canales obligatorios. El fallo histórico `INST_RETIRED.ANY` es diagnóstico en todos los LP; LP0 permanece entre los workers. REF_TSC fuera de [0.98,1.02], frecuencia inválida o canal obligatorio ausente causa `HOLD_INSTRUMENTATION`. No hay fallback de siete P. Con instrumentos válidos y gate científico fallido, LP0 no se excluye ni rescata el resultado.

## Autorización A6 (D14)

`AUTHORIZED.schema_r15.json` implementa el esquema estricto S-18 A6. Hay un único parser estricto en Python, usado por la entrada C++ mediante delegación al mismo orquestador. Rechaza claves duplicadas, BOM, JSON mal formado o no finito, campos ausentes, tipos incorrectos, claves desconocidas y valores que no coinciden con el esquema/política. A6 liga sesión, plan y orden, hash del ejecutable integrado, referencias, kernel, lector, validador, primitiva de expulsión, EMON/EDP, documentos y manifiestos R15.

`--official` sin AUTH válido falla cerrado con `measurements_started=false`. Las mutaciones del parser usan hashes ficticios marcados `TEST_ONLY`, solo en fixtures bajo `logs_r15/obj_r15/authorized_fixtures_r15`, eliminadas al terminar; el gate oficial siempre calcula y compara hashes de archivos reales. No se crea `collector/AUTHORIZED.json` real. En el entorno de esta tarea faltan `torch.dll`, `torch_cpu.dll` y `c10.dll` bajo `.venv_cuda/Lib/site-packages/torch/lib`, por lo que la integridad de referencias Torch falla cerrada antes de crear AUTH real.

El ledger permite un reintento técnico global solo ante `DURATION_INSUFFICIENT` o `FREQUENCY_UNSTABLE`, preserva sesión/plan/semilla/orden, no reintenta fallos científicos y hace final el segundo HOLD.

## Cadena SMOKE e instrumentación

El runner N=3 captura EMON continuamente alrededor del harness, crea un intento D13 nuevo, verifica hashes, ejecuta el único validador S-20 y llama al derivador. La etiqueta siempre es `SMOKE`; los números no se usan para adjudicación. El QA `--qa-r15` ejecuta el kernel en 512 posiciones para m=1,4,8,16 con O=1024 y 60 barridos, comprobando cada salida contra S-12.

Evidencia del smoke R15 del 2026-10-12: el harness pasó, pero S-20 produjo `INVALID/FREQ_WINDOW_EXCURSION` (120 muestras, 144,000 grupos EMON). La derivación quedó `SMOKE_ONLY`, `numeric_metrics_used=false`, unión incompleta por N=3. Estos son resultados de diagnóstico, no un veredicto de residencia.

La ruta de páginas grandes requiere `SeLockMemoryPrivilege` en el token del proceso. Si Windows devuelve `ERROR_NOT_ALL_ASSIGNED` o las regiones no son grandes, la adquisición informa el error real y no marca la medición como válida. El collector no cambia políticas del sistema.
