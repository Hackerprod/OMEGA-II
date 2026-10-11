# Inspección Opus — Fase T0.0 (contexto limpio, 2026-10-10)

Normas: `Roadmap.md` (tablero, §4.2, §4.3, §5.1, §7), `T0.md` (§2, §3, §3.1, §7), `Conversacion.md` (L2500-2546, L2622-2662, L2841-2849, L2894-2928, consultado por búsqueda), `MD\22.md`–`MD\28.md`.
Método: solo lectura. Recalculé medianas desde los CSV crudos con Python, verifiqué SHA256SUMS contra disco y revisé código, logs y `COMMANDS.json`.

## VEREDICTO

**`T0.0 = DESVIACIONES`**

Ninguna desviación invalida mediciones, series, correctness ni el gate `D_read`. No hace falta volver a medir. Hay cuatro incumplimientos de instrucciones explícitas de Sol o de la spec: preservación de evidencia, reetiquetado legacy, tabla final sin TBD y registro de resoluciones. Además hay inconsistencias documentales. Según Roadmap §4.3, la fase vuelve a EN_CURSO hasta corregirlas. Todas las correcciones son documentales y pequeñas.

## Desviaciones

### 1. MEDIA — El preflight crudo del tramo 2 (E-core) no se conservó (MD/28 §4)
- MD/28 §4 exige: «conservar los preflights de ambos tramos».
- `logs\COMMANDS.json` registra dos ejecuciones de `phase1_resume_official_session_preflight`:
  - 02:06:25 UTC (tramo 2, 16 series E): stdout sha `E2FB5B40…`.
  - 03:06:22 UTC (tramo 3, 16 series P m=1): stdout sha `92F4E2DF…`.
- Los dos escribieron en el mismo archivo `logs\phase1_resume_official_session_preflight.stdout.txt`. En disco solo queda el del tramo 3 (`captured_utc 2026-10-11T03:06:22`, media 6.64 %, pico 14 %, 177 procesos).
- `phase1_execution_status.json` y `phase1_session_summary.json` también reflejan solo el tramo 3 (`resume_pcore_m1_only=true`).
- La cadena `2026-10-11T02:06:2…` solo aparece en `COMMANDS.json`.
- Consecuencia: los valores que adjudicó MD/28 (media 5.545 %, pico 11 %, 175 procesos) y la evidencia cruda de que el tramo 2 cumplió las condiciones bloqueantes (Chrome ausente, plan de energía, topología) sobreviven solo como texto en `phase1_session_report.md:14` y como un hash.
- También se sobrescribieron los stdout crudos de los preflights HOLD 1 y 2 (`E3596E…`, `B6767A…`). Esos dos casos sí conservan sus JSON resumen en `results\T0.0\phase1_preflight_hold_attempt{1,2}_*`.
- Corrección mínima:
  - Registrar el incidente `PREFLIGHT_SEGMENT_2_RAW_OVERWRITTEN` (no recuperable): hash original, hora, valores de la sesión tomados del informe, y declaración de que ninguna muestra depende de esos valores (la carga es solo observación).
  - Informar a Sol junto con el paquete de T0.1.
  - Para T0.1, el runner debe escribir los archivos de preflight y de estado con nombre único por tramo.

