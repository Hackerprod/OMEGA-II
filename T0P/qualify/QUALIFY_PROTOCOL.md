# Protocolo ejecutable de cualificación — revisión de Sol

## Alcance y estado

Este paquete ofrece `--dry-run`, un motor de estadística/decisión probado solo con fixtures `SYNTHETIC`, y `--timer-smoke` sobre una carga entera `DUMMY`. El smoke no llama al kernel OMEGA ni a ggml/Torch. Las pruebas sintéticas no representan datos de hardware. Los adaptadores de QA existentes permanecen separados del tramo medido.

**Bloqueo de entrega:** el recolector que adquiere muestras oficiales y coordina las rutas OMEGA/ggml/Torch aún no está integrado. Por tanto `--official` valida `AUTHORIZED.json` (hash de ejecutable y de este protocolo) y sigue fallando cerrado incluso si ambos hashes coinciden. No crear ni simular esa autorización. El modo no ha sido ejecutado en esta revisión. La implementación estadística no constituye un recolector de mediciones.

El estado científico es el de T0.md §10 y MD/35–43: las varas están congeladas por S-16; S-16 autoriza presentar a Sol el kernel, su QA y este harness antes de cualquier medición oficial. La campaña oficial debe ejecutarse en una sola sesión nueva, sin importar datos históricos.

## Configuración y fronteras

- Forma contractual: ocho shards independientes de `[1024,512]`, suma de 4.194.304 pesos, `m ∈ {1,4,8,16}`.
- Rutas ggml: `ggml_flat` con `use_ref=false` y despacho optimizado normal; `ggml_repack` con funciones upstream VNNI. La vara ganadora se elige solo entre referencias cualificadas, por mediana de tiempo, fijándola por celda y frontera antes de comparar OMEGA. No se elige por el ratio contra OMEGA.
- Rutas Torch: `mm`, `matmul`, `linear`; `mv` solo para `m=1`. Se usan las variantes `_out` con `Y` preasignada. `mkldnn_linear` queda fuera del conjunto gobernante por política de páginas.
- Cada celda/ruta tiene dos fronteras: entradas preparadas; preparación dinámica necesaria más operador. La preparación dinámica se cobra en cada aplicación. Los layouts persistentes de pesos pueden prepararse fuera y su almacenamiento se registra. No se permite una copia INT8 persistente decodificada ni fusionar aplicaciones independientes.
- En Torch se controla una hebra interna por worker (ATen intra/inter-op y OpenMP/MKL); en ggml `n_threads=1`. En 8P se usan ocho workers persistentes, uno por P-core físico, sin SMT y con shards separados. La salida forma parte de la política de memoria de cada ruta.
- Todos los buffers efectivos, incluida `Y`, deben pasar la verificación `QueryWorkingSetEx`: cobertura por dirección y longitud con `dwPageSize`, `Valid=1`, `LargePage=1`, sin fallback. `PAGE_EXPECTED_QUALIFY.json` fija la cobertura esperada de las referencias y OMEGA.

## Preflight de una sesión oficial

Antes de cualquier muestra: comprobar hashes de ejecutable, fuentes y DLL cargadas; validar CPU, topología, P-cores, SMT hermanos, ISA y estado del sistema; aplicar y verificar afinidad; exigir páginas grandes en todos los buffers; comprobar el plan de energía, Chrome cerrado, procesos de fondo y condiciones ambientales; ejecutar una sola pasada de S-12 fuera del timer por ruta/celda; validar dispatch efectivo. Un control obligatorio ausente o fallido termina con código distinto de cero y sesión no aceptada. No se descartan muestras por carga de fondo observada.

El orden de familias es: `P_instr`; cualificación/medición de referencias y OMEGA para ggml y Torch; `P_Q4` por `m`; operador OMEGA concurrente 8P para `m=1`; `BW_DRAM_read_8P` load-only; cálculo de veredictos al final. Dentro de las familias, cada bloque usa orden aleatorizado preregistrado con semilla congelada antes de abrir datos. Las comparaciones de referencia/OMEGA son apareadas; no se cambia el orden, ruta ni semilla tras observar muestras.

## Estadística y criterios

- `N=31` muestras medidas y `warmup=5` por serie. Se informan mediana, IQR, MAD, mínimo y máximo. Ninguna muestra se descarta retrospectivamente.
- Calibración previa solo determina unidades/repeticiones y no entra en las estadísticas. La política de MD/35 requiere objetivo de calibración de al menos 150 ms y muestras de al menos 100 ms. Las llamadas/repeticiones quedan fijadas antes de las 31 muestras; una muestra inferior a 100 ms invalida la serie, conserva los crudos y permite únicamente el reintento técnico preregistrado duplicando unidades, sin inspeccionar throughput.
- Por muestra se calcula `P=MAC útiles/tiempo`. En 8P, `P` usa MAC totales divididos por tiempo de pared del equipo, no suma de singles. Se calcula `G_ggml=mediana(P_OMEGA,r/P_ref,r)` y `G_Torch=mediana(P_OMEGA,r/P_Torch,r)` por celda y frontera.
- Gate de competencia: cada celda de ggml `G_ggml ≥ 0,90`; cada celda Torch `G_Torch ≥ 1,20`; `m={1,4,8,16}` en cada P-core individual y en 8P. No hay compensación entre celdas.
- Margen físico: `D'_hot=0,5625·P_OMEGA,hot,8P(1)/BW_DRAM,read,8P ≥ 2,00`. El denominador es lectura secuencial load-only, con working set por worker mayor que LLC, buffers pre-touched, ocho P-cores, sin stream de stores y medida en la misma sesión.
- Techo de instrucción: medir cadenas independientes del primitivo real `vpdpbusd` en `{1,2,4,8,10,12,14}`; exigir plateau de 12 frente a 14 dentro del 3 % y duplicación de desenrollado dentro del 3 %. Guardar disassembly y comprobar ausencia de spills/cargas en el bucle aritmético. Sin plateau, `P_instr=UNQUALIFIED`; informar fracción del kernel respecto al plateau, sin usarla como gate adicional.
- La referencia ggml cualificada más rápida se determina por su propia mediana dentro del conjunto congelado, antes de calcular ratios. Cada resultado de selección, veredicto y criterio queda trazado a las celdas de entrada.

