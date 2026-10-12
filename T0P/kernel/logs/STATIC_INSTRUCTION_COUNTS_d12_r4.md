# Conteo estático de instrucciones

Fuente: dumpbin /disasm obj\q4_kernel.obj. Para cada especialización se cuenta el salto K4 más corto que incluye decode y VPDPBUSD, incluidas cargas/broadcasts y el salto. No es una medida dinámica.

| m | tile activaciones | VPDPBUSD | cargas/broadcasts | decode | conversiones | FMA | spills | otras | total | VPDPBUSD/total |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 1 | 16 | 10 | 24 | 0 | 0 | 0 | 4 | 54 | 29.63% |
| 4 | 4 | 32 | 20 | 12 | 0 | 0 | 0 | 4 | 68 | 47.06% |
| 8 | 4 | 32 | 20 | 12 | 0 | 0 | 0 | 4 | 68 | 47.06% |
| 16 | 4 | 32 | 20 | 12 | 0 | 0 | 0 | 4 | 68 | 47.06% |

## Comparacion estatica ronda 2 / ronda 3

| m | VPDPBUSD/total antes | VPDPBUSD/total despues | decode/VPDPBUSD despues | cadenas antes/despues | spills K4 antes/despues | pila fuera K4 antes cargas+stores | pila fuera K4 despues cargas+stores por grupo |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 18/60 = 30,00% | 16/54 = 29,63% | 24/16 = 1,50 | 9/8 | 0/0 | 10+10 | 8+8 (mas 8 stores de inicializacion) |
| 4 | 32/68 = 47,06% | 32/68 = 47,06% | 12/32 = 0,375 | 8/8 | 0/0 | 14+14 | 14+14 (mas 8 stores y 1 load fuera del bucle) |
| 8 | 32/68 = 47,06% | 32/68 = 47,06% | 12/32 = 0,375 | 8/8 | 0/0 | 14+14 | 14+14 (bloque interno m=4) |
| 16 | 32/68 = 47,06% | 32/68 = 47,06% | 12/32 = 0,375 | 8/8 | 0/0 | 14+14 | 14+14 (bloque interno m=4) |

El m=1 ronda 3 procesa 8 tiles utiles (16 dots VPDPBUSD), sin la novena cadena redundante. El desensamblado tiene 16 VPDPBUSD y 54 instrucciones K4 frente a 16 dots utiles y 60 instrucciones antes: 3,375 frente a 3,75 instrucciones por dot util; VPDPBUSD/total queda 0,37 puntos porcentuales bajo el objetivo estatico de 30%. Se eligio eliminar tiles repetidos porque reduce instrucciones por MAC util y conserva 8 cadenas y decode/VPDPBUSD=1,50. No es un resultado de rendimiento.

Spills fuera de K4 por bloque de tiles y grupo: m=1, antes 10 cargas+10 almacenes, despues 8+8; m=4 (y m=8/16, procesados como tiles m=4) antes y despues 14+14. El objeto nuevo tiene ademas 8 stores de inicializacion m=1/m=4 y un load de cierre m=4 fuera del cuerpo de grupo. Son accesos logicos de pila, no bytes DRAM. K4 tiene cero spills; esto no implica cero spills del operador.
El decoder reconoce saltos K4 por instrucciones de nibble y VPDPBUSD. Cuenta como spill solo cargas/almacenamientos SIMD a slots de stack dentro del cuerpo K4. Los porcentajes son conteos estaticos del objeto, no mediciones dinamicas.
