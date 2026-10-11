# Freeze de referencia Torch CPU â€” T0P-Q4-RESIDENCY

## Cambio de esta revisiÃ³n

El adaptador consume las 20 celdas canÃ³nicas de `T0P/common/data` (M=1,4,8,16 por cinco fixtures) y usa el generador, la referencia INT32/double y el verificador comunes. El formato serializado, hashes y conversiones ggml/OMEGA estÃ¡n documentados en [common/README.md](../common/README.md). El test ejecutable del verificador acepta una salida vÃ¡lida y rechaza NaN, Inf y una salida finita fuera de tolerancia.

Se evaluaron sin cronÃ³metro `at::mm`, `at::matmul`, `at::linear`, `at::mv` para M=1 y la ruta explÃ­cita `at::mkldnn_linear`. El detalle celda a celda estÃ¡ en `adapter/verify_common_all.txt`. Las candidatas densas tienen errores fuera de tolerancia en la mayorÃ­a de fixtures; por ello quedan `NOT_COMPARABLE` para esas celdas. MÃ¡ximo observado global: absoluto 1.28125, relativo 0.00421186945661 (el relativo excluye referencias exactamente cero; los errores absolutos de esas referencias sÃ­ cuentan). Los mÃ¡ximos por candidata y M estÃ¡n en el log y el JSON. No se selecciona una candidata de rendimiento a partir de estos resultados.

## Torch, precisiÃ³n y concurrencia

Se usa el paquete instalado Torch 2.11.0+cu128 solo con tensores CPU. Antes de ATen se fijan `OMP_NUM_THREADS=1`, `MKL_NUM_THREADS=1`, `OMP_DYNAMIC=FALSE`, `MKL_DYNAMIC=FALSE`, intra-op=1 e inter-op=1; el log confirma los seis valores efectivos. El proceso de validaciÃ³n es un Ãºnico hilo y cada celda estÃ¡ protegida por `NoGradGuard`; no crea worker threads.

Se leyÃ³ el `ATen/Context.h` del venv. La API `setFloat32Precision` fija y el getter confirma `generic/all=ieee`, `mkldnn/matmul=ieee`, `cuda/matmul=ieee`. TambiÃ©n se registran `float32MatmulPrecision`, TF32 CuBLAS/CuDNN/oneDNN, controles FP16/BF16 de reducciÃ³n expuestos por ese header y autocast CPU/CUDA. Los valores efectivos constan en el log.

Para `from_blob(...).clone()`, las direcciones que se consultan y registran son las de los clones usados realmente: X, W `[O,I]` y W transpuesto contiguo `[I,O]`. Para cada tensor se registra direcciÃ³n, tamaÃ±o, alineaciÃ³n, regiÃ³n de memoria, estado/tipo/protecciÃ³n y allocation base; la asignaciÃ³n observada es privada, comprometida y PAGE_READWRITE. W y Wt son representaciones persistentes de 2 MiB cada una por celda; X es dinÃ¡mico de 2 KiB, 8 KiB, 16 KiB o 32 KiB.

## Ruta oneDNN y atribuciÃ³n

La ruta explÃ­cita sÃ­ ejecuta `aten::mkldnn_linear` y su resultado se convierte con `to_dense()` fuera de cualquier cronÃ³metro. Se preparan X y W en layout MKLDNN antes de ejecutar el operador. Los valores se contrastan con el verificador comÃºn. ATen informa literalmente `Cannot access data pointer of Tensor that doesn't have storage` al intentar inspeccionar la asignaciÃ³n opaca empaquetada; no es posible confirmar sus pÃ¡ginas/asignador con la API instalada. Queda `NOT_COMPARABLE_PAGE_POLICY`, aunque la operaciÃ³n y su dispatch estÃ¡n disponibles. No se interpreta `hasMKLDNN` como prueba de dispatch.