### 2. MEDIA-BAJA — El reetiquetado legacy ordenado por Sol no se aplicó (MD/25 §14; T0.md §3.1, viñeta «Contabilidad»)
- MD/25 §14 ordena: `v_i actual = Q4_M1_PREOPT_PER_CORE`, `e actual = Q4_M1_PREOPT_BY_ISA`, `BW_L2 actual = BW_Q4_LOGICAL_PREOPT`, «No eliminarlos».
- Una búsqueda en `results\T0.0\` no encuentra ninguna de esas tres etiquetas.
- Los archivos legacy siguen con sus nombres originales:
  - `d_vi.csv` y `d_vi_resumen.csv`: `metric=MAC_per_second`, cuando en realidad son ops/s (2 por MAC, según `unit_correction.md`).
  - `e.md`: cabeceras «MAC/s», que también son ops/s.
  - `f_ancho_banda.csv`: series `f_bw_l2_*`, `f_bw_l2_sigma_*` y `f_bw_d_n*`.
  - `stage2_raw\f_r_summary.csv`: `r` sin la etiqueta `LEGACY_PREOPT_DESCRIPTIVE`.
- Mitigación parcial que ya existe en disco:
  - `unit_correction.md` y los derivados `*_MACconv*`.
  - `phase1_ABC_status.csv`: `r_A/r_B/r_C=LEGACY_PREOPT_DESCRIPTIVE` y A/C `DEFERRED_TO_T0_1`.
  - `phase1_T0.1_handoff.md:25`: `BW_memcpy_logical(n)` como histórico.
- Corrección mínima: un manifiesto único de etiquetas legacy (archivo legacy → etiqueta oficial MD/25/MD/28 → unidad real), sin tocar los originales, y añadirlo a SHA256SUMS.

### 3. MEDIA-BAJA — La tabla final de T0.0 tiene TBD y omisiones dentro de su alcance (MD/28 §13 E; Roadmap §4.2)
- `a_inventario.md:20` mantiene «Relojes / Potencia | TBD-MEASURE … | Pendientes». No remite al estado final:
  - PL1/PL2 = `UNAVAILABLE_WITH_ALLOWED_INTERFACES` (MD/23).
  - Frecuencia = `UNAVAILABLE_WITH_QUALIFIED_TOOLS` (MD/28).
- `phase1_T0.0_final_table.csv` solo cubre las métricas de la fase 1. No incluye las filas de la tabla 2.2 (inventario, ISA, topología, contadores, QPC, PL1/PL2) ni `BW_memcpy_logical(n)`. Por tanto no existe una «tabla T0.0 final» única de los ítems a–i.
- `i_condiciones.md` se titula «frecuencia efectiva sostenida» y presenta la mediana PDH de 2100 MHz, que MD/28 §12 prohíbe usar como frecuencia. No tiene nota de reemplazo.
- Corrección mínima:
  - Añadir esas filas a `phase1_T0.0_final_table.csv`.
  - Actualizar la fila de `a_inventario.md` con una referencia.
  - Añadir a `i_condiciones.md` una línea de reemplazo que remita a `effective_frequency_attempt.json`.

### 4. BAJA — Resoluciones de Sol sin registrar en el Roadmap y registro de cambios de T0.md incompleto
- Roadmap §7 registra MD/22 y MD/24 (con una mención a MD/25).
- No registra las resoluciones que modifican la salida §4.2 de T0.0:
  - MD/23: RAM `FIRMWARE_REPORTED` y PL1/PL2 `UNAVAILABLE`.
  - MD/25/28: `BW_L2`→`BW_Q4_logical`, `BW_D`→`BW_memcpy_logical`, A/C `DEFERRED_TO_T0_1`, `r` legacy y frecuencia `UNAVAILABLE_WITH_QUALIFIED_TOOLS`.
- Esto choca con la regla de registro en el Roadmap (Roadmap §4.3 cond. 2; T0.md §4: «Las resoluciones se registran en `Roadmap.md`»).
- T0.md §9 no tiene entradas para la §3.1 (MD/24–28), a pesar de la regla de T0.md:5. Su línea 3 cita un hash del Roadmap anterior al registro de MD/24.
- Corrección mínima: una fila en Roadmap §7 y entradas en T0.md §9.

### 5. BAJA — Inconsistencias documentales en los informes
- `phase1_session_report.md`:
  - `:12` dice «834 entries verified»; la cifra real es 944/944.
  - `:13` dice «no MD/28 series have started», lo que contradice la sección «MD/28 final closure» del mismo archivo.
  - `:14` solo da el preflight del tramo 2; falta el del tramo 3 (6.64 % / 14 % / 177).
  - El tramo 3 no tiene una etiqueta formal `SESSION_SEGMENT_3`.
- `phase1_disassembly_evidence.md:33`: el título dice «calls=7» pero el texto dice N=31.
- `a_inventario.md:11` cita «MD/424» (= `MD\20.md`, del flujo anterior al reinicio) como autoridad para «Raptor Lake».
- T0.md §3 nombra el entregable `e_pico_mac.csv`, que no existe. Los reales son `e.md`, `e_MACconv.md` y los `phase1_P_*`.
- Los scripts de compilación de la fase 1 están en `T0\src\build\` en lugar de `T0\build\` (T0.md §2).

### 6. BAJA (higiene, fuera del núcleo de T0.0)
- `ESTADO_PROYECTO.md` está desactualizado: fecha del 2026-10-09 y dice que T0.0 no ha iniciado.
- `ROADMAP_FREEZE_RECORD.md` está obsoleto y sigue en la raíz de `Ejercise`.
- La raíz `C:\Projects\OMEGA` tiene archivos sueltos de la campaña A1: `a1_test*.log`, `run_a1_tests.ps1`, `Claude-agent.cmd`.
- `T0\src\__pycache__` tiene 5 archivos `.pyc` fuera de SHA256SUMS. Es inocuo.

## Comprobaciones conformes (con evidencia)

- **Ítems a–i**: todos tienen salida.
  - a: `a_inventario.md`.
  - b: `b_topologia.md` y `.csv`; topología obtenida por API, 8P con SMT + 8E, L2 2 MiB / 4 MiB por clúster, LLC 30 MiB.
  - c: `c_isa.md`; AVX2 y VNNI-256 presentes, AVX-512 ausente, XGETBV 0x7.
  - d, e: v_i final = `phase1_P_Q4_i_m1_by_core.csv` (16 series P) más E-core m=1; e = `P_instr` P/E de ambas rutas y `P_Q4_peak`.
  - f: `BW_memcpy_logical(n)` histórico, `BW_DRAM_read_8P`, `BW_Q4_logical,i`; B medido, A/C `DEFERRED_TO_T0_1` según MD/28 §10.
  - g: `g_contadores.md`, con la vía alternativa escrita.
  - h: `h_tiempo.md`; QPC a 10 MHz, coste amortizado y RDTSCP.
  - i: PDH legacy, frecuencia `UNAVAILABLE_WITH_QUALIFIED_TOOLS` (permitido por MD/28 §12) y PL1/PL2 `UNAVAILABLE` (MD/23). Plan de energía registrado en el preflight (GUID 8c5e7fda…).
- **Contrato d=512 y sin sustitutos**: forma O=I=512 y `weight_physical_allocated_bytes=147504` (payload 147456 = 512·16·18). Las formas pequeñas solo aparecen en los fixtures de correctness. Ninguna referencia a `D:` en `T0\`. Una sola spec (`T0.md`); el resto de specs está solo en `Legacy\`.
- **Fidelidad a MD/25–28**:
  - `kN=31`, `kWarmup=5`, `kTimedRegionMinNs=1e8`, `kCalibrationTargetNs=1.5e8` (`phase1_measure_harness.cpp:34-41`).
  - 79/79 series `SERIES_COMPLETE`, intento 1, sin `INVALID_TIMER_TOO_SHORT`. Muestra mínima 143.49 ms (P m=1) y 145.3 ms (P_instr).
  - 79 calibraciones ≥150.0001 ms, unidades idénticas a las de las series, `statistical_sample=false`.
  - Mediana/IQR/MAD con `outlier_discard_count=0`.
  - Ventana de equipo: `QPC_start` tras la barrera de listos, antes del release; `QPC_end` tras la barrera de finalización; joins en el destructor (`:404-416`, `:397-400`).
  - P_ref = clave API P más baja (`:319-323`) = `CORE_API_00`.
  - Orden preregistrado: alternancia de ISA y M ascendente; semillas E `4D44323545434F52` y P m=1 `4D44323850314D31`; plan congelado a las 19:48, antes del tramo 3 (20:06).
  - `D_read`: numerador simultáneo de 8P; recalculado como ΣMAC/ventana de equipo, que coincide con `group_MAC_per_second`. No es suma de singles.
  - Denominador: `BW_DRAM_read_8P`, solo carga, 8 acumuladores, 125,829,120 B por worker pre-tocados, bytes contados = bytes leídos (`:681-742`).
  - MAC convencional = O·I·m·calls. Recorridos = M/m_tile exactos (m_tile 1/2/4).
- **HOT_PATH_CLEAN** (`q4_kernel.cpp`):
  - Sin asignación, `validate`, `group_dot`, `ldexp` ni `double` en las entradas cronometradas. `group_dot`/`double` solo bajo `#ifdef T0_Q4_QA`.
  - `half_to_float_fast` usa F16C. Dispatch fuera del timer. Entradas `noinline`.
  - Flags `/O2 /std:c++17 /arch:AVX2 /fp:precise /EHsc /Brepro`, sin `/fp:fast` (`src\build\build_phase1_measure_harness.cmd`).
  - P_instr: `vpmaddwd+vpaddd` y `vpdpwssd` codificado en VEX (decodificado: `ymm2..9 += ymm0·ymm1`), 8 acumuladores independientes.
