# Especificación del harness de residencia R11

## Medición A/B/C

El núcleo es `omega_q4::run` del mismo kernel Q4 del harness r10. Cada condición crea ocho shards O×512, uno por worker P en LP 0,2,…,14, con activaciones y salida privadas. Large pages se verifican en pesos, activaciones y salida; la QA comprueba reconstrucción exacta, productos de grupo y S-12.

- A: un bloque de pesos por worker, expulsado antes del episodio. Se reutiliza la misma dirección K veces sin flush intermedio.
- B: K asignaciones independientes por worker, regiones no solapadas, byte idénticas, materializadas y con páginas verificadas. Se expulsan todas antes del episodio y se usa una copia por aplicación, sin flush adicional.
- C: el mismo bloque/dirección de A; flush inicial y antes de cada aplicación posterior.

Antes de cada aplicación se acondicionan activaciones y salida de forma idéntica. Se escribe el mismo valor previo en Y; se lee X y no se toca pesos durante el acondicionamiento. Flush, acondicionamiento y barreras se cronometran aparte y quedan fuera de `kernel_only`. Prefetch de software no se usa; el estado de prefetch de hardware no se cambia. El flush elige CLFLUSHOPT si CPUID lo permite, con SFENCE; en otro caso CLFLUSH con MFENCE.

## Reloj y crudos

El smoke fija afinidad, comprueba invariancia TSC y ausencia de migración, calibra TSC contra QPC durante cinco intervalos y exige error relativo ≤0,1 %. Se cualifica sincronización entre los ocho cores y mide el coste de un par RDTSCP+fences; exige ≤1 % del intervalo de aplicación más corto. Para cada aplicación registra por worker TSC, QPC, LP, dirección de pesos y tiempos separados de flush, acondicionamiento y barrera. `t_app=max(e_i)-min(s_i)` se calcula con las ocho marcas TSC; T_X(K) suma los K `t_app` del episodio.

Las salidas A/B/C se comparan bit a bit para cada condición, réplica y worker. El crudo de smoke conserva `SMOKE_ONLY`, N=3 y R=1 y nunca entra a umbrales. La ejecución oficial requiere N=31, warmup=5, un R común para A/B/C y todo K dentro de cada W,m, y ≥2,0 s acumulados del tramo medido por muestra; no se rellenan muestras con espera.

## Ejecución y límites

Compilar: `cmd /c call T0P\harness\build_r11.cmd`. El runner collector genera el subconjunto SMOKE y ejecuta `harness_r11.exe --residency-smoke <captura> <condiciones.csv> 3`. Builds, objetos temporales y crudos quedan en `obj_r11\` y `logs_r11\`.

La ruta SMOKE real cubre O=912, 1024 y 58256, A/B/C, ocho shards y varias parejas m,K. `run_residency_campaign` acepta solo N=31, 420 condiciones y un CSV de seis columnas con R calibrado; verifica que R sea común por W,m y ejecuta R episodios por muestra sin esperas. Cada fila conserva `sample_seconds=R·T_X` y queda inválida si el tramo medido no llega a 2,0 s. El modo `--residency-campaign` exige las marcas de autorización del orquestador y EMON activo; el modo directo `--residency-smoke` solo admite N=3.

No existe una autorización real, y S-20 mantiene pendiente la cualificación del lector conjunto EMON/TSC; el orquestador cierra antes de la adquisición oficial. Por eso N=31 y la matriz completa están implementados, pero no se ejecutaron como sesión oficial.
