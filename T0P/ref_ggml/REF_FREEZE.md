# Congelación de referencia ggml

## Fuente y build

- Proyecto: `ggml-org/llama.cpp`, v0.6.0, commit `d81235049384534c167caea52b85a694f6103d14`; SHA256 del ZIP: `54D8E1F79B4A8BC0EF70B2ECE968C0A8AFE8030EDB8257D64DEDFB1169983414`.
- Se reutilizaron `build_llama/bin/ggml-cpu.dll` y `ggml-base.dll`; no se recompiló ggml. CMake registrado: Release/Ninja, MSVC 19.44.35229.0, `GGML_NATIVE=OFF`, `GGML_AVX2=ON`, `GGML_AVX_VNNI=ON`, CUDA/tests/ejemplos/tools OFF.
- `adapter/build.cmd` compila el adaptador y el archivo upstream `arch/x86/repack.cpp` sin modificarlo, para poder llamar los kernels repack que el DLL no exporta. El kernel conserva la aritmética upstream. El build reutilizado de ggml sigue siendo el registrado.

## Entradas comunes y candidatas

Se cargaron las 20 celdas canónicas `T0P/common/data` (fixtures `uniform`, `mixed_cancellation`, `varied_scales`, `sub127`, `extremes`; M=1,4,8,16). Antes de usarlas, el SHA-256 del archivo se comparó con el hash regenerado por `t0common::generate`. Todos coincidieron. Referencia: `t0common::reference`, productos por grupo INT32 y acumulación double.

- **A, plana:** `ggml_mul_mat`, pesos Q4_0 en buffer plano; Q4_0 CPU trait `vec_dot`, una hebra. Se registró la dirección y RVA del callback de `ggml_get_type_traits_cpu(Q4_0)` dentro del `ggml-cpu.dll` cargado. En el fuente congelado el trait apunta a `ggml_vec_dot_q4_0_q8_0` y AVX-VNNI usa `vpdpbusd`.
- **B, repack:** llamada directa a `ggml_gemv_q4_0_8x8_q8_0` para M=1 y `ggml_gemm_q4_0_8x8_q8_0` para M=4,8,16. Se ejecutaron las 4 formas en los 5 fixtures.
- Pesos: copia de escala FP16 y transformación de `block_q4_0x8` con interleave y XOR `0x8888888888888888`; reconstrucción de códigos/escalas exacta bit a bit.
- Activaciones: copia sin pérdida de INT8/escalas FP16 a `block_q8_0x4`; M=1 usa Q8_0 original. No hay recuantización, ni Q8→F32→Q8. No se alteró upstream.

**Despacho:** B usa llamada directa a las direcciones enlazadas de las funciones upstream; nombre, dirección y M aparecen por celda en `adapter/qa_output.txt`. Para A se observó el callback real del trait y su RVA (`0xc8ad0` en esa ejecución). Limitación: `ggml-cpu.dll` no exporta el símbolo y no hay PDB; no se pudo confirmar su nombre con resolución simbólica ni breakpoint. La identificación nominal se apoya en la asignación del trait del fuente local. Los kernels no exponen sus productos INT32 por grupo; la referencia común sí los calcula, pero la igualdad interna no se capturó por separado.

## QA numérico sin cronometraje

Se ejecutaron 40 celdas ruta-fixture-M. Las entradas, referencia y salidas fueron finitas. `adapter/QA_RESULTS.md` contiene por celda error absoluto máximo, error relativo máximo, error normalizado `max |y-y_ref| / Σ_f |x_f·w_f|`, conteo de salidas fuera de tolerancia, hash canónico y función ejecutada. También están las líneas completas de ejecución en `adapter/qa_output.txt`.

Se informa la tolerancia de MD/35 `|y-y_ref| ≤ 1e-5 + 1e-5|y_ref|`, sin decidir si gobierna. Varias celdas uniform, varied_scales y sub127 no la pasan; mixed_cancellation y extremes sí. Los errores permanecen reportados sin escoger criterio ni descartar celdas. No se midió rendimiento.

## Fronteras y hebras

El adaptador separa `prepare()` y `prepare_activations()` de `run_once_*()`. La preparación estática cubre hash/carga, conversión y repack de pesos, contexto, grafo, tensores, scratch y salida. La preparación dinámica empaqueta las activaciones Q8 sin pérdida por aplicación. Para frontera de entradas preparadas queda fuera del kernel; para frontera dinámica se cobra preparación dinámica más kernel. `run_once_*` reutiliza estructuras y no hace asignaciones del adaptador. La QA preparó una vez las entradas antes de cada ejecución; no se hizo benchmark.

El proceso recibió `OMP_NUM_THREADS=1` y `OMP_DYNAMIC=FALSE` antes de iniciar; el adaptador los verificó. Grafo: `n_threads=1`. No se cambió configuración del sistema.

## Módulos cargados

`EnumProcessModules` registró 17 módulos con ruta absoluta, tamaño y SHA-256; el inventario está en `adapter/QA_RESULTS.md` y `REF_FREEZE.json`, y sus hashes están en `SHA256SUMS_ref_ggml.txt`. Incluye `ggml-cpu.dll`, `ggml-base.dll`, `VCOMP140.DLL`, `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll`, `MSVCP140.dll` y dependencias del sistema. `ggml.dll` no fue cargado por este ejecutable y por eso no figura como módulo efectivo.

## Estado y ambigüedades

