# Diseño del kernel Q4 OMEGA: extensión geométrica S-19

## Contrato y geometrías

El operador conserva `d=512`, `I=512`, `G=16` grupos de 32, empaquetado Q4 de dos bloques K4 por vector de 32 bytes, activaciones INT8 preparadas y corrección exacta `Σ((q+8)x) − 8Σx = Σ(qx)`. El orden de acumulación es K4 ascendente y grupos 0…15; cada grupo convierte y escala en FP32 con el mismo producto y FMA. `run()` no asigna memoria, valida, despacha ni instrumenta.

`prepare_weights(cell,O,...)` y `prepare_activations(cell,O,...)` reciben O por shard. Se exigen múltiplos de 8 entre 8 y 58.256. Las API de tamaño son `weight_bytes(O)=288·O` y `output_bytes(m,O)=4·m·O`. Los planos de códigos/escalas mantienen exactamente el orden original; solo cambia su extensión. Las activaciones siguen ocupando `672·m` bytes y guardan qwords, escalas FP32, sumas INT32 y copia FP16.

La API de ejecución es `run(weights, activations, O, output)`. El trabajo útil por llamada es exactamente `m·O·512` MAC.

| W objetivo (KiB) | O | Bytes reales por shard (288·O) |
|---:|---:|---:|
| 256 | 912 | 262.656 |
| 288 | 1.024 | 294.912 |
| 512 | 1.824 | 525.312 |
| 768 | 2.728 | 785.664 |
| 1.024 | 3.640 | 1.048.320 |
| 1.280 | 4.552 | 1.310.976 |
| 1.536 | 5.464 | 1.573.632 |
| 1.792 | 6.368 | 1.833.984 |
| 2.048 | 7.280 | 2.096.640 |
| 2.560 | 9.104 | 2.621.952 |
| 3.072 | 10.920 | 3.144.960 |
| 4.096 | 14.560 | 4.193.280 |
| 6.144 | 21.848 | 6.292.224 |
| 8.192 | 29.128 | 8.388.864 |
| 12.288 | 43.688 | 12.582.144 |
| 16.384 | 58.256 | 16.777.728 |

La regla aplicada es `O=8·round_half_up(W_KiB·1024/(8·288))`; todos los tamaños del barrido son múltiplos de ocho. Se compila un solo ejecutable y O se pasa en ejecución.

## Recorrido, colas y ausencia de MAC adicionales

Con `T=O/8` tiles de salida, el recorrido principal m=1 procesa solo bloques completos de ocho tiles (`ot+8≤T`); m≥4 procesa solo parejas completas (`ot+2≤T`). La cola m=1 recorre cada tile residual una sola vez; la cola m≥4 procesa el tile residual único con una rutina de un tile y cuatro activaciones. Ninguna cola comparte filas con el cuerpo principal, accede a un tile posterior ni inventa MAC. Cada salida se escribe una vez y el tamaño de salida es exactamente `4·m·O` bytes. Los casos sin cola no ejecutan rutinas residuales.

La QA prueba todos los grupos y todas las filas para las cinco familias de aislamiento, en todos los O y m. El barrido de coordenadas recorre las 512 posiciones K y llama a `run()` completo en cada posición. Para O=1024 compara exactamente todos los outputs, todos los m y las 512 posiciones; para los otros quince O compara las primeras y últimas ocho salidas tras cada ejecución completa. El aislamiento de grupo y la suite numérica recorren O completo, por lo que cubren exhaustivamente las colas y ambas fronteras.

## Identidad histórica y generación QA

En O=1.024 el cuerpo geométrico es el candidato congelado: no hay cola para m=1 ni m≥4, y cada bloque K4 conserva las mismas instrucciones y el orden de escala/FMA. La QA compara las 40 salidas binarias de los diez fixtures y m={1,4,8,16}, valor por valor, con el resultado producido por el código previo congelado; el control `BIT_IDENTITY` debe registrar 40/40 y cero diferencias.

Para O≠1.024, la QA parte de los fixtures deterministas existentes. Extiende únicamente filas de pesos/escala mediante el índice fijo `source=(o·1664525 + 1013904223 + O + 0x4F4D4547) mod 1024`; así conserva las familias de valores y evita depender del estado de un generador externo. La referencia FP64 independiente calcula cada fila fuente mediante `t0common::reference64`; la adaptación de forma aplica `t0common::s12_compare` por salida. `common` no se modifica.

## Cobertura PAGE y controles

