# Datos comunes T0P Q4

`common.hpp/.cpp` contiene generador determinista, formato canónico, conversiones ggml/OMEGA, referencia FP64 S-12 y diagnósticos. `reference64()` entrega r64, S, S+ y B por elemento; `reference()` conserva FP32 solo como diagnóstico. Las reglas de la referencia S-12, el redondeo dirigido, las APIs de control exacto y fixtures nuevos están descritos más abajo.

## Serialización y SHA-256

SHA-256 se calcula sobre: ASCII `T0P-CELL-v1`; u32 little-endian O=1024, I=512, M y fixture_id; códigos Q4 int8 [o][i]; escalas de peso FP16 [o][g]; códigos de activación int8 [m][i]; escalas FP16 [m][g]. FP32 de escala es decodificación exacta de sus 16 bits.

SplitMix64 parte de `0x6a09e667f3bcc909 XOR (M<<24) XOR (fixture_id<<40)`. Las 20 celdas de `uniform`, `mixed_cancellation`, `varied_scales`, `sub127` y `extremes` se conservan byte a byte.

## S-12

Por salida y término j, `t_j=(qW_j*sW_g)(qX_j*sX_g)`, `S=sum |t_j|` se calcula desde los códigos/escalas canónicos en double. `S+=up(S64/(1-gamma514(64)))`; `B=up((gamma514(32)+gamma18(64))*S+)`, con `u32=2^-24`, `u64=2^-53`. Cada operación del límite usa redondeo dirigido (`FE_UPWARD`, y redondeo inferior para la resta del denominador); el error de salida también se eleva al comparar. La regla es `up(|double(y)-r64|)<=B`. Si S=0, la salida debe ser ±0 exacto. No hay piso absoluto. La tolerancia anterior `1e-5+1e-5|y_ref|` se calcula solo como diagnóstico.

`verify_s12()` devuelve índices y datos por cada salida fallida: (m,o), r64, y, error, S, B, error/B y S/|r|. `exact_representation()` acepta un callback de reconstrucción desde almacenamiento efectivo; `exact_group_dots()` compara callback de ruta con oráculo INT32 independiente. La validación rechaza dimensiones/tamaños incoherentes, códigos no válidos y escalas no finitas o desincronizadas de sus bits.

## Fixtures

`NEW_FIXTURES_FROZEN.json` congela semillas, construcciones y SHA-256 de las 16 celdas añadidas; su SHA-256 es `NEW_FIXTURES_FROZEN.sha256`. Se generó antes de ejecutar QA sobre esas celdas. `zero_input` exige salida exacta cero. `single_coordinate` hace una salida entera exactamente representable al activar la coordenada 0; el barrido de coordenadas 0..511 pasa con igualdad exacta en 512 lotes por M. `cancellation_high` deja S/|r|=2049; `cancellation_extreme` deja r=2^-24 y S/|r|≈9.55e11. S-13 fija “casi exacta” como r distinto de cero y S/|r|≥1024.

## Conversiones externas

`ggml_q4_0()` preserva bits FP16 y traslada q a nibbles con bias +8; `ggml_q8_0()` copia escalas FP16 e INT8; `omega_q4_18()` copia nibbles signed y escala FP16. No se recalcula ni se requantiza escala.


## Estado gobernante S-13

El manifiesto congelado original y su SHA-256 no se modifican. `NEW_FIXTURES_FROZEN_addendum1.json` (SHA-256 en el `.sha256` compañero) preregistra `cancellation_exact_groups` para M=1,4,8,16 y EXACT_GROUP_DOT_ISOLATION antes de ejecutar ambas QA. La cancelación exacta usa grupos +28448 y -28448, r=0, S=56896 y exige salida ±0 exacta. `ERRATA_cancellation_high.md` corrige κ de 2048 a 2049.

El verificador y todos sus consumidores se compilan con `/fp:strict`; el código consulta y valida `fesetround` y `_controlfp_s` al cambiar y restaurar el entorno. `verifier_unit_tests()` usa operandos `volatile` de runtime en suma, resta, multiplicación y división dirigidas; verifica restauración al volver a la operación llamadora y compara B con el cálculo de mayor precisión. Se mantienen las pruebas S-12 de frontera, NaN, Inf, tamaño, mutaciones exactas y salida cero.

La comprobaci?n independiente `bound_exact_verify.py` contrasta el B emitido por `common.cpp` con un or?culo de `fractions.Fraction` que representa exactamente los FP16 y la f?rmula racional de gamma; pas? 2.030 casos (10 fixtures, m=1/4/8/16, slots y outputs variados). El generador `bound_exact_dump.cpp` se compila con `/fp:strict` desde `common/build.cmd`.
