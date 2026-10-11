# Freeze de referencia Torch CPU — T0P-Q4-RESIDENCY

## Cambio de esta revisión

El adaptador consume las 20 celdas canónicas de `T0P/common/data` (M=1,4,8,16 por cinco fixtures) y usa el generador, la referencia INT32/double y el verificador comunes. El formato serializado, hashes y conversiones ggml/OMEGA están documentados en [common/README.md](../common/README.md). El test ejecutable del verificador acepta una salida válida y rechaza NaN, Inf y una salida finita fuera de tolerancia.

Se evaluaron sin cronómetro `at::mm`, `at::matmul`, `at::linear`, `at::mv` para M=1 y la ruta explícita `at::mkldnn_linear`. El detalle celda a celda está en `adapter/verify_common_all.txt`. Las candidatas densas tienen errores fuera de tolerancia en la mayoría de fixtures; por ello quedan `NOT_COMPARABLE` para esas celdas. Máximo observado global: absoluto 1.28125, relativo 0.00421186945661 (el relativo excluye referencias exactamente cero; los errores absolutos de esas referencias sí cuentan). Los máximos por candidata y M están en el log y el JSON. No se selecciona una candidata de rendimiento a partir de estos resultados.

## Torch, precisión y concurrencia

Se usa el paquete instalado Torch 2.11.0+cu128 solo con tensores CPU. Antes de ATen se fijan `OMP_NUM_THREADS=1`, `MKL_NUM_THREADS=1`, `OMP_DYNAMIC=FALSE`, `MKL_DYNAMIC=FALSE`, intra-op=1 e inter-op=1; el log confirma los seis valores efectivos. El proceso de validación es un único hilo y cada celda está protegida por `NoGradGuard`; no crea worker threads.

Se leyó el `ATen/Context.h` del venv. La API `setFloat32Precision` fija y el getter confirma `generic/all=ieee`, `mkldnn/matmul=ieee`, `cuda/matmul=ieee`. También se registran `float32MatmulPrecision`, TF32 CuBLAS/CuDNN/oneDNN, controles FP16/BF16 de reducción expuestos por ese header y autocast CPU/CUDA. Los valores efectivos constan en el log.

Para `from_blob(...).clone()`, las direcciones que se consultan y registran son las de los clones usados realmente: X, W `[O,I]` y W transpuesto contiguo `[I,O]`. Para cada tensor se registra dirección, tamaño, alineación, región de memoria, estado/tipo/protección y allocation base; la asignación observada es privada, comprometida y PAGE_READWRITE. W y Wt son representaciones persistentes de 2 MiB cada una por celda; X es dinámico de 2 KiB, 8 KiB, 16 KiB o 32 KiB.

## Ruta oneDNN y atribución

La ruta explícita sí ejecuta `aten::mkldnn_linear` y su resultado se convierte con `to_dense()` fuera de cualquier cronómetro. Se preparan X y W en layout MKLDNN antes de ejecutar el operador. Los valores se contrastan con el verificador común. ATen informa literalmente `Cannot access data pointer of Tensor that doesn't have storage` al intentar inspeccionar la asignación opaca empaquetada; no es posible confirmar sus páginas/asignador con la API instalada. Queda `NOT_COMPARABLE_PAGE_POLICY`, aunque la operación y su dispatch están disponibles. No se interpreta `hasMKLDNN` como prueba de dispatch.

Se instaló un callback global `RecordFunction` fuera de cualquier cronómetro. Evidencia registrada por candidata: `mm → aten::mm`; `matmul → aten::matmul, aten::mm`; `linear → aten::linear, aten::matmul, aten::mm`; `mv → aten::mv, aten::addmv_`; oneDNN explícito → `aten::mkldnn_linear` (más la conversión `aten::to_dense`). No se afirma qué biblioteca BLAS interna ejecuta cada `aten::mm`.

## Dependencias y compilación

El proceso enumeró 37 módulos cargados usando `EnumProcessModules` y `GetModuleFileNameEx`; el manifiesto registra las rutas absolutas, tamaño en disco, tamaño mapeado y SHA-256 de cada uno, incluidos `torch_cpu.dll`, `c10.dll`, `libiomp5md.dll`, runtimes de MSVC y DLL CUDA cargadas por la distribución. No todos se usan por los tensores CPU; se incluyen porque el proceso los cargó.

Build MSVC x64 con SDK 10.0.26100.0 y toolset 14.44.35207 mediante `adapter/build.cmd`. El build enlaza las fuentes comunes y Torch; el generador independiente se construye con `common/build.cmd`. Hashes de los binarios, fuentes, 20 celdas, log y módulos: `SHA256SUMS_ref_torch.txt`; el script de freeze verificó todas las entradas contra disco. Todo resultado compilado está dentro de `T0P/common` o `T0P/ref_torch`.

## Estado y ambigüedades abiertas

No se hizo benchmark cronometrado. La prueba corrió 20 celdas y registró errores de todas las candidatas disponibles; la unidad del verificador pasó. Las rutas densas permanecen registradas, pero sus celdas fallidas no son comparables bajo la tolerancia de aceptación. El oneDNN explícito permanece fuera de candidatas comparables por la asignación/política de páginas opaca.

Siguen abiertas la inspección externa de páginas del almacenamiento MKLDNN; la elegibilidad de Torch FP32 para una futura vara tras los fallos numéricos observados; y la afinidad/ejecución del futuro harness de varios workers. No se resuelven con una medición cronometrada. `ref_ggml` y `T0\SHA256SUMS.txt` no se modificaron.
