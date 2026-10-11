# T0.0 parte 2, etapa 1 — Q4 correctness

## Operador y referencias

- `W[O][I]` es fila-mayor; `I` múltiplo de 32; los grupos no cruzan filas. Cada grupo se almacena como 16 bytes de nibbles signed two's-complement `[-7,+7]` (peso `2k` low nibble, `2k+1` high nibble) seguidos por scale FP16 IEEE little-endian. No se añade padding entre grupos/filas/matrices.
- Activaciones de entrada: INT8 simétricas `[-127,+127]`, escala FP32 por grupo de 32. El generador emite los valores INT8 y sus escalas por separado; no introduce un paso float→INT8 con fórmula no especificada.
- Para cada output/group, se suma el producto entero de 32 términos, se multiplica por `scale_act * scale_weight`, y se acumula en double (mayor o igual a FP32); salida final convertida a FP32. `m` está soportado de forma general; el unpack de pesos se hace una vez por grupo y se reutiliza para las `m` activaciones.
- Rutas: ISA_REF escalar; ISA_BASE AVX2; ISA_OPT AVX-VNNI-256 solo tras CPUID support. Test incluye igualdad exacta de `int32` group dots contra la referencia escalar.
- Generación: SplitMix64, semilla base explícita `0x544F30302D513431`; semillas sucesivas por caso y seed de activaciones `seed XOR 0x9E3779B97F4A7C15`. Nibbles generados como `rng % 15 - 7`; activaciones como `rng % 255 - 127`. Fixtures extremos fuerzan la secuencia `-7,+7,0` y `-127,+127,0`. Escala peso FP16: código `0x3800 + ((g+o)%16)*0x40` (finita, positiva); escala activación FP32: `0.25 + ((g+7*m+(seed&31))%32)/32`. Generación explícita/determinista, nunca derivada de resultados medidos.

## Tolerancia y pruebas

Declarada en `src/q4_correctness.cpp` antes de la corrida: `atol=1.0e-5f`, `rtol=1.0e-5f`, aceptación `abs(y_route-y_ref) <= atol + rtol*abs(y_ref)`. No se cambió tras la prueba. Casos: varias formas pequeñas, caso extremo `±7/0` y `±127/0`, y `O=512,I=512` con `m=1,4,16` más extremo `m=16`.

| Ruta | Resultado | Integer group dots | Error final máximo |
|---|---|---|---:|
| ISA_REF escalar | referencia | identidad por definición | 0 |
| ISA_BASE AVX2 | PASS en todos los casos | bit-exact contra scalar | 0 |
| ISA_OPT AVX-VNNI-256 | PASS en todos los casos (CPUID confirmado) | bit-exact contra scalar | 0 |

La salida cruda por caso/ruta está en `q4_correctness_raw.jsonl`; el mismo stdout/stderr crudo está en `logs/stage1_q4_correctness_cpu_only.*`. No contiene tiempos ni throughput.

Construcción: `build/build_q4_correctness.cmd`, MSVC cl/link 19.44.35229.0 (toolset 14.44.35207), Windows SDK 10.0.26100.0; compilación `/O2 /Brepro`; enlace `/Brepro /INCREMENTAL:NO`. Los comandos exactos y sus salidas están en `logs/stage1_build_q4_correctness.*`.

## Bytes persistentes reportados

Para **una** matriz `W[512][512]`: grupos `512*(512/32)=8192`; payload Q4+scales `8192*18=147,456 B`; padding `0`; descriptor persistente `sizeof(WeightMatrix)=48 B`; vector capacity observada `147,456 B`; total de representación asignada que informa el operador `147,504 B`. La cuenta usa `capacity()` del buffer y el descriptor persistente; no incluye cabeceras internas del allocator del CRT.

El núcleo agregado `16d²` del contrato equivale a 2,359,296 B de payload Q4; esta prueba ejercita una matriz `O=512,I=512`, no instancia 16 matrices. No presento la cifra agregada como bytes efectivamente asignados por esta prueba.
