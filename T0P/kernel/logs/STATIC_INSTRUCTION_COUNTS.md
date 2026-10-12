# Conteo estático del kernel con O variable

Fuente: `dumpbin /disasm obj\q4_kernel.obj`; compilación `/O2 /arch:AVX2 /fp:precise`. Conteos estáticos del cuerpo K4, sin cronometraje. El cuerpo principal se comparte para todas las O; cada cola se cuenta en su rutina residual. Las colas no existían antes de la extensión geométrica.

| Ruta | VPDPBUSD | instrucciones K4 | VPDPBUSD/total | decode/VPDPBUSD | cadenas | spills K4 (cargas+stores) | spills fuera K4 (cargas+stores) | pila fuera K4 (cargas+stores) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Principal m=1 antes (O=1024) | 16 | 54 | 0,296 | 1,50 | 8 | 0+0 | 8+16 | 8+16 |
| Principal m=1 después | 16 | 54 | 0,296 | 1,50 | 8 | 0+0 | 8+16 | 8+16 |
| Principal m≥4 antes | 32 | 68 | 0,471 | 0,375 | 8 | 0+0 | 15+22 | 15+22 |
| Principal m≥4 después | 32 | 68 | 0,471 | 0,375 | 8 | 0+0 | 15+22 | 15+22 |
| Cola m=1 antes | — | — | — | — | — | — | — | — |
| Cola m=1 después | 8 | 58 | 0,138 | 1,50 | 1 | 2+3 | 0+1 | 0+1 |
| Cola m≥4 antes | — | — | — | — | — | — | — | — |
| Cola m≥4 después | 8 | 24 | 0,333 | 0,375 | 4 activaciones | 0+0 | 0+2 | 0+2 |

El cuerpo K4 principal no se degrada: m=1 conserva 16/54 y 1,50 decode/dot; m≥4 conserva 32/68 y 0,375. La cola m=1 maneja un tile residual; m≥4, un tile residual y cuatro activaciones. Los spills K4 son accesos SIMD a slots de pila dentro del cuerpo K4; fuera de K4 el conteo de spill coincide con las cargas y stores de pila listados. Se mantienen las 8 stores de inicialización y, para m≥4, la load de cierre fuera del bucle en ambos principales. Los conteos son estáticos del objeto, no mediciones de rendimiento.