- **Correctness**: `logs\phase1_correctness_only_attempt23.stdout.txt` (binario final, compilación 41).
  - 11 fixtures con `integer_group_exact`, `AVX2_vs_ref_allclose`, `VNNI_vs_ref_allclose`, `AVX2_vs_VNNI_bit_exact` y `finite_outputs` en true.
  - Primitivas P_instr en true. `summary PASS`.
  - Ejercita la entrada cronometrada real y calls 1 vs 31.
  - FP32 ratificada en MD/26 §2 y reflejada en T0.md:85.
- **Cifras recalculadas desde los CSV crudos** (todas coinciden):

  | Cifra | Recalculado | Notas |
  |---|---|---|
  | TEAM 8P AVX2 | 133.398060e9 | ×0.5625 = 75.0364 GB/s |
  | TEAM 8P VNNI | 137.703394e9 | ×0.5625 = 77.4582 GB/s |
  | `BW_DRAM_read_8P` | 74.455801 GB/s | — |
  | `D_read` | 1.007798 / 1.040325 | — |
  | `P_Q4_peak` | AVX2 29.2728e9 (m=32), VNNI 30.9782e9 (m=8) | — |
  | P_instr P | AVX2 115.175e9, VNNI 128.680e9 | — |
  | P_instr E | AVX2 32.391e9, VNNI 32.439e9 | — |
  | E-core Q4 m=1 | AVX2 6.5564e9 (6.517–6.570), VNNI 6.9303e9 (6.903–6.957) | — |
  | P-core m=1 | AVX2 16.715e9, VNNI 17.497e9 | P_ref dentro de mín/máx |

