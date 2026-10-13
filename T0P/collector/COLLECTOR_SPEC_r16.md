# COLLECTOR_SPEC_r16

## Cadena de adquisición

`official_run_r16.py` es el gate secuencial. Su adaptador explícito lanza el ejecutable real `harness_r16.exe`; en pruebas, un doble sustituye solamente la ejecución del proceso. La máquina decide y sella cada fase antes de invocar la siguiente:

1. `preflight`: identidad del plan, recursos, topología, páginas grandes, reloj y lector disponible.
2. `qa_instruments_expulsion`: QA de kernel, proveedores, instrumentos y expulsión antes de medir.
3. `qualification`: BW_DRAM y P_instr, luego selección/comparación y corte automático en el modo oficial.
4. `residency`: solo tras cualificación apta; consume el schedule sellado en orden.
5. `final_controls`: revalidación D8 posterior a la residencia.
6. `derivation_verdict`: veredicto solo después de controles válidos.

Un `FALLIDA_CUALIFICACION` o `HOLD` corta la cadena antes de la adquisición siguiente. El fallo temprano no requiere episodios ni resultados de fases futuras. En SMOKE, la cualificación mínima diagnóstica no habilita resultados científicos, y los controles/derivación oficiales permanecen bloqueados.

## Plan y verificaciones

`residency_plan_r16.py` congela el plan íntegro (32.102 eventos), hashes de contenido y orden, condiciones y mapa de IDs. La selección usa 31 muestras entre candidatas elegibles y la comparación contiene solo OMEGA, la ganadora ggml y la ganadora Torch; los conteos son 11.718 + 6.696 eventos. La ganadora ocupa una posición de familia congelada. Los cortes de segmento respetan unidades indivisibles de §4 y cada segmento revalida identidad, topología, páginas, reloj y lector, con 180 s de acondicionamiento.

La verificación rechaza IDs duplicados o ausentes, reordenamiento, multiplicidad incorrecta, A/B/C separados y pares A/A separados. Residence se ordena por bloques completos `(W, réplica)` y contiene sus marginales, K y variantes; W=288 incorpora A/A. El schedule de smoke es una selección etiquetada del plan, no una sustitución de la adquisición oficial completa.

`qualify_r16.py` comprueba elegibilidad, selección, frontera y plateau. Los contrastes instrumentales son 12/U8 frente a 14/U8, 12/U16 frente a 12/U8 y 14/U16 frente a 14/U8, con tolerancia de 3 %. El caso sintético `0,98; 1,00; 1,02; 1,00` debe producir `HOLD_INSTRUMENTAL`. Plateau inválido nunca habilita cualificación.

## D13, EMON y verificaciones D8

`telemetry_r16.py` crea cada intento write-once bajo `logs_r16/obj_r16`, copia las entradas selladas y calcula `-l` desde el schedule y presupuesto temporal, no desde un valor fijo. El runner comprueba antes de EMON C: ≥4 GiB libres y el límite estimado <1 GiB; comprueba ambos de forma continua. EMON empieza tras QA, termina con `emon -stop` después del último bloque/margen y se liga a marcas QPC del padre. Si una captura truncada afecta eventos medidos, se invalida. El footer EMON solo se usa como diagnóstico.

`verify_residency_smoke_r16.py` verifica hash de entradas/crudos, orden/multiplicidad, workers, buffers, regiones, márgenes y proceso. En D8, numerador y denominador deben compartir ventana dentro de cada captura y alcance; Q y R pueden tener IDs de ventana distintos. Las puertas frío/caliente, ratio y reglas S-22 se validan desde las capturas; los flags de entrada no cuentan como revalidación. El caso sintético de Q/R con ventanas distintas debe aceptarse.

## Pruebas y estado

- `python -B T0P\\collector\\residency_plan_r16.py self-test`
- `python -B T0P\\collector\\qualify_r16.py --self-test`
- `python -B T0P\\collector\\official_run_r16.py --self-test`
- `python -B T0P\\collector\\verify_residency_smoke_r16.py --self-test`
- `python -B T0P\\collector\\telemetry_r16.py self-test`

Las pruebas de gate verifican que no se adquiere residencia tras `FALLIDA_CUALIFICACION`/HOLD. Las pruebas del plan mutan duplicados, faltantes, orden y pares separados. Los datos del smoke son diagnósticos y nunca alimentan veredicto científico.

El validador completo desde crudos S-20/D10, derivador D11, QA integrada D12 y autorización D14 quedan pendientes según su frontera de entrega. No se crea `AUTHORIZED.json` real ni se inicia sesión oficial. La prueba acotada de 36 bloques y la estimación requieren autorización y recursos posteriores.
