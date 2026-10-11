# Diseño del kernel Q4 OMEGA, ronda 2

## Defectos de la ronda 1 y cambios

La ronda 1 cargaba 16 bytes y ejecutaba seis instrucciones SIMD de extracción/orden por cada VPDPBUSD: el dot ocupaba 1/13 instrucciones en m=1 (7,69 %) y 4/19 en m>=4 (21,05 %). También mantenía un acumulador por tile de ocho filas (cuatro cadenas para cuatro activaciones) y no compartía los broadcasts entre tiles de filas en m=1.

La ronda 2 cambia el layout de códigos y el bloqueo:

- Cada carga alineada de 32 bytes contiene ocho filas y dos K4 consecutivos. El nibble bajo forma las 32 lanes u8 del bloque K4 2*kp; el alto forma las 32 lanes del bloque 2*kp+1.
- El decode usa exactamente vpand, vpsrlw 4 y vpand: tres operaciones para dos vectores y dos VPDPBUSD.
- En m=1, nueve tiles de filas comparten cada par de broadcasts de activación; cada tile mantiene una cadena de dot independiente. Hay nueve cadenas en vuelo.
- En m=4/8/16, dos tiles de filas comparten cuatro activaciones preparadas. Cada tile y activación mantiene una cadena independiente: ocho cadenas en vuelo. El decode de cada tile se reutiliza en las cuatro activaciones.
- Los acumuladores de dot están nombrados como escalares SIMD; el desensamblado no muestra cargas ni almacenamientos de spill en el cuerpo K4. El informe estático cuenta 0 spills.

El tile final m=1 cubre las ocho filas de salida restantes con un solapamiento de un tile ya calculado; la escritura repetida contiene el mismo resultado. Esto conserva un cuerpo uniforme con nueve cadenas y no sale del rango de salida.

## Layout de pesos y preparación dinámica

PackedWeights conserva 262.144 bytes de códigos y 32.768 bytes de escalas FP16, 294.912 bytes en total. Los códigos se ordenan [tile_o8][grupo16][par_k4][fila_o8][K_in_pair4]. Cada byte almacena en el nibble bajo q+8 del primer K4 y en el alto q+8 del K4 siguiente. Para cada par, la línea de 32 bytes carga ocho filas por cuatro posiciones K. scale_offset mantiene las ocho escalas consecutivas por tile y grupo. Los buffers siguen siendo t0common::Buffer y admiten large pages.

La preparación dinámica mantiene ocho DWORD de activación por grupo, FP32 de escalas, sum(x) INT32 y los bits FP16 originales. El kernel carga y transmite los DWORD de activación una sola vez por par K4 en m=1 y reutiliza esos broadcasts entre tiles de salida. Para m>=4, cada par de vectores de pesos se decodifica una vez y se usa con las cuatro activaciones. No hay extensión de activación repetida por fila de salida.

La corrección sigue siendo exacta: Σ((q+8)*x) - 8*Σx = Σ(q*x). Las dos contribuciones K4 de cada par se suman en INT32; la magnitud máxima es menor que el límite INT32. qa_group_dots usa el mismo layout empaquetado y VPDPBUSD, y la QA compara cada dot de grupo contra el oráculo INT32.

## Orden y semántica FP32

Dentro de cada grupo, los bloques K4 se acumulan en orden ascendente: nibble bajo y después nibble alto de cada par. Los grupos se procesan en orden 0..15. Después de completar el dot entero del grupo, vcvtph2ps convierte las ocho escalas de peso una vez por tile de filas; se multiplica por la escala FP32 de activación preparada y se hace un FMA por activación y tile. Toda conversión y escalado FP32 está fuera del bucle K4. No se reordena la acumulación FP32 entre grupos; se conserva S-12 y la semántica de cancelación exacta.

## Paralelismo y registros

El desensamblado de dumpbin /disasm obj\q4_kernel.obj confirma 9 cadenas m=1 y 8 cadenas para m=4/8/16. Cada cadena es un acumulador YMM independiente sobre las ocho filas de salida. La hipótesis de latencia/throughput es 5 ciclos y 0,5 ciclos por instrucción (2 VPDPBUSD por ciclo) para el P-core Golden Cove; uops.info mide esos valores en Alder Lake-P y muestra ejecución en P0/P1. Raptor Cove es la evolución P-core de Golden Cove; se usa la misma hipótesis teórica para Raptor Lake y no se afirma una medición local. Con 8-9 cadenas, la latencia ya no serializa todo el trabajo como en la ronda 1: hay múltiples cadenas y operaciones de decode/broadcast independientes para emitir mientras se resuelven dependencias. El techo ideal de 2/ciclo requeriría 10 cadenas si solo hubiera VPDPBUSD; el conteo de cadenas aquí satisface el mínimo de ocho exigido, no demuestra ni mide saturación del techo.

Fuente de latencia y throughput: [uops.info, VPDPBUSD YMM, Alder Lake-P](https://uops.info/html-instr/VPDPBUSD_VEX_YMM_YMM_M256.html). La comparación con Raptor Cove es una hipótesis arquitectónica, no dato medido para este objeto.

## Conteo estático del cuerpo K4

logs/STATIC_INSTRUCTION_COUNTS.md se genera desde el objeto actual con dumpbin /disasm. El contador elige el salto hacia atrás K4 más corto que contiene VPDPBUSD y decode; incluye loads/broadcasts y el salto, y marca como spills las cargas/almacenamientos SIMD dirigidos a slots de stack dentro del cuerpo. No es medición dinámica.

| m | cadenas independientes | VPDPBUSD/total antes | VPDPBUSD/total ahora | decode/VPDPBUSD | conversiones | spills |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 9 | 1/13 = 7,69 % | 18/60 = 30,00 % | 27/18 = 1,50 | 0 | 0 |
| 4 | 8 | 4/19 = 21,05 % | 32/68 = 47,06 % | 12/32 = 0,375 | 0 | 0 |
| 8 | 8 | 4/19 = 21,05 % | 32/68 = 47,06 % | 12/32 = 0,375 | 0 | 0 |
| 16 | 8 | 4/19 = 21,05 % | 32/68 = 47,06 % | 12/32 = 0,375 | 0 | 0 |

Los umbrales estáticos se cumplen: VPDPBUSD/total >=30 % en m=1 y >=45 % en m>=4; decode <=1,5 por VPDPBUSD en m=1 y <=0,4 en m>=4; no hay conversiones ni spills en el cuerpo K4. Los registros no desbordan los 16 YMM disponibles en Windows x64 según el desensamblado del bucle.

## Contrato y QA

run es monohilo, puro, no asigna, valida, despacha por ISA ni instrumenta. La preparación estática de pesos y dinámica de activaciones está separada. El contrato mantiene m={1,4,8,16}, large pages vía t0common::Buffer, reconstrucción exacta desde el almacenamiento efectivo y contrapruebas negativas de códigos y escalas. La QA cubre S-12, finitud, entrada nula, cancelación exacta, barrido de 512 coordenadas, EXACT_GROUP_DOT_ISOLATION y los 212 registros PAGE de PAGE_EXPECTED_KERNEL.json.

No se hicieron mediciones de rendimiento ni cronometraje. Esta revisión es estática y teórica; no declara ventaja empírica frente a otra implementación.