- **SHA256SUMS**: 944/944 coinciden y no falta ningún archivo (recalculado ahora). Solo 5 `.pyc` sin listar.
- **Incidentes**:
  - `UNAUTHORIZED_PREFLIGHT_GATE`: los HOLD 1 y 2 se conservan; no hubo series antes de retirarlo.
  - Segmentación: los archivos crudos del tramo 1 no se modificaron después de las 18:45. El tramo 2 escribió solo `e_confirm_*` (19:08). El tramo 3 escribió solo `p_q4_pcore_m1_*` (20:08).
  - Ninguna muestra se descartó a posteriori.

## Nota técnica latente (no es desviación de T0.0)

`select_prevalidated_kernel` / `kernel_m_tile` (`q4_kernel.cpp:261,267`) envían cualquier M que no sea 1, 2 ni múltiplo de 4 (por ejemplo M=3 o M=6) a la ruta m=1. Esa ruta procesa solo la fila 0, así que esas M darían una salida silenciosamente incompleta. Los M oficiales {1,2,4,8,16,32} no están afectados. Conviene añadir un guard que rechace esas M antes de T0.1.

## Lo que no pude verificar

- Que el tramo 2 cumpliera Chrome ausente, plan de energía y topología: el registro crudo se perdió (desviación 1). Solo existe la afirmación del informe más un hash.
- Que los datos del tramo 1 no hayan cambiado entre tramos: solo lo respaldan las marcas de tiempo, porque no hay un SHA256SUMS anterior conservado.
- `Conversacion.md` lo revisé por búsqueda en las líneas citadas por el Roadmap, no completo.

---

## Reinspección (2026-10-10, 20:28 hora local; reloj del i7)

Método: verifiqué en disco cada corrección. Recalculé desde los CSV crudos y comprobé SHA256SUMS completo. Revisé `q4_kernel.cpp` y la última corrida de correctness.

### Estado de las desviaciones 1–6

