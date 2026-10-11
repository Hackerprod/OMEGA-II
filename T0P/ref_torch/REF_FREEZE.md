# Freeze de referencia Torch CPU

## HISTORIAL ? S-12/S-13

Los resultados S-12/S-13 son historial y quedan reemplazados por los veredictos S-14 de abajo. Los fixtures congelados y sus hashes permanecen intactos.

## GOBERNANTE ? S-14

Los buffers efectivos X/W/Wt usan `from_blob` con deleter sobre common Buffer. No se clonan a regiones que abandonen el asignador. QA completa: 130 filas; S-12, representaci?n exacta, barridos 512/512 por M y cancelaci?n exacta pasan. Group-dot isolation: 2.375.680 comprobaciones para mm, matmul y linear; 81.920 para mv (m=1). `mkldnn_linear` queda diagn?stico/no comparable.

| Candidata | QA_NUMERICAL | DISPATCH_QUALIFIED | PAGE_POLICY | REFERENCE_READY |
|---|---|---|---|---|
| mm | PASS | PASS | HOLD | HOLD |
| matmul | PASS | PASS | HOLD | HOLD |
| linear | PASS | PASS | HOLD | HOLD |
| mv (m=1) | PASS | PASS | HOLD | HOLD |

`QueryWorkingSetEx` inspeccion? las regiones efectivas: p?ginas v?lidas, `LargePage=0`. El token no contiene `SeLockMemoryPrivilege`, as? que PAGE_POLICY queda HOLD de recurso. No se cambi? la pol?tica del sistema. El mismo ejecutable queda listo para probar p?ginas grandes con el privilegio concedido en una sesi?n nueva.