Se instalÃ³ un callback global `RecordFunction` fuera de cualquier cronÃ³metro. Evidencia registrada por candidata: `mm â†’ aten::mm`; `matmul â†’ aten::matmul, aten::mm`; `linear â†’ aten::linear, aten::matmul, aten::mm`; `mv â†’ aten::mv, aten::addmv_`; oneDNN explÃ­cito â†’ `aten::mkldnn_linear` (mÃ¡s la conversiÃ³n `aten::to_dense`). No se afirma quÃ© biblioteca BLAS interna ejecuta cada `aten::mm`.

## Dependencias y compilaciÃ³n

El proceso enumerÃ³ 37 mÃ³dulos cargados usando `EnumProcessModules` y `GetModuleFileNameEx`; el manifiesto registra las rutas absolutas, tamaÃ±o en disco, tamaÃ±o mapeado y SHA-256 de cada uno, incluidos `torch_cpu.dll`, `c10.dll`, `libiomp5md.dll`, runtimes de MSVC y DLL CUDA cargadas por la distribuciÃ³n. No todos se usan por los tensores CPU; se incluyen porque el proceso los cargÃ³.

Build MSVC x64 con SDK 10.0.26100.0 y toolset 14.44.35207 mediante `adapter/build.cmd`. El build enlaza las fuentes comunes y Torch; el generador independiente se construye con `common/build.cmd`. Hashes de los binarios, fuentes, 20 celdas, log y mÃ³dulos: `SHA256SUMS_ref_torch.txt`; el script de freeze verificÃ³ todas las entradas contra disco. Todo resultado compilado estÃ¡ dentro de `T0P/common` o `T0P/ref_torch`.

## Estado y ambigÃ¼edades abiertas

No se hizo benchmark cronometrado. La prueba corriÃ³ 20 celdas y registrÃ³ errores de todas las candidatas disponibles; la unidad del verificador pasÃ³. Las rutas densas permanecen registradas, pero sus celdas fallidas no son comparables bajo la tolerancia de aceptaciÃ³n. El oneDNN explÃ­cito permanece fuera de candidatas comparables por la asignaciÃ³n/polÃ­tica de pÃ¡ginas opaca.

Siguen abiertas la inspecciÃ³n externa de pÃ¡ginas del almacenamiento MKLDNN; la elegibilidad de Torch FP32 para una futura vara tras los fallos numÃ©ricos observados; y la afinidad/ejecuciÃ³n del futuro harness de varios workers. No se resuelven con una mediciÃ³n cronometrada. `ref_ggml` y `T0\SHA256SUMS.txt` no se modificaron.

## ReevaluaciÃ³n S-12 (2026-10-11)

Se aplicÃ³ `reference64()` y `verify_s12()` a 36 celdas (nueve fixtures Ã— cuatro M). `mm`, `matmul` y `linear` pasan S-12 en 36/36; `mv` en M=1 pasa 9/9. RepresentaciÃ³n FP32 efectiva X/W coincide exactamente con la desquantizaciÃ³n canÃ³nica y cada salida registra el operador recibido (`aten::mm`, `aten::matmul`/`aten::mm`, `aten::linear` y dispatch anidado, `aten::mv`/`aten::addmv_`). `mkldnn_linear` tiene salida numÃ©rica S-12 vÃ¡lida en 36/36 y dispatch `aten::mkldnn_linear`, pero el estado final es NOT_COMPARABLE por la polÃ­tica de pÃ¡ginas/alocaciÃ³n no inspeccionable del tensor opaco.

La tolerancia antigua se sigue calculando por celda; muchas filas la incumplen, sin gate. El log completo es `adapter/verify_common_all.txt`. No se cronometrÃ³. Los fixtures S-12 nuevos estÃ¡n congelados en `../common/NEW_FIXTURES_FROZEN.json` antes de QA.

Abierto: el fixture `single_coordinate` actual prueba la posiciÃ³n 0 y no recorre las 512; la cuestiÃ³n de la variante casi exacta sigue remitida a Sol como ambigÃ¼edad de definiciÃ³n aunque esta construcciÃ³n cumple S/|r|â‰¥1e5. `mkldnn_linear` conserva el impedimento de inspecciÃ³n fÃ­sica de pÃ¡ginas.


