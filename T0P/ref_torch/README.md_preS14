# Referencia Torch CPU

La revisión S-12, candidatas, resultados y ambigüedades están en [REF_FREEZE.md](REF_FREEZE.md) y [REF_FREEZE.json](REF_FREEZE.json). QA cruda: `adapter/verify_common_all.txt`; unidad del comparador: `adapter/verifier_unit_tests.txt`. Se conservaron los resultados previos con sufijo `_preS12`. No se cronometró.

## Estado gobernante S-13

`common.cpp` y este consumidor se compilan con `/fp:strict`. `--unit` pasa las pruebas de redondeo dirigido en suma, resta, multiplicación y división con operandos de runtime, restauración del modo, cota S-12, NaN/Inf, tamaños y mutaciones exactas. `EXACT_GROUP_DOT_ISOLATION` ejecuta mm, matmul, linear y mv (m=1) sobre las 5 familias × 16 grupos, con escalas unitarias y compara los valores FP32 observados directamente como double con el oráculo INT32.

Los controles de representación comparan los buffers efectivos de X, W y Wt contra sus mapas canónicos independientes. Los clones ATen usados por las rutas se inspeccionan con `QueryWorkingSetEx`. El token no contiene `SeLockMemoryPrivilege`; el asignador normal se registra como HOLD de recurso, sin cambiar políticas del sistema. `mkldnn_linear` sigue como diagnóstico no gobernante bajo la política de páginas.

Cierre S-13: gate Torch 130/130 PASS; `QueryWorkingSetEx` recorre todas las p?ginas de X, W y Wt efectivos: 120 consultas, 0 fallos. Wt coincide exactamente con el mapa can?nico transpuesto.
