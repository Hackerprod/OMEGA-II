# Congelación de referencia LibTorch CPU — T0P-Q4-RESIDENCY

## Selección

Se usa el LibTorch ya instalado en `C:\Projects\OMEGA\.venv_cuda\Lib\site-packages\torch`: versión 2.11.0+cu128. Tiene `torch\include`, `torch\lib`, `torch_cpu.dll/.lib`, `torch.lib` y `c10.lib`. Aunque el paquete incluye CUDA, ATen CPU es operativo: el ejecutable C++ enlazó con MSVC 14.44.35207 y ejecutó `at::matmul` en CPU. No se descargó un archivo externo; URL y SHA256 de descarga: N/A. No se instaló ni modificó nada en el venv.

## Contrato implementado

Dimensiones O=1024, I=512, M=1,4,8,16; semilla de pesos `0x544F505F515034`, de activaciones `0x544F505F414354`, mismas funciones `t0q4::generate_weights/generate_activations` de `T0\src\q4_kernel.cpp`. FP32: se aplican todas las escalas de grupo a Q4 y a INT8 antes del producto. El peso se desquantiza y transpone una vez a un tensor contiguo estático `[I,O]`; cada aplicación crea X dinámico `[M,I]`. No se llama a una API privada de prepack: oneDNN/MKL puede empaquetar internamente según despacho.

La operación es `at::matmul(X,Wt)`, sin gradientes (`at::NoGradGuard`), `at::set_num_threads(N)` configurable por CLI y `at::set_num_interop_threads(1)`. La API C++ disponible `at::globalContext().setFloat32MatmulPrecision("highest")` establece precisión máxima FP32; no se usa BF16/TF32 deliberadamente. El backend compilado y observado declara BLAS=MKL, USE_MKL=ON, USE_MKLDNN=ON, capacidad CPU AVX2, `hasMKLDNN=1`, MKL 2025.3 y oneDNN/MKL-DNN 3.10.2. El paquete es CUDA-enabled, pero estas ejecuciones usan tensores CPU. Configuración íntegra en `adapter\verify_m4.txt`.

## Equivalencia (sin cronómetro)

Se ejecutó cada M con N=4 para verificar la instalación; no son datos de rendimiento. Una referencia escalar independiente extrae cada nibble, calcula cada producto de grupo en INT32 y suma `dot * scale_x * scale_w` en double. Resultado: máximos absolutos 0, máximos relativos 0, fallos 0 para M=1,4,8,16; tolerancia `|y-y_ref| <= 1e-5 + 1e-5|y_ref|`. La ruta FP32 no promete identidad bit a bit en general. No se usa `_int_mm`. La igualdad exacta observada corresponde a estos fixtures concretos.

## Rutas candidatas y selección futura

Candidatas que conservan la matemática FP32: `at::mm(X,Wt)`, `at::matmul(X,Wt)` (la implementación congelada), y `at::linear(X,W)` con W contiguo `[O,I]` y bias ausente. Para m=1, además `at::mv` fila por fila solo como candidata de diagnóstico si la celda se define con esa forma; no sustituye una ruta matricial para m>1. MKLDNN explícito podría evaluarse solo si se documenta conversión/layout y se verifica equivalencia; no forma parte de este freeze. `_int_mm` no es candidata porque no incorpora las escalas por grupo. `torch.compile` no está preregistrado.

Regla antes de la campaña: «la más rápida cualificada por celda», exigiendo contrato/equivalencia y ausencia de reducción de precisión. Sin benchmark en esta tarea, ninguna ruta ha sido ordenada ni elegida por velocidad.

## Compilación y estado

`adapter\build.cmd` usa vcvars64, SDK 10.0.26100.0, MSVC 14.44.35207; `/O2 /std:c++17 /arch:AVX2 /fp:precise /EHsc /MD /DNDEBUG`; enlaza `torch.lib torch_cpu.lib c10.lib`. Ejecuciones de verificación con 4 hilos; los hilos definitivos por celda quedan por fijarse/controlarse para la futura comparación (N hilos configurable). No se hicieron mediciones cronometradas ni R llamadas de rendimiento.

SHA256 (bytes en disco):

- `adapter\ref_torch.cpp`: a7ae81986a27fe42b912b0f8a1e91c60724081a25bea082be00f442cacb95180
- `adapter\ref_torch.exe`: 09a56c447fb3b7bd2102572ace622b07664167875668442f45412e14fb240a66
- `adapter\build.cmd`: 8c004cd3d478c8eceeee52a1e44fe121128ea84252c640c034751f3d6199e69a
- Fuente externa usada `T0\src\q4_kernel.cpp`: 3955e7bc266a0848d3e0c2c99f3e22006b2c47280d63f576ab89efde2e86b045
- Header externo usado `T0\src\q4_kernel.hpp`: 22e7a71c88c66c82e9658f964213f184f181d836cf688b579678756e8a590a7b

Ambigüedades que quedan abiertas: si la congelación exige una distribución CPU-only pese a que el paquete CUDA ejecuta el backend CPU; si «prepacking» requiere un formato oneDNN persistente explícito en vez del peso FP32 transpuesto contiguo; cuál API candidata gobernará cada celda; lista final de N, afinidad/P-cores y R para el futuro bloque cronometrado; y política exacta para determinar en runtime si el dispatch de una forma usa MKL u oneDNN cuando el build contiene ambos. No se resuelven aquí.

