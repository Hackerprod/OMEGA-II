# Datos comunes T0P Q4

`common.hpp/.cpp` define el generador determinista compartido, el archivo de celda canónico, conversiones sin recuantizar y la referencia/verificador independiente. `build.cmd` compila `generate_cells.exe`; ejecutar desde la raíz del repositorio con `T0P\common\generate_cells.exe T0P\common\data` escribe una celda por (M, fixture). Los archivos `.bin` son datos canónicos, no temporales.

## Serialización y hash

SHA-256 se calcula sobre estos bytes, en este orden, todos los enteros little-endian: ASCII `T0P-CELL-v1` (sin NUL); `u32 O=1024`, `u32 I=512`, `u32 M`, `u32 fixture_id`; códigos Q4 `int8` en orden `[o][i]`; escalas de peso como `u16 IEEE-754 binary16` en orden `[o][g]`; códigos de activación `int8` en `[m][i]`; escalas de activación `u16 binary16` en `[m][g]`. El valor FP32 de cada escala se obtiene decodificando exactamente esos mismos 16 bits. El archivo `.bin` contiene exactamente ese flujo; su SHA-256 es el hash canónico impreso para la celda. No se serializan padding, structs nativos, direcciones ni texto dependiente de locale.

La secuencia aleatoria es SplitMix64: estado inicial `0x6a09e667f3bcc909 XOR (M<<24) XOR (fixture_id<<40)`, incremento `0x9e3779b97f4a7c15`, seguido de las mezclas definidas literalmente en `common.cpp`. El flujo llena primero códigos de peso, códigos de activación, escalas de peso y escalas de activación; las reglas explícitas de fixture sobrescriben después los códigos aplicables.

Pesos usan 32 códigos signed en `[-8,7]` por grupo; activaciones conservan el byte `int8` original. Fixtures: `uniform`, `mixed_cancellation` (cada grupo tiene peso constante y activación alterna ±11, por lo que el dot entero de cada grupo es exactamente cero; el signo de peso varía entre grupos), `varied_scales` (mantisas no triviales), `sub127` (todos los grupos tienen máximo absoluto ≤40), y `extremes` (pesos −8/+7 y activaciones −127/+127).

## Adaptación a referencias

* Para `block_q4_0` de ggml, copiar la escala binary16 original a `d`; para cada `j=0..15`, guardar `qs[j] = (q[j]+8) | ((q[j+16]+8)<<4)`. Es el orden de nibbles Q4_0 de ggml. Para `block_q8_0`, copiar la escala binary16 original a `d` y los 32 bytes INT8 originales a `qs`. No calcular `amax`, no dividir ni cuantizar de nuevo.
* Para el formato de `q4_kernel.hpp`, el grupo ocupa exactamente 18 bytes: `byte[j/2]` contiene `(q[j]&15)` en nibble bajo para j par y nibble alto para j impar; bytes 16..17 son la escala binary16 original little-endian. La activación sigue siendo el vector INT8 original y la escala por grupo su binary16 decodificado a FP32.
* `ggml_q4_0`, `ggml_q8_0` y `omega_q4_18` implementan esos mapeos. Cada conversión conserva exactamente códigos y bits de escala; no recuantiza.

La aceptación común obtiene cada dot de grupo en INT32, aplica `dot * scale_x * scale_w` y acumula grupos en double antes de convertir a FP32 de salida. Se exige que escalas, referencia, salida y error sean finitos y que `|y-y_ref| <= 1e-5 + 1e-5*|y_ref|`. `verifier_unit_tests()` cubre una salida válida, NaN, Inf y un error fuera de tolerancia.

El resumen de error absoluto incluye también referencias nulas. El máximo relativo se calcula solo para referencias no nulas, ya que el error relativo no está definido cuando `y_ref == 0`; los casos nulos siguen sometidos a la tolerancia absoluta del verificador.