| # | Estado | Evidencia |
|---|---|---|
| 1 Preflight del tramo 2 | **RESUELTA** (incidente registrado; el dato no es recuperable) | `results\T0.0\INCIDENT_preflight_segment2_overwritten.md`: `PREFLIGHT_SEGMENT_2_RAW_OVERWRITTEN`, NO RECUPERABLE, hash original `E2FB5B40…`, valores 5.545/11/175 tomados del informe y declaración de que ninguna muestra depende de ellos. El runner ahora usa un nombre de archivo por segmento. `phase1_T0.0_final_table.csv` contiene `PREFLIGHT_SEGMENT_2_RAW,NO_RECUPERABLE`. **Queda pendiente** informar a Sol junto con el paquete de T0.1. |
| 2 Reetiquetado legacy | **RESUELTA** | `legacy_labels_manifest.md` asigna `Q4_M1_PREOPT_PER_CORE`, `Q4_M1_PREOPT_BY_ISA`, `BW_Q4_LOGICAL_PREOPT`, `BW_memcpy_logical(n)` y `LEGACY_PREOPT_DESCRIPTIVE`, con la unidad real (ops/s frente a MAC/s). Los originales `d_vi*`, `e*.md`, `f_ancho_banda.csv`, `stage2_raw\*` y `phase1_raw\*` no cambiaron (mtime anterior a las 20:08 y hash conforme a SHA256SUMS). |
| 3 Tabla final / TBD | **RESUELTA** | `phase1_T0.0_final_table.csv` tiene 47 filas: inventario, ISA, contadores, QPC, plan de energía, PL1/PL2, telemetría, PDH 2100 como `LEGACY_NOMINAL_CONSTANT`, `BW_memcpy_logical(n)`, legacy y segmentos 1–3. `a_inventario.md` ya no tiene «Pendientes» y cita MD/22–28 en lugar de MD/424. `i_condiciones.md` aclara que PDH es nominal constante y NO frecuencia efectiva, y que el estado gobernante es `UNAVAILABLE_WITH_QUALIFIED_TOOLS`. No queda ningún TBD dentro del alcance de T0.0. |
| 4 Registro de resoluciones | **RESUELTA** | Roadmap §7: línea 386 (MD/24 y MD/25) y nueva línea 387 (MD/23 y MD/26–28). T0.md §9: entradas 158–159. |
| 5 Inconsistencias en informes | **RESUELTA** | `phase1_session_report.md`: 953 entradas, sin «no MD/28 series», preflight del tramo 3 (6.636 % / 14 % / 177) y `SESSION_SEGMENT_3`. `phase1_disassembly_evidence.md`: «calls=N=31». T0.md:54 ya no menciona `e_pico_mac.csv`. Lo único sin cambiar es que `src\build` sigue en lugar de `build\`; es cosmético y no bloquea. |
| 6 Higiene | **RESUELTA en lo relevante** | Los archivos sueltos de A1 se movieron a `BACKUP_T0_RESTART\ROOT_LOOSE_2026-10-11\` (5 archivos). La raíz `C:\Projects\OMEGA` ya no tiene archivos sueltos. `ESTADO_PROYECTO.md` está actualizado a 2026-10-11. Sigue sin marcar el obsoleto `Ejercise\ROADMAP_FREEZE_RECORD.md`; está fuera de T0.0 y no bloquea. |

### Integridad de datos y kernel
- Volví a recalcular todo desde los crudos y los valores son idénticos a la primera inspección: 79 series, TEAM 8P 133.398e9 / 137.703e9, `BW_DRAM_read_8P` 74.4558e9, `D_read` 1.007798 / 1.040325, P_instr, E-core y P-core m=1.
- **Guard de M**: está solo en `validate_inputs`, en `q4_kernel.cpp:259-264`, que se ejecuta antes del timer.
  - `execute_output_block`, `execute_core_fixed_m`, `execute_core_m1` y las entradas batch (`:96-173`) son textualmente idénticos a la versión inspeccionada. El hot path no cambió.
  - La compilación QA 42 y la correctness 24 dan `summary PASS`; los 11 fixtures dan PASS.
  - Observación sin efecto en el veredicto: el binario cronometrado `phase1_measure_harness.exe` (hash `436123B2…`, junto con su `.obj` y `.asm`) se compiló antes del guard. En disco ya no está el `.cpp` exacto que lo produjo; solo existe la versión con el guard, cuya diferencia está fuera del hot path. Así consta en T0.md §9:159. Es aceptable porque el binario y el listado de ensamblador medidos se conservan con su hash.

### Nueva desviación

**R1 (TRIVIAL, mecánica): SHA256SUMS no da 953/953, da 952/953.**
- La línea `../T0.md` (`812CB64D…`) no coincide.
- T0.md se editó a las 20:27:02, después de regenerar SHA256SUMS a las 20:26:09. Su hash actual es `AD62D3E9…`.
- Esto incumple MD/28 §13 F («todos los hashes/verificadores PASS»). Ningún artefacto de T0 está afectado.
- Corrección mínima: regenerar la entrada de `../T0.md` (o regenerar el archivo completo tras la última edición de la spec) y comprobar de nuevo 953/953.

### VEREDICTO DE REINSPECCIÓN

**`T0.0 = COMPLETED`, condicionado únicamente a corregir R1.**

Las desviaciones 1–6 están resueltas y el hot path, los datos crudos y las cifras siguen intactos. Si después de regenerar SHA256SUMS la verificación da 953/953 sin otros cambios en artefactos de T0, la fase pasa a `INSPECCIONADA_POR_OPUS` (Completed) sin otra inspección. Si cambia cualquier artefacto distinto de SHA256SUMS o de la línea de la spec, hay que volver a inspeccionar.

Pendientes que no bloquean: informar a Sol del incidente del preflight del tramo 2 junto con el paquete de T0.1, y archivar `ROADMAP_FREEZE_RECORD.md`.
