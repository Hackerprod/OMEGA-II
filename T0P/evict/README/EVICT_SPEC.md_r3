# EVICT_SPEC — biblioteca y cualificación independiente S-19/S-20

## Expulsión

`evict_range(ptr, bytes)` expulsa cada línea de 64 B que intersecta `[ptr,ptr+bytes)`, incluidos extremos parciales; rechaza puntero nulo, longitud cero y desbordamiento. Termina con `SFENCE` y `MFENCE`. `evict_weights_regions()` admite solo `WeightCodes` y `WeightScales`; rechaza X, Y, activaciones, sumas, scratch y salida. La selección queda congelada: CLFLUSHOPT cuando `CPUID.07H:EBX[23]`, de otro modo CLFLUSH. La variante diagnóstica no cambia esa selección. No se usa CLWB ni prefetch de software.

La variante de ocho hilos fija y verifica un worker en cada LP `0,2,4,6,8,10,12,14` (los ocho P-cores de mayor clase de eficiencia) y coordina por barrera. El layout Q4 usa stride 288 B: 256 B códigos y 32 B escalas. El shard O=1024 ocupa 294.912 B. Todas las asignaciones de QA son large pages reales alineadas (2 MiB), con DUMMY `0xA5`; no se cargan ni ejecutan kernels Q4, OMEGA, ggml ni Torch.

QA determinista compara direcciones tocadas con todas las líneas de códigos/escalas y con X/Y/activaciones/sumas/scratch. Se rechazan omitir última línea, desplazar un byte e incluir X. La repetición arquitectónica comprueba ocho shards con ocho workers concurrentes.

## Control LLC y muestras

El control por P usa un victim-buffer DUMMY de 8 MiB, recorrido dependiente con permutación Fisher–Yates determinista (xorshift32, semilla `0x53493139`), sin prefetch de software. Se obtienen N=31 tiempos fríos/calientes por P. Cada muestra es el promedio de recorridos dependientes de esa réplica; el ratio gobernante es `mediana_r(t_frío,r/t_caliente,r)`. Se repiten frío/caliente con CLFLUSH solo como diagnóstico.

## Método S-20 A: región medida dentro de bloques con márgenes

La adquisición oficial de esta cualificación usa EMON CLI con `-collect-edp` y `T0P/telemetry/reader/emon_joint_qualification.edp` (3.600 grupos conjuntos, sin multiplexación). Para **cada condición independiente** se ejecuta la misma carga continuamente en `[t_pad_ini,t_pad_fin]`; la región de rendimiento y análisis es `[t_ini,t_fin]`, estrictamente interior. Se fijan márgenes mínimos `m_ini,m_fin ≥ 0,200 s`, es decir, dos veces la ventana EMON máxima observada de 0,100 s. En la repetición se programaron 0,250 s por lado y el menor margen efectivo fue 200,026 ms. Cada margen conserva la misma carga, operaciones y estado de caché de la condición; no se intercala otra condición ni flush/acondicionamiento ajeno.

Esta definición **no equivale a llamar bloque a la propia región medida**. El acolchado es actividad de la misma condición que protege sus extremos; no cuenta como muestra de rendimiento ni entra en el denominador estadístico o temporal. Se consideran únicamente ventanas EMON completas que solapan `[t_ini,t_fin]` y están contenidas en `[t_pad_ini,t_pad_fin]` (con guarda U); las que cruzan el acolchado se excluyen y contabilizan. No se interpolan ni extrapolan contadores. La cobertura y los huecos se calculan sobre la unión de los segmentos completos recortados a la región, nunca sobre la duración de los márgenes.

`U` se obtiene ajustando QPC/FILETIME con muestreo de bracket: `U = residuo_máximo + bracket_máximo/2 + 0,5 ms` por cuantización del timestamp civil. Cada ventana termina en el QPC ajustado del timestamp EMON y empieza `THREAD/f_TSC` antes, con `f_TSC=2112,003049 MHz`. Para segmentos unidos `[a_i,b_i]` recortados a la región de duración `D`:

- `coverage_lower = Σ max(0, (b_i−a_i)−2U) / D`.
- Cota de hueco en el primer/último extremo: hueco observado `+ U`; entre segmentos: hueco observado `+ 2U`. El máximo debe ser ≤20 ms.
- También se exige duración medida ≥2 s, ≥20 ventanas, cobertura ≥95 %, ventana individual ≤100 ms y `U≤1 ms`.

Fracción LLC por P/condición: `Σ OFFCORE_REQUESTS.L3_MISS_DEMAND_DATA_RD / Σ OFFCORE_REQUESTS.DEMAND_DATA_RD`; L2 usa las entradas equivalentes `L2_RQSTS`. Se suman numerador y denominador sobre los mismos grupos y el mismo par LP; denominador cero es no estimable. Los márgenes nunca se agregan a estas sumas.

## Módulo común de análisis

`T0P/telemetry/reader/sync/emon_window_sync.hpp/.cpp` conserva `WindowSync` y `EmonPipeReader` para diagnóstico de marcas y llegada de stdout. `sync_begin_block()`/`sync_end_block()` **no son el método oficial**: la tubería entrega líneas en ráfagas y la detección no delimita ventanas de manera fiable. El análisis previo mostró alternancia +2/−60 ms (p95 64 ms, máximo 81 ms); en la repetición nueva el máximo diagnóstico fue 143,628 ms. Se descarta la sincronización de tiempo real.

El API común `analyze_padded_block(PaddedBlock, windows, U, required_margin_ms=200)` verifica orden y duplicados, márgenes, contención, exclusiones, unión/cobertura, hueco, cuenta de ventanas y duración máxima. Devuelve márgenes, ventanas usadas/excluidas, límites individuales y `qualified/reason`. El colector debe guardar para cada condición las cuatro marcas QPC (acolchado y región) y su mapa QPC/UTC, ejecutar EMON CLI oficial y pasar ventanas reconstruidas sin reordenarlas. No debe esperar a que llegue una línea para decidir los límites.

## Umbrales y decisión

No se cambian umbrales: ratio frío/caliente ≥1,50; LLC fría ≥0,90; LLC caliente ≤0,10; cobertura ≥95 %; hueco ≤20 ms; ≥20 ventanas; ventana ≤100 ms; región ≥2 s. `EVICTION_QUALIFIED` exige conjuntamente los controles físicos y temporales. En caso contrario se informa el `HOLD_*` correspondiente, sin selección posterior de resultados.

