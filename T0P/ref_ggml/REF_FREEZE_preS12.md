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
