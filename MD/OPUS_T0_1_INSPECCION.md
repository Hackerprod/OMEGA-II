# Inspección Opus — Fase T0.1 / H0 (contexto limpio, 2026-10-11)

Normas leídas: `Roadmap.md` (tablero, §0, §1, §4.2, §4.3, §5.2.1–5.2.6, §6.1, §7), `Conversacion.md` (L2664-2892), `T0.md` (§2, §4, §4.1, §9), `MD\22.md`, `MD\29.md`–`MD\32.md`.
Artefactos: `T0\src\h0_*`, `T0\src\build\build_h0_scaffold.cmd`, `T0\results\T0.1\*`, los 222 archivos de `h0_raw\h0_official_20261011b\`, `T0\logs\COMMANDS.json`, `T0\logs\h0_*`, `T0\SHA256SUMS.txt`.
Método: solo lectura. Recalculé desde los CSV crudos por aplicación (≈10 GB, 73 series) con un script propio fuera del proyecto. Verifiqué los 1620 hashes y revisé código, cronología y logs.
Hardware: los cambios Ryzen → i7-13700F (8P+8E, L2 2 MiB/P, 4 MiB por cluster E, LLC 30 MB, VNNI, Windows 11, C:) se tratan como legítimos cuando son traslación de capacidad. Abajo se separan de las desviaciones científicas.

## VEREDICTO

**`T0.1 = DESVIACIONES`**

- Los datos crudos son íntegros y las cifras del tablero coinciden con ellos.
- El control de medición es válido: R_noise 0.0128, expulsión PASS, δ_BC ≤ 0.009.
- `H0-R1 FAIL` y `H0-R4 FAIL` son robustos. Fallan con el criterio preregistrado (MD/29) y también con el criterio original de `Conversacion.md` (ρ marginal 0.914 en K=8 y 0.906 en K=16, frente a ρ ≤ 0.50).
- Por tanto, `H0_FAIL` no depende de ninguna de las desviaciones encontradas.
- Aun así, hay una desviación MAYOR en una cifra reportada: `H0-K PASS` es un artefacto de una métrica sin normalizar. Debe elevarse a Sol antes de la adjudicación.
- Hay además desviaciones de proceso y de documentación.
- Según Roadmap §4.3, la fase vuelve a EN_CURSO hasta corregirlas. **No hace falta volver a medir.**

---

## 1. Desviaciones

### D-1. MAYOR — `H0-K PASS` y la «rodilla» 294,912–524,160 B son un artefacto. No hay transición física en los datos.

- **Norma:**
  - Conv.L2867: «Debe aparecer una transición visible».
  - Conv.L2876: «Si no aparece ninguna discontinuidad o degradación progresiva…».
  - Roadmap §5.2.5: «cambio abrupto al cruzar la capacidad útil de L2».
- **Implementación:**
  - MD/29 §10.2 y T0.md §4.1: «rodilla confirmada en `W_j` si c_A(W_j)≥1.15·c0».
  - `c_X` es el «coste kernel-only por aplicación», sin normalizar por bytes ni por MAC.
  - Código: `T0\src\h0_derive.py:101-115`. Resultado: `h0_derived_h0_official_20261011b.json` → `H0_K`.
- **Problema:** el trabajo por aplicación crece en proporción a W (O filas × 512). Cualquier salto de W mayor del 15 % «pasa» de forma trivial.
  - El primer salto de 294,912 a 524,160 B multiplica el trabajo por 1.777.
  - El coste observado sube 1.881×.
- **Recalculo normalizado** (coste por byte, A, 8P, K=16, m=1):
  - Va de 101.99 ps/B (262,080 B) a 101.52 ps/B (3,145,824 B).
  - Ninguno de los 11 puntos se aparta más de ±0.7 %.
  - Cociente máximo frente al plateau: **1.0023**.
  - MAC/s por núcleo constante: 17.43–17.55 GMAC/s.
  - Con una lectura física del criterio, **H0-K = FAIL** (sin transición hasta 3,145,824 B).
- **Consecuencias:**
  - `B_L2_operational = 206,424 B` no tiene base física.
  - La frase del tablero «H0-K PASS (rodilla 294,912–524,160 B)» es engañosa.
- **Impacto en el veredicto:** ninguno. H0 falla igual por R1 y R4.
- **Corrección:**
  - Elevar a Sol: la definición preregistrada, aplicada literalmente, es tautológica.
  - Hasta que Sol decida, no registrar «rodilla» ni `B_L2_operational` como hallazgo. Reportar H0-K como «PASS literal / sin transición normalizada».
- **Agravante (nota para Sol):** el barrido A llega como máximo a 8 × 3,145,824 B = 25.2 MB, menos que la LLC de 30 MB. El barrido nunca sale de LLC. Solo podría detectar L2→LLC, y en este kernel esa transición cuesta 0 %. El objetivo «comportamiento L2/L3/DRAM» (Conv.L2675) queda cubierto solo por la fila B.

### D-2. MAYOR (científica, no de hardware; preregistrada por Sol; sin efecto en el resultado) — Criterios de residencia y matrixización cambiados respecto a Conversacion/Roadmap §6.1

| Elemento | Conversacion / Roadmap | MD/29 (T0.md §4.1) | ¿Hardware? |
|---|---|---|---|
| K de adjudicación | «Para d=512, m=1 o 4, K=8» (Conv.L2828) | `K_gate=16` | **Sí, legítimo.** P10/AMB-04: con K=8 B cabe en una LLC de 30 MB |
| Umbral mínimo | «ρ ≤ 0.50 → gate mínimo aprobado» (Conv.L2831); Roadmap §6.1: «[i7] umbrales sin cambio numérico» | `rho_1 ≤ 2/3`; 0.50 pasa a «STRONG» | **No.** Relaja el criterio |
| Señal fuerte | «ρ ≤ 0.25 → evidencia fuerte» | `rho_1 ≤ 0.50` | **No** |
| Definición de c | «c_A(K)=(T_A(K)−T_A(1))/(K−1)» (marginal), «en coste marginal por iteración» (Conv.L2802) | media por aplicación que incluye la 1.ª aplicación fría (MD/32 D3, §7) | **No** |
| ρ | «c_A(K)/c_B(K)» | `max(c_A/c_B, c_A/c_C)` | **No** (más conservador) |
| Matrixización | «MAC/s(m=8 u 16)/MAC/s(m=1) ≥ 1.5» (Conv.L2854) | `S4=P_A(4)/P_A(1) ≥ 1.50` | Parcial. Elegido con datos de T0.0 (m=4 ≥ 95 % del pico) |
| Gates añadidos | — | H0-R4 (`rho_4 ≤ 0.90`), H0-K (1.15×), `delta_BC ≤ 0.15`, `R_noise ≤ 0.05` | No (son controles nuevos) |

- Todo se decidió antes de los datos: MD/29 se guardó a las 03:36 UTC y la primera serie empezó a las 12:15 UTC. Sol tenía autoridad, porque cerró AMB-04/10/13.
- Pero Roadmap §0 dice «Único cambio permitido: la lógica de hardware» y §6.1 dice «umbrales sin cambio numérico». Por eso debe constar como desviación científica autorizada, no como traslación de hardware.
- **Invariancia comprobada por recalculo con los criterios originales:**
  - ρ marginal (definición Conv.L2813-2825) a m=1: K=8 → **0.914**; K=16 → **0.906**. Ambos fallan ρ ≤ 0.50 y están cerca de «ρ ≈ 1.00 → no existe ventaja útil».
  - S8 = 1.759 y S16 = 1.766: pasan el mínimo original (≥ 1.5) y no llegan a «señal fuerte» (≥ 2.0).
  - El veredicto no cambia.

### D-3. MAYOR (procedimiento, sin efecto en los datos) — T0.1 se abrió antes de que T0.0 estuviera Completed

- **Normas:**
  - Roadmap §4.3: «Ninguna fase inicia sin la anterior Completed».
  - MD/22 §6: «Deben resolverse únicamente después de que T0.0 alcance Completed».
  - MD/28, recogido en el Roadmap §7: «las 13 AMB de T0.1 se presentan a Sol solo con T0.0 Completed».
- **Hechos en disco:**
  - `MD\OPUS_T0_0_INSPECCION.md` (03:28 UTC) dictamina «T0.0 = DESVIACIONES → EN_CURSO».
  - `MD\29.md` (respuesta de Sol a las 13 AMB) se guardó a las 03:36 UTC, 8 minutos después.
  - En `T0.md` §9, el registro de MD/29 («T0.1 = EN_CURSO») está **antes** de «Correcciones documentales… T0.0 → VERIFICADA_POR_JUEZ».
  - `ESTADO_PROYECTO.md` (03:16 UTC) aún decía «se presentan JUNTAS a Sol SOLO cuando T0.0 este Completed».
  - No existe en disco ningún registro de la «Reinspección Opus: COMPLETED» de T0.0 que cita el tablero.
- **Conclusión:** las AMB se presentaron y T0.1 se abrió con T0.0 en EN_CURSO.
- **Impacto científico:** nulo. Las desviaciones de T0.0 eran solo documentales y Sol usó las mismas cifras.
- **Corrección:** registrarlo y aportar el registro de la reinspección de T0.0, o declarar el incidente.

### D-4. MENOR — Estado del tablero: `VERIFICADA_POR_JUEZ` sin veredicto de Sol

- Roadmap §4.3, para IMPLEMENTADA → VERIFICADA: «en T0.1 si AMB-19 resuelve que H0 es un gate propio, además, Sol emite el veredicto científico». AMB-19 resolvió que H0 es gate propio (MD/29 §11).
- El tablero marca VERIFICADA_POR_JUEZ con «H0_FAIL (PROVISIONAL hasta inspección Opus y adjudicación de Sol)».
- MD/29 §21.15–16 pone el veredicto del JUEZ antes de Opus. Esto contradice el Roadmap.
- Además, «FALLIDA_LÍNEA_MUERTA … Sol (veredicto), juez lo registra» (Roadmap §4.3).
- **Corrección:** dejar constancia de esta contradicción y obtener el veredicto de Sol, con D-1 incluido.

### D-5. MENOR — Código cambiado después del freeze y primer lanzamiento oficial abortado, sin registrar

- La sesión `h0_official_20261011` (12:14:20 UTC) abortó en el preflight con «Get-CimInstance : Consulta no válida» (`results\T0.1\h0_official_20261011.stderr.txt`). No hubo timing: `official_timing_performed:false`.
- Después:
  - `src\h0_runner.py` se modificó (12:14:35 UTC).
  - El plan se re-hasheó (12:15:05).
  - Se lanzó `h0_official_20261011b` (12:15:09).
- El kernel, los umbrales, W/K/m y el hot path no cambiaron: los hashes de `h0_scaffold.exe`/`.obj` coinciden con el plan.
- Pero MD/32 §16 autoriza «la única sesión oficial» tras la verificación D1–D5. Ni el relanzamiento ni el parche del runner aparecen en `T0.md` §9 ni en el tablero. El primer intento se conserva (correcto).
- Las correcciones D2/D4 terminaron a las 12:12–12:13 UTC (`logs\h0_D1_D5_audit_*_final`) y el lanzamiento fue a las 12:14. La verificación del juez sobre disco cabe en ≈1 minuto. Ver §4.

### D-6. MENOR — La validación de expulsión solo demuestra expulsión de L2, no de LLC

- Conv.L2786: buffer «para destruir la residencia de L2 y L3».
- El test (`h0_scaffold.cpp:409-431`) corre **un núcleo a la vez** con un barrido de 8 MiB, menor que la LLC de 30 MB.
- La recarga «expulsada» de 288 KiB tarda 2.9–5.2 µs, unos 57–100 GB/s en un núcleo, compatible con aciertos en LLC.
- PASS (mediana 3.136, mínimo 2.636) prueba la expulsión de L2, no la de LLC.
- En las series C, los 8 workers barren a la vez (64 MiB) y δ_BC = 0.0006 indica que C se comportó como B. Sol aceptó el test (MD/32 §10/§14).
- Debe constar en el informe que «expulsión validada» significa expulsión de L2.

### D-7. MENOR — Derivación: celda distinta de la declarada en el plan, y script escrito tras los datos

- El plan declara `"P8_W_sweep_architectural_A": "separate A-only knee-cell record …"`.
- `h0_derive.py:101-102` usa para W=294,912 la celda del barrido K (`05_…K16_m1_A_1`, c=29,934.8) en lugar de `04_P8_Knee_K16_m1_W294912_A_A_1` (c=29,938.9).
- Efecto: c0 pasa de 28,332.2 a 28,334.25. Ninguno en el resultado.
- `h0_derive.py` (13:28 UTC) no estaba en el freeze. Fuera de esa elección de celda, aplica exactamente los umbrales del plan: no hay umbrales cambiados tras ver los datos.

### D-8. MENOR — Derivados y métricas obligatorias incompletos

Lo que pide la norma:
- MD/29 §21.14: «eta_instr(m), fixed-work curves, SMT effect, P/E topology effects».
- MD/29 §14.2: «El coste de barreras y eviction se reporta aparte».
- MD/22: seleccionar `PRIMARY_WORKER_POLICY`.
- MD/29 §17.3: marca `TOPOLOGY_ALTERNATIVE_FOR_T0.2`.
- Roadmap §5.2.5: escalado con núcleos y coste de cada barrera.

El derivado no tiene `eta_instr(m)`, coste de barrera, coste de expulsión, política de workers ni efecto P/E. Los datos crudos sí los contienen (`team_wall_ns`, `initial_eviction_ns`, `eviction_between_ns`). La «Salida» de Roadmap §4.2 («tabla … con las métricas de 5.2.5») está incompleta.

### D-9. MENOR — La fila SMT se reporta con una métrica que no es comparable

- El derivado muestra `c_A_SMT = 57,893.6` junto a `c_A_8P = 29,934.8`.
- `c_A_SMT` suma el tiempo de kernel de dos hermanos que corren a la vez (normalización por núcleo físico, ambigüedad ya señalada en `h0_D1_D5_audit_20261011.json`).
- En tiempo de pared por aplicación, SMT da 31,592.9 ns frente a 31,222.3 ns en 8P: SMT es ≈1.2 % **más lento**, no 1.93× más lento.
- Roadmap §5.2.6, pregunta 6 («¿SMT ayuda o estorba?») necesita la métrica de pared.

### D-10. MENOR — La columna cruda `weight_storage_address` vale 0 en todas las filas oficiales

- La ruta cronometrada `runQ4KApplications` (`h0_scaffold.cpp:295-315`) no rellena `clocks.weight_storage_address`. Solo lo hace `runKApplications`, que se usa en QA.
- Por eso los datos crudos no muestran, aplicación por aplicación, que B usó K asignaciones.
- Evidencia indirecta suficiente:
  - Ambas rutas llaman a `forApplication(variant, app)`.
  - `weight_physical_allocated_bytes(B) = 37,754,880 = 8 × 16 × 294,960`.
  - QA D4 PASS y δ_BC ≈ 0.

### D-11. MENOR — Barridos H0 reducidos frente a Conv.L2677-2684 / Roadmap §5.2.1

- La conversación lista los conjuntos m × K × bytes.
- Lo ejecutado: K solo a m=1; m solo a K=16; W solo en A, K=16, m=1.
- Lo decidió Sol antes de los datos («No se ejecuta un factorial completo», MD/29 §13.1).
- Los puntos W cubren todos los de P9 (512…2560 KiB) más 256, 288 y 3072 KiB. Eso es una traslación de hardware legítima.
- Se retiraron (32,1) y Km=96 (AMB-12). Debe constar como reducción autorizada.

### D-12. DOCUMENTAL

- **Cabecera de `T0.md`:** cita el Roadmap `DB65224D…`. El Roadmap vigente es `EDC0769B…` y el plan registró `A771C3D6…`. El §7 normativo cambió (registro de MD/29) y la cabecera no se revalidó. Es el mismo tipo de hallazgo R1 que en T0.0.
- **`T0.md` §9:** la última entrada dice «Plan H0 sin congelar hasta que el juez verifique D1–D5». Faltan el freeze, el incidente de preflight, la sesión oficial, los resultados y el veredicto provisional.
- **`ESTADO_PROYECTO.md`:** está desactualizado («T0.0 = EN_CURSO», «T0.1 / H0 = HOLD»).
- **`h0_measurement_plan.json`:** tiene `"status":"FROZEN_FOR_OFFICIAL_TIMING"` pero conserva campos contradictorios: `"H0_FREEZE": "HOLD until Sol/JUEZ code review"`, `"CODE_REVIEW": "REQUIRED"`, `"official_timing_performed": false`. También tiene mojibake («Â§»).
- **Tablero, fila T0.0:** dice a la vez «INSPECCIONADA_POR_OPUS (Completed 2026-10-11)» y «pendiente reinspección Opus».
- **Tablero, fila T0.1:** «H0-K PASS (rodilla 294,912–524,160 B)» (ver D-1).

### Cambios de hardware legítimos (no son desviación)

- **Workers:** 8 P físicos sin SMT como primario. E por cluster de 4 con 4 MiB compartida (AMB-03/15).
- **K_gate=16:** P10, LLC de 30 MB.
- **W como fracciones de L2_P:** P9 y P6.
- **Expulsión:** 8 MiB/worker = 64 MiB en agregado, dentro del rango 32–64 MiB de Conv.L2786.
- **ISA:** AVX-VNNI-256 en lugar de AVX-512 (AMB-05).
- **SMT 16LP** en lugar de 8T.
- **Presupuesto L2 medido** en lugar de «0.50–0.70 MiB» (AMB-18).

---

## 2. Cifras reportadas frente a artefactos (recalculadas desde los crudos)

Proceso:
- Recorrí las 73 series (≈ 33 M filas).
- Para cada (rep, worker): sample_cost = Σ kernel_only_ns / (episodios·K).
- team = máximo sobre workers; c = mediana de las 31 reps.
- Para R_noise: pares A/A por (rep, episodio, worker, app) y p95 nearest-rank.

| Cifra | Reportada | Recalculada | Estado |
|---|---|---|---|
| R_noise | 0.012779552715654952 | 0.012779552715654952 (1,253,888 pares, 0 sin pareja; p99 0.118, máx 1.93) | ✓ |
| Expulsión 8 MiB: mediana / mínimo de ratios | 3.13636 / 2.63636 | 3.13636 / 2.63636 | ✓ |
| c_A / c_B / c_C (K=16, m=1) | 29,934.8 / 32,820.4 / 32,800.5 | 29,934.8 / 32,820.4 / 32,800.5 | ✓ |
| rho_1 / δ_BC(m1) | 0.91263 / 0.000607 | 0.91263 / 0.000607 | ✓ (tablero 0.913 / ≈0.0006) |
| c_A / c_B / c_C (K=16, m=4) | 67,958.0 / 68,359.0 / 68,988.5 | idénticos | ✓ |
| rho_4 / δ_BC(m4) | 0.99413 / 0.00917 | 0.99413 / 0.00917 | ✓ (tablero 0.994 / ≈0.009) |
| S4 | 1.76196 | 4·29,934.8/67,958.0 = 1.76196 | ✓ (tablero 1.762) |
| c0, rodilla literal, B_L2_operational | 28,332.2; [294,912, 524,160]; 206,424 | idénticos | ✓ aritméticamente; artefacto (D-1) |
| Barrido W normalizado | — | 101.3–102.0 ps/B; máx/plateau 1.0023 | sin rodilla (D-1) |
| ρ con la definición de Conversacion | — | K=8: 0.914; K=16: 0.906 | H0 falla igual (D-2) |
| S8 / S16 | 1.759 / 1.766 | 1.759 / 1.766 | ✓ |
| Descriptivos 4P, E (×2), P+E, FLOPs constantes (9 pares), barrido E | ver JSON | 73/73 series coinciden con los summaries (≤ 0.05 ns) | ✓ |
| «47 bloques» | 47 | 47 celdas en el estado de ejecución, 73 series crudas | ✓ |

Integridad de los datos crudos:
- Todas las series tienen 31 reps, `status=OK`, `series_attempt=1`, sin reintento técnico y ruta `ISA_OPT_AVX_VNNI_256`.
- Filas = reps × episodios × workers × K en todas las series. La excepción es SMT, donde cada hermano hace 8 de las 16 aplicaciones, como prevé el diseño.
- Las sumas por (rep, worker) coinciden con `sample_kernel_only_total_ns` (0 discrepancias fuera de SMT, que agrega por núcleo físico).
- A y B tienen 0 barridos entre aplicaciones. C tiene barrido antes de cada aplicación > 0.
- El orden aleatorizado (`h0_variant_order_*b.json`) es idéntico byte a byte al de revisión generado a las 05:15 UTC, es decir, quedó fijado antes de los datos.
- El orden de ejecución respeta D1: preflight → expulsión → R_noise → rodilla → K → m → R4 → FLOPs → 4P → SMT → E → P+E.

Bytes físicos (Conv.L2611):
- W_real = 294,912 B por shard.
- Asignado = 294,960 B (+48 B de metadatos).
- Equipo 8P = 2,359,680 B frente al objetivo de 2,359,296 B.
- B con K=16: 37,754,880 B.
- El ejecutable reporta los bytes físicos asignados. ✓

Lectura física, que no es desviación pero conviene que Sol la tenga:
- A m=1, A consume 78.8 GB/s agregados de pesos y B 71.9 GB/s. `BW_DRAM_read_8P` = 74.46 GB/s.
- El kernel consume pesos a casi el mismo ritmo al que la DRAM los entrega (D_read ≈ 1.04 en T0.0). Por eso la ventaja de residencia queda acotada a ≈ 1/D_read.
- A m=4, B baja a 34.5 GB/s: es compute-bound y ρ → 1.
- `H0_FAIL` es coherente con la física de este kernel y esta máquina. La suficiencia del gate de adecuación del kernel (D_read ≥ 1.00, MD/25) la decidió Sol.

---

## 3. Violaciones de reglas

- **Umbrales o definiciones cambiados tras ver los datos: no encontrados.**
  - `measurement_gate_values_from_MD29` del plan (guardado 12:15:05 UTC, antes de la primera serie a las 12:15:24) coincide con MD/29 y con `h0_derive.py`.
  - Las únicas elecciones posteriores a los datos son las de D-7 y no tienen efecto.
  - Los cambios de criterio de D-2 son previos a los datos y los hizo Sol.
- **Modelos sustitutos o reducidos: no encontrados.**
  - Todos los shards son [O,512] Q4 de d=512 con W_real según la tabla de MD/30.
  - 4P, E y P+E usan W_arch(n_w) = B_core/n_w exacto.
  - No se usa d ≠ 512, ni GPU, ni CUDA o torch en `src\h0_*`.
  - Variar W con d=512 fue autorizado por Sol (MD/29 §2.1) como microbenchmark de working set.
- **Higiene:**
  - La raíz de `Ejercise\` está limpia: solo documentos permitidos, sin `T0_*` sueltos.
  - Todo lo de T0.1 está en `T0\results\T0.1\` y `T0\logs\`.
  - El intento abortado, el preflight de prueba y el orden de revisión se conservan con nombre único (correcto).
  - Hay duplicados idénticos por diseño: `session_summary` = `execution_status` y `preflight_summary` = `preflight_status`.
  - Excepciones: la documentación desactualizada de D-12 y el incidente sin registrar de D-5.
- **Hashes:**
  - `SHA256SUMS.txt`: **1620/1620 OK**, 0 ausentes y 0 archivos de `T0\` fuera del manifiesto, incluidos los ≈ 10 GB de CSV crudos.
  - `artifacts_sha256` del plan: los 27 fuentes y binarios coinciden con disco. Solo `../Roadmap.md` difiere, por la actualización posterior del tablero (esperado).
  - El kernel `q4_kernel.cpp` del build H0 (`3955E7BC…`) lleva el guard de M fuera del hot path añadido al final de T0.0, y no es byte a byte el fuente cronometrado en T0.0. MD/31 §2 condicionaba la no-revisión del kernel a «si su SHA/binario coincide exactamente con el kernel T0.0». No encontré constancia de que Sol fuera informado de esa diferencia. Severidad: menor; ya registrada en T0.md §9 para T0.0.

---

## 4. Lo que no pude verificar

1. La **reinspección Opus de T0.0** citada en el tablero («COMPLETED», 953/953): no existe ningún archivo, hora ni contenido en disco.
2. Que el juez verificara D1–D5 sobre disco antes del freeze. Las correcciones terminaron a las 12:12–12:13 UTC y el lanzamiento fue a las 12:14. El plan registra `FROZEN_FOR_OFFICIAL_TIMING`, pero no hay acta de esa verificación.
3. El contenido del parche de `h0_runner.py` (D-5): no hay versión previa en disco para comparar. Su hash posterior sí coincide con el plan.
4. Que `h0_scaffold.exe` se reconstruya bit a bit desde los fuentes hasheados. Hay `/Brepro` y los hashes coinciden con el plan, pero no recompilé.
5. Que las copias B sean páginas físicas distintas (sin PFN, por diseño; MD/32 D4) y que C haya expulsado la LLC (D-6). Solo hay evidencia indirecta (δ_BC).
6. Contadores L2/LLC/DRAM: `UNAVAILABLE`, como permite MD/29 §18. No puedo afirmar «B vino de DRAM», solo que su comportamiento fue no residente y equivalente a C.
7. Si Sol conoce D-1 (H0-K sin normalizar) o el desajuste de MD/31 §2 sobre el kernel. No hay MD posterior a 32.
8. `Conversacion.md` lo consulté por las secciones pertinentes (L2664-2892 y las citadas en el Roadmap), no completo.

---

## Reinspección (2026-10-11, ≈07:05 hora local del i7)

Método: solo lectura, salvo esta sección.
- Leí `MD\33.md`, `MD\34.md`, `T0\src\h0_corrections.py`, `T0\results\T0.1\h0_corrections_h0_official_20261011b.json`, la «Reinspección» de `MD\OPUS_T0_0_INSPECCION.md`, y los diffs de `Roadmap.md`, `T0.md`, `ESTADO_PROYECTO.md` y `T0\SHA256SUMS.txt` contra el commit `27d420b` (posterior a mi inspección; su manifiesto coincide con el que verifiqué entonces: 1620 entradas).
- Recalculé el manifiesto completo y comprobé a mano la elección de celdas, ρ marginal y P_instr.
- Cambios desde mi inspección: 4 archivos modificados (los tres documentos y el manifiesto) y 4 nuevos (`MD\33.md`, `MD\34.md`, `h0_corrections.py` y su JSON). Ningún archivo de `src\` ni de `results\` previo cambió.

### R-1. Estado de los hallazgos de la inspección anterior

| # | Estado | Evidencia |
|---|---|---|
| D-1 H0-K artefacto | **CORREGIDO en lo sustantivo; residuo documental abierto** | Sol (MD/34 D-B): «`H0_K_SCIENTIFIC = NOT_ESTABLISHED` … `C_L2_knee = null`, `B_L2_operational = null`». Recogido en el JSON de correcciones (`"H0_K_SCIENTIFIC": "NOT_ESTABLISHED"`, `"C_L2_knee": null`), en T0.md §4.1 y en Roadmap §7 («Registro histórico MD/29–34»). **Sigue abierto:** la columna de evidencia del tablero, fila T0.1, conserva «H0-K PASS (rodilla 294,912–524,160 B)» y «veredicto del juez H0_FAIL (PROVISIONAL hasta inspección Opus y adjudicación de Sol)». `ESTADO_PROYECTO.md` §2 conserva la viñeta «K PASS (rodilla 294,912-524,160 B …) … Pendiente: adjudicacion de Sol». Ver N-2. |
| D-2 Criterios cambiados | **DOCUMENTADO** (desviación científica reconocida por Sol) | MD/34 D-C: «Desviación científica de Sol … No ratifico las relajaciones numéricas como consecuencia del hardware». Roadmap §6.1, línea nueva: «Lectura ORIGINAL inequívoca … mínimo `ρ≤0.50`, fuerte `ρ≤0.25`, estimador MARGINAL … `m=8/16`, `K=8`. La adaptación legítima al i7 es solo `K_gate=16`». Roadmap §7 registra MD/29–32 como historia, sin reescribirla. Se ajusta a lo que pide MD/34. Matiz en la cifra reportada: ver N-1. |
| D-3 Apertura prematura de T0.1 | **RETIRADO en lo principal (error mío); residuo documental abierto** | La reinspección de T0.0 sí existe: `MD\OPUS_T0_0_INSPECCION.md` L145, «## Reinspección (2026-10-10, 20:28 hora local …)», con veredicto L177 «`T0.0 = COMPLETED`, condicionado únicamente a corregir R1». Mi afirmación «No existe en disco ningún registro» fue incorrecta, y MD/34 D-D lo rectifica con razón. **Sigue abierto:** MD/34 pide «Debe enlazarse la comprobación histórica de 953/953; si su registro no se conserva, se declara la laguna». No hay log enlazado ni laguna declarada; el tablero solo afirma «953/953». Tampoco consta si R1 se corrigió antes de que MD/29 se guardara a las 20:36. |
| D-4 VERIFICADA sin veredicto de Sol | **CORREGIDO** | MD/34 D-A: «H0_FAIL → FALLIDA_LINEA_MUERTA_PHYSICAL_PREMISE». El tablero dice ahora «VERIFICADA_POR_JUEZ (veredicto H0_FAIL firme, MD/34; Completed pendiente …)». Falta registrar el estado terminal: ver N-2. |
| D-5 Relanzamiento y parche sin registrar | **DOCUMENTADO** | T0.md §4.1: «el primer lanzamiento oficial `h0_official_20261011` abortó en preflight (bug de comillas WMI en `h0_runner.py`, sin series ni datos); parche previo a los datos y relanzamiento como `h0_official_20261011b`». También en ESTADO §2. El contenido del parche sigue sin poder compararse (no hay versión previa); MD/34 lo acepta. |
| D-6 Expulsión solo de L2 | **DOCUMENTADO** | JSON de correcciones, `limitations`: «eviction validation certifies local L2 perturbation, not full LLC eviction»; T0.md §4.1: «expulsión certifica perturbación local (no LLC)». |
| D-7 Celda y autoría de la derivación | **CORREGIDO** | `h0_corrections.py:10` usa `04_P8_Knee_K16_m1_W{w}_A_A_1` para los 11 W; el JSON da `"c_A_dedicated_294912": 29938.9`. T0.md §4.1: «`src/h0_derive.py` fue escrito por el juez DESPUÉS de los datos». El derivado histórico queda intacto (hash `6CE2B602…`). |
| D-8 Derivados incompletos | **PARCIAL — sigue abierto** | Añadido `eta_instr_VNNI_per_core` (η(1)=0.136, η(4)=0.240). Comprobé que `P_instr = 128.68 GMAC/s` es la mediana de los 8 P-core VNNI de `phase1_P_instr_by_core.csv`; el script no documenta que es una mediana. **Abierto:** (a) coste de expulsión. El JSON declara «eviction/barrier costs: no separate instrumentation», pero los crudos sí tienen `initial_eviction_ns` y `eviction_between_ns`. MD/34 pide «Los costes de expulsión … deben identificarse con su alcance real» y solo admite declarar la limitación «donde no haya instrumentación separada», que es el caso de las barreras, no de la expulsión. (b) `PRIMARY_WORKER_POLICY`, efecto P/E y `TOPOLOGY_ALTERNATIVE_FOR_T0.2` (T0.md §4.1 y L127, MD/22) no aparecen ni seleccionados ni declarados como no aplicables tras H0_FAIL. |
| D-9 Métrica SMT | **DOCUMENTADO** | JSON `SMT_note` y T0.md §4.1: «SMT ≈1.2 % más lento; no 1.93×». La cifra se cita de mi recálculo y del de Sol; el script no la recalcula desde los crudos. Es aceptable. |
| D-10 `weight_storage_address=0` | **DOCUMENTADO, con imprecisión** | JSON: «weight_storage_address=0 in several rows: NOT RECORDED». En realidad vale 0 en **todas** las filas oficiales cronometradas, no en «varias». |
| D-11 Barridos reducidos | **PARCIAL** | Roadmap §7 registra «barrido W reducido» como desviación. No menciona que K solo se barrió a m=1 y m solo a K=16, aunque MD/34 D-C pide que «El barrido reducido también debe constar como modificación científica de alcance». |
| D-12 Documental | **PARCIAL** | Corregidos: cabecera de T0.md (ahora cita `3D6E4FE1…`, que coincide con el Roadmap en disco) y fila nueva en §9. **Abiertos:** (a) `h0_measurement_plan.json` sigue teniendo `"H0_FREEZE": "HOLD until Sol/JUEZ code review"`, `"CODE_REVIEW": "REQUIRED (MD/31 §2)"` y `"official_timing_performed": false`; MD/34 pide «reconciliar los campos contradictorios del plan mediante el registro de ejecución» y no existe ese registro. (b) La fila T0.0 del tablero sigue diciendo a la vez «Completed» y «pendiente reinspección Opus». (c) Fila T0.1: ver D-1 y N-2. |
| Nota MD/31 §2 (kernel con guard ≠ fuente cronometrado en T0.0) | **CERRADO** | MD/34 D-D: «El guard de M fuera del tramo cronometrado … quedan reconocidos … sin requerir remedición». |

### R-2. Desviaciones nuevas introducidas por las correcciones

**N-1. MENOR — La «lectura original» de ρ en K=16 usa el operador de MD/29, no el de Conversacion.**
- **Conversacion (Conv.L2813-2825):** «ρ = c_A(K)/c_B(K)», con coste marginal.
- **Roadmap §6.1 (línea nueva):** «ρ marginal K=8 ≈0.914, K=16 ≈0.907». T0.md §4.1: «K=16 ≈0.907 (Sol 0.90630)».
- **Código:** `h0_corrections.py:19-21` calcula `rho_marginal=max(mA / mB, mA / mC)`, que es el operador de MD/29, y para C usa como referencia K=1 la de A (`mC = (T(C) - A[1])/(K-1)`). Ninguna norma define esa referencia.
- **Recalculo:** K=16, mA/mB = 29,771.05/32,848.86 = **0.90631** (coincide con Sol) y mA/mC = 0.90689. A K=8 el máximo es A/B (0.91402), por eso allí coinciden.
- **Efecto:** ninguno en el veredicto; ambos valores fallan ρ ≤ 0.50. Pero la cifra etiquetada «original» no es la definición original, y la discrepancia con Sol se anota sin explicarla.
- **Corrección:** reportar 0.90630 como lectura original (c_A/c_B) y, si se quiere, 0.90689 aparte, etiquetado como «max A/B, A/C con referencia K=1 de A».

**N-2. MENOR (documental) — Tablero y estado incoherentes con MD/34.**
- Roadmap §4.3: «FALLIDA_LÍNEA_MUERTA … Sol (veredicto), juez lo registra».
- MD/34 da el veredicto, pero la columna ESTADO de T0.1 sigue en `VERIFICADA_POR_JUEZ` y no registra `FALLIDA_LINEA_MUERTA_PHYSICAL_PREMISE` como estado, ni siquiera como pendiente de Completed.
- La fila T0.2 tiene ESTADO `PENDIENTE`, mientras su evidencia dice «NOT_STARTED».
- La evidencia de T0.1 y `ESTADO_PROYECTO.md` §2 mantienen el texto retirado «H0-K PASS (rodilla …)» y «PROVISIONAL … Pendiente: adjudicacion de Sol», junto al texto nuevo que lo contradice.

**N-3. MENOR — Limitación declarada inexacta.** Es lo que se describe en D-8(a): el registro de correcciones declara «sin instrumentación» para la expulsión, que sí está instrumentada. Eso contradice el mandato de MD/34 D-D.

**N-4. DOCUMENTAL — Frase de la cabecera de T0.md obsoleta.** La cabecera cita el hash nuevo `3D6E4FE1…`, pero mantiene «El hash citado corresponde al Roadmap tras registrar MD/22 y antes de declarar T0.0 EN_CURSO», que ya no es cierto. Además, el Roadmap cambió su contenido normativo (§6.1 y §7) y no consta una revalidación explícita de la spec contra él, que la propia cabecera exige.

**N-5. HIGIENE — Fuera del manifiesto y manifiesto desfasado.**
- `T0\src\__pycache__\h0_derive.cpython-313.pyc` (generado a las 06:57 al importar `h0_derive`) está fuera de `SHA256SUMS.txt`.
- `ESTADO_PROYECTO.md` se editó a las 07:00:46, 5 s después de regenerar el manifiesto (07:00:41), y su entrada ya no verifica. Es el mismo patrón que R1 de T0.0.

Sin desviación nueva (comprobado):
- No hay umbrales nuevos ni gates retroactivos. Sol se negó expresamente a convertir la normalización en gate, y el JSON la etiqueta `H0_K_diagnostic_only_not_a_gate`.
- No hay nuevas mediciones. Ninguna fuente cronometrada cambió.
- El K-sweep tiene una sola celda por (K, variante), así que el script de correcciones no pudo elegir celdas.
- Los cambios de hardware siguen tratados como antes. Solo `K_gate=16` se ratificó como traslación legítima.

### R-3. Hashes e inmutabilidad

- **`SHA256SUMS.txt` recalculado:** 1626 entradas, **1625 OK y 1 FALLO**: `../ESTADO_PROYECTO.md` (manifiesto `6D8D32A2…`, disco `296F44B8…`), por N-5. Los 222 archivos de `h0_raw\h0_official_20261011b\` (12 GB) dan OK.
- El único archivo de `T0\` fuera del manifiesto es el `.pyc` de N-5.
- Nota técnica: el manifiesto usa CRLF; `sha256sum -c` necesita quitar los `\r`.
- **Al publicar esta sección, la entrada `../MD/OPUS_T0_1_INSPECCION.md` (`97A007AB…`) dejará de verificar.** Es esperado: hay que regenerarla.
- **Crudos y derivado histórico sin modificar:**
  - `git diff 27d420b -- T0/SHA256SUMS.txt` solo cambia las líneas `../ESTADO_PROYECTO.md`, `../Roadmap.md`, `../T0.md` y `../MD/*`, y añade las dos de `h0_corrections`.
  - Las 1610 entradas restantes, incluidos los 222 crudos, el plan (`5107EC8B…`), `h0_execution_status_…b.json` (`5C638946…`) y `h0_derived_h0_official_20261011b.json` (`6CE2B602…`), son idénticas byte a byte a las del manifiesto que verifiqué en la inspección anterior. Sobre disco dan OK.
  - Los mtimes de los crudos están entre 05:15 y 06:24 locales, todos anteriores a la derivación.
  - `h0_corrections.py:46` hace `assert not p.exists()` y escribe solo un archivo nuevo.
  - Coinciden también con los hashes que cita Sol en MD/34. Su tabla tiene dos erratas de transcripción (`…deب5b3…` en `h0_scaffold.cpp` y `…aafeCD04…` en el plan), pero los valores reales coinciden con el manifiesto.

### R-4. Veredicto de reinspección

**`T0.1 = DESVIACIONES`**: solo menores o documentales. El veredicto científico `H0_FAIL` (MD/34) y todos los datos están intactos, y no hace falta volver a medir.

Lista de pendientes para pasar a Completed:
1. **N-1:** reportar ρ original en K=16 = 0.90630 (c_A/c_B). Separar el 0.90689 como `max(A/B, A/C)` con referencia K=1 improvisada.
2. **N-2 / D-1 residual:** limpiar la evidencia del tablero T0.1 y la viñeta de ESTADO (quitar «H0-K PASS (rodilla…)» y «PROVISIONAL»). Registrar el estado terminal según §4.3 y alinear la columna ESTADO de T0.2 con NOT_STARTED.
3. **N-3 / D-8:** reportar el coste de expulsión con su alcance, desde `initial_eviction_ns` y `eviction_between_ns`, y declarar como no instrumentadas solo las barreras. Declarar `PRIMARY_WORKER_POLICY`, P/E y `TOPOLOGY_ALTERNATIVE_FOR_T0.2`, como selección o como «no aplicable por H0_FAIL», con base citada.
4. **D-12(a):** registro de ejecución que reconcilie `H0_FREEZE`, `CODE_REVIEW` y `official_timing_performed` del plan, sin modificar el plan.
5. **D-3 residual:** enlazar la verificación 953/953 posterior a R1 de T0.0 o declarar la laguna. Corregir la fila T0.0 del tablero («pendiente reinspección Opus»).
6. **D-11:** registrar también como reducción de alcance que K solo se barrió a m=1 y m solo a K=16.
7. **N-4:** actualizar la frase de la cabecera de T0.md y dejar constancia de la revalidación contra el Roadmap `3D6E4FE1…`.
8. **N-5:** excluir o hashear el `.pyc` y regenerar el manifiesto después de la última edición (ESTADO y esta inspección). Verificar 100 %.
9. **D-10:** cambiar «several rows» por «todas las filas oficiales cronometradas».

Si solo cambian documentos, el manifiesto y registros nuevos separados (sin tocar crudos, plan, derivado histórico ni fuentes cronometradas), bastará una comprobación puntual de estos 9 puntos para pasar a Completed.

### Comprobación puntual (2026-10-11, tras las correcciones documentales)

| # | Estado | Evidencia |
|---|---|---|
| 1 N-1 | CORREGIDO | JSON `marginal_rho_reconstruction.16`: `rho_original_conversacion_cA_over_cB` = 0.90630 y `rho_max_AB_AC_MD29_operator` aparte, con la nota «improvised, no norm defines it». En Roadmap §6.1 y T0.md §4.1: «K=16 0.90630 (c_A/c_B, definición original; … 0.9069, solo informativo)». |
| 2 N-2 / D-1 | CORREGIDO, con un defecto nuevo | Tablero T0.1: «VERIFICADA_POR_JUEZ → FALLIDA_LINEA_MUERTA_PHYSICAL_PREMISE (veredicto de Sol, MD/34…)» y «H0-K: PASS literal histórico pero `NOT_ESTABLISHED`». Se quitaron «(rodilla …)» y «PROVISIONAL». T0.2 y T0.3 están en NOT_STARTED, igual que en ESTADO §2. **Defecto nuevo (N-6):** la ruta de esa misma celda contiene un retorno de carro suelto, `T0\results\T0.1\…`. `Roadmap.md` pasa a tener terminadores mixtos («CRLF, CR line terminators»). |
| 3 N-3 / D-8 | PARCIAL | La limitación está corregida: «eviction cost IS instrumented … pure barrier/sync overhead is NOT separately instrumented». `PRIMARY_WORKER_POLICY`, P/E y `TOPOLOGY_ALTERNATIVE_FOR_T0.2` constan como «NO APLICABLE tras H0_FAIL» (T0.md §4.1), lo que acepto. **Abierto:** el coste de expulsión no se reporta con valores, aunque MD/29 §14.2 dice «El coste de barreras y eviction se reporta aparte» y los datos ya existen. Hacerlo no requiere medir. |
| 4 D-12(a) | CORREGIDO | T0.md §4.1, «Registro de ejecución (reconcilia los campos contradictorios del plan sin modificarlo) …». El plan no cambió. |
| 5 D-3 | CORREGIDO, con matiz | Tablero T0.0: «reinspección Opus: COMPLETED (… 2026-10-10 20:28 …) … laguna declarada, sin cronología inventada». Matiz: dice «verificación 953/953 posterior a R1 declarada allí», pero la reinspección solo fijaba el 953/953 como condición; no lo da por hecho. |
| 6 D-11 | CORREGIDO | T0.md §4.1: «alcance reducido: K solo se barrió a m=1 y m solo a K=16». |
| 7 N-4 | CORREGIDO | La cabecera de T0.md cita `23533F45…`, que coincide con el Roadmap en disco, y deja constancia de la revalidación del 2026-10-11. |
| 8 N-5 | CORREGIDO | `__pycache__` eliminado. Con él salieron del manifiesto 10 `.pyc` que estaban hasheados. Es bytecode regenerable, no evidencia, así que no hay efecto. No queda ningún archivo de `T0\` fuera del manifiesto. |
| 9 D-10 | CORREGIDO | JSON: «weight_storage_address=0 in ALL official timed rows». Lo mismo en T0.md §4.1. |

**Hashes:**
- Recalculé `SHA256SUMS.txt`: **1616/1616 OK**, incluidas las entradas `../` de ESTADO, Roadmap, T0.md y MD.
- Frente al manifiesto de la reinspección, solo cambian tres cosas: las 10 entradas `.pyc` retiradas, `h0_corrections.py` y su JSON (regenerado).
- Los 222 crudos, el plan (`5107EC8B…`), `h0_execution_status_…b.json` (`5C638946…`) y el derivado histórico (`6CE2B602…`) son idénticos.
- `h0_derive.py` es idéntico al del commit 27d420b.

**Veredicto final: `T0.1 = DESVIACIONES`.** Son dos, ambas menores y sin efecto científico; `H0_FAIL` sigue firme:
1. **N-6 (documental):** retorno de carro suelto que rompe la ruta `T0\results\T0.1\…` en la celda de evidencia T0.1 del tablero de `Roadmap.md`.
2. **D-8 residual (menor):** falta reportar el coste de expulsión con valores y alcance, a partir de `initial_eviction_ns` y `eviction_between_ns` (MD/29 §14.2), sin medir de nuevo.

Si se corrigen solo esos dos puntos y se regenera el manifiesto, T0.1 queda CONFORME sin otra inspección. La frase del 953/953 (punto 5) conviene precisarla, pero no bloquea.
