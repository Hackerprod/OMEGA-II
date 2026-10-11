# Congelación de referencia ggml

## Fuente
- Proyecto: `ggml-org/llama.cpp`; `v0.6.0` es una release estable de GitHub (`prerelease=false`, publicada 2026-10-05).
- Commit del tag verificado por API: `d81235049384534c167caea52b85a694f6103d14` (coincide con el ZIP registrado). El objeto annotated tag es `69ecae922d2b60c1fa8c590279f4e2099af75111`; el tag no tiene firma verificada.
- URL de descarga: `https://github.com/ggml-org/llama.cpp/archive/refs/tags/v0.6.0.zip`
- Archivo: `llama.cpp-v0.6.0-source-20261011-a1.zip`; SHA256 registrado `54D8E1F79B4A8BC0EF70B2ECE968C0A8AFE8030EDB8257D64DEDFB1169983414`.
- Árbol fuente `llama.cpp-0.6.0` y build `build_llama` estaban presentes. DLL e import libraries CPU se conservaron; no fue necesaria reextracción ni recompilación del proyecto.

## Build CPU
Release con Ninja incluido en Visual Studio BuildTools; MSVC 19.44.35229.0, toolset 14.44.35207. Opciones: `GGML_NATIVE=OFF`, `GGML_AVX2=ON`, `GGML_AVX_VNNI=ON`, `GGML_CUDA=OFF`, tests/ejemplos/tools OFF. CMake registró `/arch:AVX2`, `GGML_AVX2`, `GGML_FMA`, `GGML_F16C`, `__AVXVNNI__`, `GGML_AVX_VNNI`, `__BMI2__`, `GGML_BMI2`. DLL: `ggml-base.dll`, `ggml.dll`, `ggml-cpu.dll` en `build_llama/bin`; import libraries en `build_llama/ggml/src`.

## Adaptador y ruta efectiva
Adaptador mínimo: `adapter/main.cpp`, compilado como `adapter/adapter.exe`, enlazando las import libraries del build y `T0/src/q4_kernel.cpp`. Cada producto es una llamada a `ggml_mul_mat`, evaluada por `ggml_graph_compute_with_ctx(..., 1)` (una hebra).

Para m=1,4,8,16 se selecciona el type trait CPU de Q4_0 `ggml_vec_dot_q4_0_q8_0`, que llama al helper `mul_sum_i8_pairs_float`; en este build `__AVXVNNI__` habilita `mul_sum_us8_pairs_float` con `vpdpbusd` AVX-VNNI. El adaptador imprime detección runtime `AVX2=1`, `AVX-VNNI=1` en cada ejecución. Para m>1 se procesan varias filas de salida del operador, pero sigue siendo el vector dot por fila. `ggml_gemm_q4_0_8x8_q8_0` y repack están presentes en el árbol, pero no son seleccionados por esta ruta del grafo CPU ni por estos tamaños; no se afirma que se haya ejecutado GEMM.

## Formato y equivalencia
Se generan pesos/activaciones con `t0q4::generate_weights(O=1024,I=512,seed=0x23456789abcdef01)` y `generate_activations(M,I,seed=0x1020304050607080)`. Las escalas de activación son FP16-exactas y se verifica su round-trip. Conversión Q4 preserva `d` FP16 y reordena el almacenamiento local intercalado de pares a ggml Q4_0: nibble bajo q[0..15]+8, nibble alto q[16..31]+8. Q8 copia los 32 int8 y guarda la escala FP16 exacta. No se recuantiza.

Referencia independiente: suma entera exacta de los 32 productos de cada grupo, convertida a float y multiplicada por las dos escalas; tolerancia `1e-5 + 1e-5*abs(y_ref)`. Ejecución completada sin cronometraje:

| m | Máximo error absoluto | Máx. error/tolerancia | Discrepancias |
|---:|---:|---:|---:|
| 1 | 0 | 0 | 0 |
| 4 | 0 | 0 | 0 |
| 8 | 0.001953125 | 0.0103252959 | 0 |
| 16 | 0.001953125 | 0.0103252959 | 0 |

La discrepancia de formato detectada al implementar fue el orden de nibbles: el archivo de congelación inicial describía incorrectamente pares low/high adyacentes para ggml Q4_0. El adaptador aplica el layout real de ggml y la equivalencia pasa en todos los casos.

## Límites y ambigüedades
- Los resultados caracterizan `ggml_mul_mat` en este commit/build/CPU; no comparan el GEMM repack porque el grafo no lo seleccionó.
- Runtime informó AVX-VNNI y build definió `__AVXVNNI__`; la ruta observada en el fuente es el helper VNNI. El tag es release estable, aunque su tag anotado no está firmado.
- No se hicieron benchmarks ni mediciones de rendimiento cronometradas.
- SHA256 de fuente y binario del adaptador y archivos registrados: `SHA256SUMS_ref_ggml.txt`.