## S-12, PAGE y salida

La referencia común conserva `r64`, `S` y `B` por salida. Se comprueba `up(|y-r64|) ≤ B`, o salida exactamente cero si `S=0`; se exigen finitud, reconstrucción exacta de representación y dots enteros exactos de las primitivas reales. `EXACT_GROUP_DOT_ISOLATION`, barrido de coordenadas y pruebas negativas son controles QA fuera del timer. Para S-17, Q1 compara bit a bit las escalas FP32 efectivas con FP16 canónico y recalcula las sumas INT32; mutaciones independientes de un byte en ambos planos deben rechazarse por el control exacto, junto con la copia FP16. Q2 exige `W=294912`, `X=672·m`, `Y=4096·m`, `dots=65536·m` en cada registro PAGE antes de calcular cobertura y exige la cobertura esperada. La observación de dispatch, comprobaciones por llamada, `OUT_WRITE`, reconstrucciones, callbacks, prints y validación no aparecen en la región cronometrada.

El JSON y el log oficiales deben conservar por ruta, celda, `m`, core, frontera y muestra: identificadores y hashes; semilla/orden; estado de afinidad y páginas; N muestras; estado de validación; mediana, IQR, MAD, min y max; selector de referencia; veredictos por gate; y razones de invalidez. Los campos de tiempo y rendimiento son nulos en modo dry-run. Los datos de calibración se registran aparte, nunca como muestras.

## Cierre e aislamiento

Antes de iniciar se cierran Chrome y herramientas no esenciales; se registra inventario de procesos y carga observada. No se cierran procesos desde el launcher. La sesión oficial conserva logs crudos, comando exacto, versión/build, DLL/runtimes cargados, entorno de hilos, topología, afinidades, frecuencia efectiva solo si una herramienta cualificada la proporciona, y hashes del paquete. Los datos se derivan una vez finalizadas todas las series; no hay filtros retrospectivos ni reruns científicos automáticos.

## Ambigüedades abiertas para Sol

1. El documento combina orden aleatorizado y orden preregistrado; debe fijarse si se usa una permutación aleatoria con semilla fija por bloque o una secuencia determinista alternada.
2. No queda definido cómo expresar el estadístico de dispersión en el gate de selección de la referencia más rápida ni el desempate cuando dos medianas coinciden; se propone mediana y desempate por nombre congelado, pendiente de Sol.
3. El objetivo de calibración de 150 ms y el mínimo de muestra de 100 ms conviven con la noción de bloque común Torch `R`; falta fijar `R` común entre candidatas y frontera cuando una llamada tiene costes muy distintos.
4. El preflight de afinidad por cada P-core requiere el mapa topológico de la máquina en la sesión; no se presume que los índices de CPU lógico sean consecutivos.
5. El dry-run QA de las referencias ejecuta controles exhaustivos en procesos separados y su disponibilidad de large pages depende del token actual. Un fallo de recurso produce HOLD, no una ruta de 4 KiB.
6. El esquema firmado y la identidad del autorizante de `AUTHORIZED.json` requieren decisión de Sol. Además, antes de habilitar la adquisición, falta integrar y revisar el recolector oficial; el launcher aquí permanece fail-closed y no se ha creado autorización.
7. La especificación no fija un tamaño concreto del buffer DRAM por worker más allá de superar LLC ni el tamaño de bloques de lectura; congelar geometría, sink y patrón antes de abrir muestras.

## Implementación verificada en esta revisión

- `--self-test`: estadística mediana/IQR/MAD, orden reproducible por semilla, mejor vara cualificada, cocientes de rendimiento sintéticos, `D′`, plateau 12/14, `rho` marginal y fronteras inclusivas/exclusivas. Incluye negativas de celda/frontera/core ausente, N≠31, cobertura PAGE parcial, S-12 fallido, hash AUTHORIZED incorrecto y semilla ausente. Salida marcada `SYNTHETIC`.
- `--timer-smoke`: QPC, calibración de `TIMED_REGION_MIN=100 ms` y afinidad comprobada con un bucle entero trivial. Toda duración está marcada `DUMMY` y excluida de cualquier resultado oficial.
- `--official`: exige ambos hashes de autorización; sin autorización se detiene antes de preparar o iniciar trabajo de medición. No ejecutado.

La puerta de 12 frente a 14 usa diferencia relativa simétrica `|p12-p14|/((p12+p14)/2)`. Esta es solo la convención del fixture sintético; confirmar con Sol antes de usarla como regla oficial si la lectura de “dentro del 3 %” debe ser otra.