S-8…S-11 implementados para QA de la vara. Sin mediciones de rendimiento ni selección de ganador. Queda abierta la identidad runtime nominal del callback A (símbolo no exportado/PDB ausente), la captura de productos internos por grupo y cuál tolerancia gobierna. Los resultados de precisión no se interpretan para resolver esas decisiones. Hashes del manifiesto verificados al actualizarlo.

## Reevaluación S-12 (sin cronometraje)

Se ejecutaron 72 celdas canónicas (nueve fixtures × cuatro M × flat/repack) y 2.048 lotes de barrido (512 coordenadas × cuatro M × flat/repack). Las 72 pasan la cota S-12 con cero salidas fallidas; los errores máximos normalizados constan por fila en `adapter/QA_RESULTS.md`. La tolerancia antigua se calcula y registra como diagnóstico, no como gate. Los 20 hashes originales siguen intactos; las 16 celdas nuevas tenían hashes congelados antes de la ronda.

Representación: `exact_representation()` reconstruye desde `block_q4_0`, almacenamiento `block_q4_0x8` posterior a interleave/XOR 0x8888888888888888 y `block_q8_0x4`; los 36 controles por celda pasan en los dos almacenamientos. B ejecuta directamente GEMV para M=1 y GEMM para M=4,8,16; A ejecuta `ggml_mul_mat`, con evidencia del callback real del trait y su RVA. Ningún kernel expone sus dots INT32 por grupo y no se capturaron por separado; esta limitación impide certificar los productos internos antes del escalado. No se atribuye equivalencia exacta de esos productos a partir de la salida agregada.

La variante `cancellation_high` da S/|r|≈2049; `cancellation_extreme` usa cancelación exacta de dos grupos más un término de escala FP16 mínima y da S/|r|≈9.55e11. La interpretación de “casi exacta” sigue abierta a Sol por truncamiento de MD/38. `single_coordinate` recorre las 512 posiciones en 512 lotes por M y pasa con igualdad exacta. No se cambió upstream ni se hicieron benchmarks.



Límite de inspección de ggml: `ggml_mul_mat` entrega solo la salida final escalada desde el callback del trait y su símbolo no está exportado ni tiene PDB. Las funciones upstream repack enlazadas directamente, GEMV/GEMM, tampoco exponen el arreglo de dots INT32 por grupo. La API común `exact_group_dots()` existe y sus pruebas de oráculo pasan, pero no se pudo alimentar con resultados internos del kernel sin alterar upstream; por eso los productos exactos de los kernels quedan como limitación abierta.

## ESTADO GOBERNANTE S-13 (2026-10-11)

El detalle ejecutado está en `adapter/QA_RESULTS.md` y la salida en `adapter/qa_output.txt`. S-12, representación, 29.696 salidas exactas de cancelación por ruta y 2.375.680 dots aislados por ruta pasan. El objeto repack upstream actual contiene 218 `VPDPBUSD` (hash `f263ccfa407e36106e4f94bb2e66ae65fd5e0d7487f288a6198c7d36489eec38`). El callback plano se alcanzó solo en m=1; m=4,8,16 registran `reached=false`, por lo que el gate es FAIL y no se autoriza el freeze definitivo.

La frontera estática incluye peso/plano y repack estático, contexto, grafo, plan, scratch y salida. Para la frontera dinámica de entradas preparadas, plano copia la activación Q8_0 original al buffer reutilizado; repack añade empaquetado lossless `block_q8_0x4`. `run_once_*` no hace verificaciones QA, reconstrucciones, allocations del adaptador ni relleno NaN. Este adaptador es QA; no se hicieron mediciones.

`SeLockMemoryPrivilege` no aparece en `whoami /priv` ni en el token. Se registra HOLD de recurso; las asignaciones normales reales se comprobaron con `QueryWorkingSetEx` y `LargePage=false`. No se cambió ninguna política de seguridad.

Las declaraciones de las secciones previas que decían que callback, dots internos o familia de cancelación seguían abiertos pertenecen al HISTORIAL S-12. El bloque actual y `GOVERNING_S13` del JSON gobiernan.

## Correcci?n de estado gobernante S-13 ? cierre final

La corrida final alcanza el callback plano para m=1,4,8,16 (no solo m=1): m?dulo `ggml-cpu.dll`, SHA-256 `cfce3924a4de5ef82317089937edc1a4ae494515af1f1d9ee8ef05f6e921ba23`, RVA `0xc8ad0`, captura INT3/VEH durante `ggml_mul_mat`, retorno `base+0x455a`. El desensamblado de esa funci?n contiene `VPDPBUSD` (p. ej. RVA `0xc8baf`); el archivo est? en `adapter/ggml_cpu_disasm.txt`. El gate ejecutado pasa 80/80 celdas. El bloque S-13 anterior queda como HISTORIAL de una corrida intermedia.


E3 final: se a?adieron `prepare_static(..., Flat|Repack|QA)` y `prepare_dynamic(..., Flat|Repack|QA)`. Flat no crea pesos/activaciones repack; Repack construye block_q4_0x8 directamente de los c?digos can?nicos y empaqueta activaciones Q8_0x4 directamente de la celda; `QA` es el ?nico modo combinado. La comprobaci?n `STATIC_ROUTE_ISOLATION` pasa. La frontera uno son los buffers est?ticos/contexto/plan; la frontera dos es la preparaci?n din?mica propia de la candidata m?s su kernel. El adaptador se ejecuta solo como QA y no se cronometr?.

Comprobaci?n final E3: `STATIC_ROUTE_ISOLATION` y `DYNAMIC_ROUTE_ISOLATION` pasan en la corrida gobernante; el gate exige ambos.