Para cada O y m, PAGE exige `n_W=288·O`, `n_X=672·m`, `n_Y=4·m·O`, y `n_dots_QA=64·m·O`, además de large pages y `queried_pages=valid_pages=large_pages=ceil(((address mod dwPageSize)+bytes)/dwPageSize)`. El parser incluye contrapruebas de tamaños incorrectos, bytes=1, ausencia, duplicación y contadores incoherentes. La QA incluye S-12 FP64, representación efectiva de códigos/escalas FP16, planos FP32 e INT32, mutaciones negativas independientes, finitud, cero/cancelación exacta, aislamiento por grupo y barrido de coordenadas.

## Inspección estática

El conteo estático distingue los cuerpos K4 del recorrido principal m=1 y m≥4 y los cuerpos K4 de ambas colas. El objetivo del principal permanece m=1: 16/54 VPDPBUSD/total, decode 1,50; m≥4: 32/68, decode 0,375. `STATIC_INSTRUCTION_COUNTS.md` registra los recuentos del objeto recompilado, cadenas, spills dentro de K4 y accesos de pila fuera del cuerpo, junto con la comparación con el objeto congelado. No se realizaron mediciones de rendimiento ni cronometraje.

## Evidencia D12 y objeto integrado

`qa_kernel_gate.py` exige 512 IDs de coordenada únicos para cada par O/m, la ruta textual `omega_q4.run`, el recuento exacto comparado por ID y los dos modos de cobertura separados: O=1024 completo y los otros quince O parcial. `--self-test` elimina un ID y cambia la ruta a `qa_run_selected_tiles`; ambos registros deben ser rechazados. El baseline binario se abre desde `logs/` junto al ejecutable, no desde el directorio de trabajo actual.

`build_d12_r4.cmd` enlaza el objeto `obj/q4_kernel.obj` en `qa_kernel.exe`, así que la QA inspecciona el mismo objeto que puede enlazarse al harness. El build del harness actual recompila el kernel con `/std:c++20` frente a `/std:c++17` en QA, aunque comparte `/O2 /arch:AVX2 /fp:precise`; comprobar el objeto resultante con `verify_kernel_object.ps1`. El enlace integrado debe sustituir `obj_r11/q4.obj` por `T0P/kernel/obj/q4_kernel.obj` y registrar el hash final.

Ejecución D12 registrada contra el objeto existente del harness: `cmd /c call "C:\Projects\OMEGA\Ejercise\T0P\kernel\build_d12_r4.cmd" "C:\Projects\OMEGA\Ejercise\T0P\harness\obj_r11\q4.obj"`. Ese mismo objeto se enlazó en `qa_kernel.exe`; el registro `qa_kernel_link_object_d12_r4.txt` identifica la ruta. La QA integrada duró 3 min 15,8 s y el gate dio `KERNEL_QA_GATE=PASS`: 64/64 pares O/m, 16/16 salidas, barrido explícito O=1024 4/4 completo y otros O 60/60 parcial, 32.768 filas de coordenadas, 40/40 resultados bit-idénticos del baseline O=1024, páginas 53/53 por par y cero fallos de aislamiento de grupos.

El hash del objeto integrado probado fue `4ef68d599afb81ed043e26f1c9998483ae38a55c68587e30ad2c5b3a39b6ef9d`. `verify_kernel_object.ps1` compara cuerpos de máquina `omega_q4` cuando los objetos completos difieren. El objeto candidato QA y el objeto integrado no son byte ni ensamblado idénticos: se registraron diferencias en `run_tile<4>` y `expand_q4_pair`; ambos cuerpos se hashéan en `logs/verify_harness_q4_r4.txt`. La evidencia de equivalencia funcional para este objeto integrado es la QA completa recién descrita, incluido el control binario congelado. Si se recompila el harness, se debe volver a enlazar su objeto recién generado en la QA y repetir el gate antes de usarlo.

Hashes D12: `qa_main.cpp` `a3ce2f38e4081775d461a9ef126774d075e93612533117c7420c3b9755bd5d6c`; `qa_kernel_gate.py` `c7204f38beeb273b783bc8bbae482d10d66458a2ccd35ae3386d72501e852122`; `qa_kernel.exe` enlazado al objeto integrado `692f3318067845e01a47b10206ac79bf40f5ddb21e6aff35311d3c862f3915fe`; objeto kernel del build QA `567750b2d4b7e974a803a1ea541ea1027ddb2b6a1aa5e870910757ebc530ae28`. El fuente geométrico ratificado conserva SHA-256 `6d50162a1e400012a7d3bf4fcec0febccc266b584436876e19d33381a7500753`.
