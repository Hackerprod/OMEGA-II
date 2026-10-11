# Conteo estático de instrucciones

Fuente: dumpbin /disasm obj\q4_kernel.obj. Para cada especialización se cuenta el salto K4 más corto que incluye decode y VPDPBUSD, incluidas cargas/broadcasts y el salto. No es una medida dinámica.

| m | tile activaciones | VPDPBUSD | cargas/broadcasts | decode | conversiones | FMA | spills | otras | total | VPDPBUSD/total |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 1 | 18 | 11 | 27 | 0 | 0 | 0 | 4 | 60 | 30.00% |
| 4 | 4 | 32 | 20 | 12 | 0 | 0 | 0 | 4 | 68 | 47.06% |
| 8 | 4 | 32 | 20 | 12 | 0 | 0 | 0 | 4 | 68 | 47.06% |
| 16 | 4 | 32 | 20 | 12 | 0 | 0 | 0 | 4 | 68 | 47.06% |

## Comparación ronda 1 / ronda 2

| m | VPDPBUSD/total R1 | VPDPBUSD/total R2 | decode/VPDPBUSD R2 | cadenas R2 | spills R2 |
|---:|---:|---:|---:|---:|---:|
| 1 | 1/13 = 7,69% | 18/60 = 30,00% | 27/18 = 1,50 | 9 | 0 |
| 4 | 4/19 = 21,05% | 32/68 = 47,06% | 12/32 = 0,375 | 8 | 0 |
| 8 | 4/19 = 21,05% | 32/68 = 47,06% | 12/32 = 0,375 | 8 | 0 |
| 16 | 4/19 = 21,05% | 32/68 = 47,06% | 12/32 = 0,375 | 8 | 0 |

El decoder reconoce saltos K4 por contener tanto instrucciones de nibble como VPDPBUSD; cuenta como spill las cargas/almacenamientos SIMD a slots de stack dentro de ese cuerpo. Los guardados ABI en el prólogo y epílogo quedan fuera. El escalado FP32 está fuera del K4. Los porcentajes son conteos del objeto, no mediciones de rendimiento.
