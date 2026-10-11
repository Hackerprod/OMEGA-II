# Análisis independiente de H0 (T0.1, sesión `h0_official_20261011b`)

**Autor:** Opus, analista independiente. **Fecha:** 2026-10-11.
**Método.** Solo lectura. No modifiqué ningún archivo del proyecto salvo este informe. No ejecuté mediciones de rendimiento.

**Qué recalculé**
- Leí los CSV crudos por aplicación (R1 A/B/C, R4 A/B/C y K=1 A/B) con un script propio en el scratchpad. También leí los `summary.csv` de 4P, E-cluster, P+E y del barrido W.
- Desensamblé `T0\src\build\h0_q4_kernel.obj` (el objeto enlazado en `h0_scaffold.exe`) con `dumpbin`, porque `q4_kernel.cpp` es posterior al listado `phase1_q4_kernel.asm` (20:20 frente a 19:46). La mezcla de instrucciones del bucle interno coincide en ambos.
- Saqué latencias, throughputs y puertos de Alder Lake-P/E de la base de datos de uops.info.

**Qué leí**
- MD\34, 29, 30 y 32.
- T0.md §3.1 y §4.1.
- MD\OPUS_T0_1_INSPECCION.md.
- `h0_derived_…`, `h0_corrections_…`, `h0_eviction_cost_…` y `h0_measurement_plan.json`.
- `h0_scaffold.cpp`, `q4_kernel.cpp/.hpp` y `phase1_instr_x64.asm`.
- Handoff y CSV de T0.0.
- Conversacion.md L2660–2900.

**Convención.** Marco cada afirmación como:
- **[DEMOSTRADO]**: se sigue de los datos o del código.
- **[COMPATIBLE]**: los datos lo admiten, pero no lo prueban.
- **[ESTIMACIÓN]**: modelo propio con supuestos explícitos.

---

## A. Análisis causal: por qué la reutilización solo ahorra ≈9 %

### A.1 Cifras decisivas recalculadas

W = 294,912 B por worker, K=16, VNNI. `BW_DRAM_read_8P` = 74.456 GB/s (T0.0). `D` = demanda lógica agregada / 74.456 GB/s.

| Celda | Coste por app (ns) | GB/s por núcleo | GB/s agregado | ps/B | D |
|---|---|---|---|---|---|
| 8P A, m=1 (amortizado K=16) | 29,934.8 | 9.852 | 78.81 | 101.50 | 1.059 |
| 8P A, m=1, apps 2..16 (crudo) | ≈29,698 | 9.93 | 79.4 | 100.7 | 1.067 |
| 8P A, m=1, 1.ª app (fría, crudo) | ≈32,340 | 9.12 | 72.9 | 109.7 | 0.98 |
| 8P K=1 (A = B, todo frío) | 32,191 | 9.161 | 73.29 | 109.15 | 0.984 |
| 8P B, m=1 | 32,820.4 | 8.986 | 71.89 | 111.29 | 0.965 |
| 8P C, m=1 | 32,800.5 | 8.991 | 71.93 | 111.22 | 0.966 |
| 4P A (W=589,824) | 59,702.6 | 9.879 | 39.52 | 101.22 | 0.531 |
| 4P B | 64,489.4 | 9.146 | 36.58 | 109.34 | 0.491 |
| E-cluster0 A (4 E, W=589,824) | 151,516 | 3.893 | 15.57 | 256.9 | 0.209 |
| E-cluster0 B | 173,266 | 3.404 | 13.62 | 293.8 | 0.183 |
| P+E: P-cores A (W=147,456) | 15,208 | 9.70 | — | — | — |
| P+E: P-cores B | 20,141 | 7.32 | ≈81.6 (16 workers) | — | ≈1.10 |
| 8P A, m=4 | 67,958 | 4.340 | 34.72 | 230.4 | 0.466 |
| 8P B, m=4 | 68,359 | 4.314 | 34.51 | 231.8 | 0.464 |

**Suelo DRAM para 8P a m=1.** 8·294,912 B / 74.456 GB/s = **31,687 ns por aplicación** [DEMOSTRADO: aritmética]. B (32,820) va al 96.5 % de ese ancho de banda.

### A.2 El kernel está limitado por los puertos vectoriales (desempaquetado y conversiones), no por memoria [DEMOSTRADO en lo esencial]

**1) El coste por byte no depende de dónde residen los pesos mientras estén en caché.**
- En A, el barrido W da 101.28–101.99 ps/B entre 262 KB y 3.15 MB por worker. Ese rango cubre L2 (2 MiB) y LLC (agregado 25.2 MB < 30 MiB).
- En los E-clusters ocurre lo mismo: 256–257 ps/B al pasar de 1 MiB a 5.2 MB de agregado sobre 4 MiB de L2 compartida.
- **Conclusión.** A ≈9.9 GB/s por núcleo, la LLC alimenta al kernel igual que la L2. La residencia *en L2* no aporta nada medible frente a la residencia en LLC con este kernel. La única penalización observada aparece cuando los datos vienen fríos (B, C, primera aplicación de A).
- Esto es exactamente el caso que Conversacion.md L2876–2881 describe como «el kernel no está usando la jerarquía como se esperaba».

**2) Mezcla de instrucciones del bucle VNNI m=1** (desenrollado ×4 grupos, en `h0_q4_kernel.obj`). Por cada grupo de 32 MAC:
- **Decode:**
  - 2 `vmovq` (cargas);
  - 2×(`vpsrlw`, 2 `vpand`, `vpunpcklbw`, `vpmovzxbw`, `vpsllw`, `vpsraw`).
- **Activaciones:** 2 `vpmovsxbw` desde memoria. Se re-extienden en signo **en cada fila**, 1024 veces por aplicación, aunque son las mismas.
- **MAC:** 2 `vpdpwssd`. Es la forma int16×int16 de 16 MAC por instrucción, no `vpdpbusd` (32 MAC).
- **Escala escalar:**
  - 2 `movzx`, `shl`, `or`, `vmovd`;
  - `vcvtph2ps`, `vmulss`, `vbroadcastss`;
  - `vcvtdq2ps`, `vmulps`, `vaddps`.

**Clasificación por puertos** (uops.info, ADL-P):
- solo p01: 12 µops;
- solo p5: 7;
- p15: 3;
- p015: 4.

Son **26 µops vectoriales por grupo sobre 3 puertos, es decir, ≥ 8.67 ciclos por grupo**. Solo 2 de las 26 µops (7.7 %) son MAC.

**3) Medido.** 17.51 MAC/ns por núcleo. La frecuencia efectiva no está medida (T0.0: `UNAVAILABLE`). Dos inferencias posibles:
- **Desde el `P_instr` VNNI:** 8 cadenas `vpdpwssd` independientes con latencia 5 y TP 0.5 dan 1.6 instr/ciclo = 25.6 MAC/ciclo, de donde 128.68/25.6 ≈ 5.03 GHz.
- **Desde el `P_instr` AVX2:** ≈4.75 GHz.

Con eso, el kernel gasta **8.7–9.2 ciclos por grupo, el 94–100 % de la cota de puertos** [ESTIMACIÓN con frecuencia inferida; el orden de magnitud es robusto].

**Corroboraciones independientes** [COMPATIBLE]:
- **SMT:** no da ganancia (≈1.2 % más lento). Encaja con puertos saturados compartidos por los dos hermanos.
- **AVX2 frente a VNNI:** solo un 3 % de diferencia (16.8 frente a 17.3 GMAC/s). La instrucción MAC no es el cuello de botella.

**m=4.** Con la misma clasificación salen ≈50 µops por grupo (128 MAC) y una cota de 16.7 ciclos. El medido es ≈20.8 ciclos (≈80 % de la cota).
- 8 de esas 50 µops son de nuevo `vpmovsxbw` de activaciones por fila.
- 8 más son escala escalar (`vmulss` y `vbroadcastss`) por slot.
- Por eso S16 ≈ S4 = 1.76: el trabajo por slot domina.

**4) Eficiencia frente al pico real.** El pico de puertos de VNNI-256 en Raptor Cove es 2 instr/ciclo (uops.info: `VPDPBUSD`/`VPDPWSSD` ymm con TP 0.5 en p01).
- Eso da 64 MAC/ciclo con `vpdpbusd` (≈322 GMAC/s a ≈5.03 GHz) o 32 con `vpdpwssd` (≈161).
- El kernel rinde **≈5.4 % del pico `vpdpbusd`** a m=1 y ≈9.6 % a m≥4.
- **Defecto menor de T0.0.** `P_instr` (128.68) se midió con 8 acumuladores y latencia 5. Está **limitado por latencia, no por throughput**, en contra de la regla de T0.md §3.1 («throughput, no latencia»). Infraestima el pico de `vpdpwssd` ≈20 %, y por tanto `eta_instr` está inflado ×1.25. No afecta a H0.

### A.3 De dónde sale el coste de B: dos componentes

**(i) Sobrecoste por núcleo del streaming en frío, presente sin saturar la DRAM** [DEMOSTRADO como fenómeno; mecanismo COMPATIBLE]
- **4P:** B mueve 36.6 GB/s agregados (49 % del ancho de banda) y aun así es 8.0 % más lento que A. Eso son 8.1 ps/B, ≈33 ns por página de 4 KiB.
- **E-cluster:** B mueve 13.6 GB/s (18 %) y es 14.4 % más lento (36.9 ps/B, ≈151 ns por página).
- **8P:** 9.8 ps/B (≈40 ns por página).
- **Lectura.** La mayor parte del 9 % de 8P (≈83 %, 8.1 de 9.8 ps/B) ya existe con 4 núcleos, sin saturación de ancho de banda.
- **Mecanismo compatible.** El streamer de L2 no cruza páginas de 4 KiB [fuente 8, medido en Coffee Lake, no en Raptor Cove], así que cada página nueva arranca con fallos de demanda parcialmente solapados por el OoO. Los TLB fríos son otra contribución posible. No hay contadores que lo prueben.

**(ii) Contención agregada de ancho de banda DRAM** [DEMOSTRADO como dependencia; magnitud COMPATIBLE]
- En 8P, B va al 96.5 % del ancho de banda medido. El sobrecoste por byte sube de 8.1 a 9.8 ps/B frente a 4P.
- **Prueba directa del mecanismo roofline:** en P+E (16 workers), la demanda agregada de A sería ≈109 GB/s (D ≈ 1.46). B queda en ≈82 GB/s, y los mismos P-cores con el mismo kernel muestran **ρ_P = 15,208/20,141 = 0.755**, frente a 0.912 en 8P.
- Cuando la demanda de cómputo supera la DRAM, la residencia empieza a pagar. El ρ de equipo de P+E (0.742) lo fijan los E-cores por la barrera; la cifra de P-cores es descriptiva mía.

**¿B viene de DRAM o de LLC?** [COMPATIBLE con DRAM casi total]
- Cada episodio empieza con barridos de 8 MiB por worker (64 MiB agregados), y dentro del episodio cada copia de B se toca una sola vez. B no tiene ninguna reutilización intra-episodio, con independencia de que su huella (37.7 MB) supere o no la LLC.
- Los crudos son coherentes con eso:
  - el coste por índice de aplicación de B es plano (32.5–33.0 µs, también en la app 0);
  - c_B(K=2) ≈ c_B(K=16);
  - K=1 da A = B.
- Sin contadores no se puede afirmar procedencia física (MD/29 tenía razón en eso).

**C por índice de aplicación.** El coste mediano de C varía de 31.0 a 33.2 y baja a 30.3 µs al final del episodio.
- Encaja con supervivencia parcial en LLC cuando otros workers ya están ociosos, o con contención variable.
- Confirma la limitación ya registrada: la expulsión solo certifica L2.
- Sobre la media, C ≈ B, así que no sesgó el gate.

### A.4 Modelo que explica ρ ≈ 0.91 y el papel de m [ESTIMACIÓN cuantitativa, consistente con todas las celdas]

`c_A ≈ t_comp` (con este kernel, insensible a L2 o LLC), mientras que `c_B ≈ max(t_comp + sobrecoste_frío, t_DRAM/η_BW)`.

**m=1, 8P:**
- t_comp = 29.7–29.9 µs; t_DRAM = 31.7 µs; η_BW ≈ 0.965; sobrecoste frío ≈ 2.4–2.9 µs.
- Las dos ramas dan ≈32.3–32.8 µs, así que **ρ ≈ 0.91**.
- **Equivalencia clave:** si B queda en su suelo de ancho de banda, **ρ ≈ η_BW/D_read**.
- Con el gate de T0.0 `D_read ≥ 1.00` (medido 1.04, en H0 1.06), el ρ alcanzable estaba acotado en ≈0.91–0.96 *antes de medir H0*.
- Hacía falta `D ≳ 1.45` para ρ ≤ 2/3 y `D ≳ 1.93` para ρ ≤ 0.50.

**m=4:**
- La demanda es 4.34 GB/s por núcleo (D = 0.47). B está tan limitado por cómputo como A, y ρ₄ ≈ 1 es exactamente lo esperable.
- Matrizar y reutilizar en K son **sustitutos** para este kernel. Ambos reducen tráfico por MAC, pero el tráfico ya no era el cuello de botella.
- Conversacion.md (L2797–2800) suponía que a m=1 o 4 «el cómputo todavía no oculta la diferencia de memoria». Ese supuesto **no se cumplió con este kernel**. No es un hecho del hardware.

**Tamaño del shard frente a L1.** 288 KiB ≫ L1D (48 KiB). A m=1 no hay reutilización dentro de una aplicación, así que el tiling en L1 es irrelevante. Solo importaría para m>1 (decodificar una vez por tile y reutilizar entre slots).

**Balance de máquina** [ESTIMACIÓN]
- Techo `vpdpbusd` de 8P ≈ 8·322 ≈ 2.6 TMAC/s.
- Techo DRAM a 0.5625 B/MAC ≈ 132 GMAC/s.
- Cociente **≈19×**. El hardware deja un margen físico enorme para que la residencia importe. El kernel usa ≈5 % de él a m=1 y, casualmente, cae justo en la cresta del roofline (D ≈ 1).

---

## B. Auditoría del protocolo y del kernel: sesgos que pudieron perjudicar a A o inflar B/C

| # | Elemento | Hallazgo | Dirección del sesgo | Magnitud |
|---|---|---|---|---|
| B1 | **Kernel lento (decisivo)** | 92 % de las µops vectoriales son decode, conversiones y escala. Las activaciones se re-extienden por fila y la escala FP16 se trata escalarmente por grupo. Usa `vpdpwssd` (16 MAC) en vez de `vpdpbusd` (32). | Contra la ventaja de A: comprime ρ hacia 1, porque ρ ≈ η/D. | **Determinante** (ver A.4) |
| B2 | Gate de preparación `D_read ≥ 1.00` (MD/25) | Algebraicamente incompatible con ρ ≤ 2/3 o ≤ 0.50 si B está limitado por ancho de banda. Sol lo llamó «mínimo de competencia» y dejó a las AMB la opción de exigir más. Ninguna AMB tradujo el umbral de ρ a un requisito sobre D. | Contra A | Determinante |
| B3 | Variable c amortizada (incluye la 1.ª app fría) | ρ = 0.9126 amortizado frente a 0.906 marginal | Contra A | ≈0.7 pts; no cambia nada |
| B4 | QPC por llamada (10 MHz, 100 ns) | Cuantización de ±0.3 % por llamada, insesgada en promedio. 2 QPC por app con el mismo overhead en A/B/C. | Ligeramente hacia 1 | < 0.2 % |
| B5 | Expulsión que también expulsa activaciones y salida (C) | 576 B + 4 KiB, < 80 líneas | Infla C, no B | < 1 %; B ≈ C |
| B6 | Expulsión incompleta de LLC (C, final de episodio) | C baja a 30.3 µs en la app 15 | Hacia 1 (A/C) | El gate usa max(A/B, A/C) y A/B lo fija |
| B7 | Afinidad y frecuencia | Afinidad fija y correcta en código. Frecuencia no medida. Tasa single-core de T0.0 (17.31) ≈ 8P en H0 (17.51): sin indicio de caída all-core. | Desconocida | Probablemente < 2 % |
| B8 | Páginas de 4 KiB en todas las variantes | B paga ≈33–40 ns por página fría ([COMPATIBLE] streamer o TLB). Con páginas grandes, **B podría abaratarse.** | A favor de A en el protocolo actual | Hasta ≈8 % de c_B. Un protocolo nuevo debe dárselas también a B. |
| B9 | B sin prefetch software ni ajuste propio | B usa el mismo kernel. Con B limitado por ancho de banda, el prefetch no baja de 31.7 µs. | Neutro o leve a favor de A | ≤ 3.5 % (η_BW) |
| B10 | Huella de B frente a la LLC | Irrelevante: barrido de 64 MiB antes de cada episodio y uso único por copia | Neutro | — |
| B11 | P+E con particionado igual | Equipo = max(E-cores, 2.5× más lentos). Mide la barrera, no la residencia. | Descriptivo | — |
| B12 | `P_instr` VNNI limitado por latencia | η_instr inflado ×1.25 | Solo afecta a diagnósticos | — |

**Conclusión de B.**
- No encuentro errores de medición que inviertan el resultado. Los controles (A/A, B ≈ C, integridad) son sólidos y el FAIL es real **para este kernel**.
- Sí encuentro una **deficiencia de diseño identificable y cuantificable**: el kernel y el gate de preparación fijaron D ≈ 1, y con D ≈ 1 el umbral de ρ era inalcanzable antes de medir.
- No es un sesgo de medición. Es que el experimento midió la eficiencia del decode de este kernel, no la física de la residencia.
- B8 va en la dirección opuesta (favorece a A). Un protocolo honesto debe corregirlo también.

**Sobre «cachear la forma desempaquetada» y «fusionar K».**
- Para un solo slot, K aplicaciones de W son estrictamente secuenciales (h_{k+1} depende de todo h_k). **No se puede fusionar ni hacer blocking temporal en K sin romper la recurrencia** [DEMOSTRADO por dependencia].
- La única «fusión» legítima es entre slots, y eso es la matrizacion (m).
- Cachear el decode sí es legítimo en A. INT8 decodificado ocupa 512 KiB más escalas por worker y cabe en L2. Es un «dividendo de residencia» que B no puede usar sin duplicar tráfico DRAM.
- Es otra hipótesis (H-D, abajo) y debe preregistrarse como tal.

---

## C. Técnicas externas aplicables (investigación web, leída el 2026-10-11)

| Idea | Mecanismo | Evidencia (fuente) | ¿Aplica al i7-13700F? | Experimento mínimo prospectivo |
|---|---|---|---|---|
| **C1. `vpdpbusd` sobre nibbles u8 sin pasar por int16** | `bytes_from_nibbles_32`: carga de 16 B, `srli`, inserción de 128 bits, `and`: 32 pesos u8 en 3 µops ALU. Producto u8×s8 de 32 MAC por instrucción. | llama.cpp `ggml_vec_dot_q4_0_q8_0` y `mul_sum_us8_pairs_float` con `_mm256_dpbusd_avx_epi32` bajo `__AVXVNNI__` [5]. uops.info: VPDPBUSD_VEX ymm en ADL-P con lat 5, TP 0.5, p01 [1]. | Sí: AVX-VNNI-256 disponible. Pesos en offset binario (u = q+8) con corrección −8·Σa por grupo, precomputada una vez por aplicación. | Solo benchmark A residente (sin B/C): P_A(m=1) por núcleo. **Predicción [ESTIMACIÓN]:** ≈12–15 µops por grupo, es decir, 2–3× el kernel actual. |
| **C2. Repack intercalado de 8 filas (`block_q4_0x8`)** | 8 filas × 32 pesos contiguos. Escalas de 8 filas convertidas con un vector. Resultados por fila en carriles, sin sumas horizontales ni escala escalar por grupo. | llama.cpp `gemv_q4_b32_8x8_q8_0_lut_avx` (`repack.cpp`, x86) [5]. Mi conteo: ≈58–74 µops por 256 MAC, de 2.6 a 3.4× menos µops por MAC que el kernel actual [ESTIMACIÓN]. | Sí. Es un cambio de layout persistente: los bytes Q4 se conservan (0.5625 B/MAC). | Idem C1. Contabilizar el repack como layout congelado, no como trabajo cronometrado. |
| **C3. Preparar activaciones una vez por aplicación** | Las activaciones [m,512] se convierten y escalan una vez, no 1024 veces | Código propio (A.2): 2 `vpmovsxbw` por grupo y fila a m=1, 8 a m=4 | Sí, trivial | Incluido en C1/C2 |
| **C4. Decode una sola vez y residente (INT8 en L2)** | A usa pesos s8 decodificados (512 KiB/worker + escalas, < 2 MiB) con `vpdpbusd` y operando de memoria. Cero decode en las K aplicaciones. | Ancho de banda de L2 de Golden Cove: «64 byte per cycle path to L2», con 63.5–78 % del teórico en multinúcleo [2]. Raptor Lake: L2 de P a 2 MB, +1 ciclo de latencia, ancho de banda por ciclo igual [3]. | Sí, pero duplica bytes de L2 por MAC: el beneficio depende de L2 (≈40–50 B/ciclo) frente a puertos | Hipótesis separada (H-D). B debe ser el **mejor** kernel Q4 desde DRAM. |
| **C5. Tablas de consulta (T-MAC)** | Sustituye multiplicaciones por tablas indexadas por bits | T-MAC: «up to 4x increase in throughput… compared to llama.cpp» [6]. El README advierte que en x86 la ganancia no está garantizada [6b]. | Dudoso en x86 de 256 bits. Más útil a 1–2 bits que a 4. | Prioridad baja. Solo si C1/C2 no alcanzan el umbral. |
| **C6. E-cores como workers de apoyo con particionado proporcional** | Sube la demanda agregada de cómputo frente a una DRAM fija. Shards de tamaño ∝ velocidad para que la barrera no la dicten los E-cores. | Datos propios de P+E: ρ de P-cores = 0.755 [DEMOSTRADO, descriptivo]. Con particionado igual, la barrera domina: llama.cpp #572 vio 2.4–3× más lento al usar E-cores en el i7-12700H [10]. uops.info: VPDPBUSD en ADL-E con TP 2.0 (16 MAC/ciclo) [1]. | Sí, como fila secundaria. Nunca sustituye la puerta de 8P. | P+E con W_E/W_P preregistrado, por ejemplo igual al cociente de throughput A medido en un benchmark previo solo-A. |
| **C7. Páginas grandes en Windows** | Menos fallos de TLB y menos arranques de prefetch si el mecanismo de página aplica | Microsoft: `SeLockMemoryPrivilege`, `GetLargePageMinimum`, `VirtualAlloc(MEM_LARGE_PAGES)`, no paginables, reservar y comprometer a la vez, asignar al inicio [7]. El streamer no cruza páginas de 4 KiB, incluso con huge pages [8]. Existe un prefetcher de página siguiente (NPP) en núcleos recientes [9]. | Sí. **Beneficia sobre todo a B**: es un control de equidad, no una mejora de A. | 4P, B con 4K frente a 2M (y A como referencia). Si el sobrecoste frío de 8 ps/B baja, queda confirmado el mecanismo por página. |
| **C8. Prefetch software** | Adelanta líneas a L2 | — | A ya está limitado por cómputo (inútil). B tiene un suelo de ancho de banda de 31.7 µs. | Solo como «B en su mejor versión» en el protocolo nuevo |
| **C9. Ancho de banda DRAM real** | Fija el suelo de B | Intel: i7-13700F con DDR5-5600 y «Max Memory Bandwidth 89.6 GB/s» [4]. Con DDR5-5200 2ch el teórico es 83.2 GB/s; el medido (74.46) es el 89.5 %. | Memoria más rápida **reduce** la ventaja de residencia. El resultado depende de esta configuración. | Volver a medir `BW_DRAM_read` en la misma sesión del protocolo nuevo |
| **C10. Frecuencia efectiva** | Convertir MAC/ns a ciclos y separar límite de puertos de throttling | El intento SEP de T0.0 falló | Necesario para verificar A.2 | Herramienta cualificada (APERF/MPERF) antes del protocolo |
| **No aplicable** | AVX-512 y AMX (ausentes); fusionar K en un slot (dependencia); tiling L1 a m=1 (sin reutilización) | — | — | — |

**Marco general.** Es el modelo roofline: P = min(π, β·I) [11]. La residencia solo puede ganar en el régimen en que β·I < π, es decir, si el kernel residente es más rápido que la DRAM.

---

## D. Conclusión

### D.1 Veredicto

1. **El H0_FAIL es válido y no se debe tocar.** La medición es limpia, los controles pasan y el resultado se reproduce desde los crudos. Para el kernel, configuración y protocolo examinados, la residencia no da ventaja útil. No propongo repetir H0 ni reinterpretar sus umbrales.
2. **No es una limitación de la física de este i7.** Es una **deficiencia identificable del kernel y del diseño del gate de preparación** [DEMOSTRADO en el mecanismo]:
   - el kernel funciona al ≈94–100 % de su cota de puertos con solo un 7.7 % de µops útiles;
   - eso lo deja en D ≈ 1.06;
   - con D ≈ 1, ρ ≈ η_BW/D ≈ 0.91 es casi aritméticamente obligatorio.
3. **Prueba empírica de dirección.** Con el mismo kernel, en cuanto la demanda agregada supera la DRAM (P+E), el ρ de los P-cores baja a 0.755. El barrido W (plano L2 = LLC) muestra que el kernel actual «no ve» la jerarquía.
4. **Se justifica un protocolo nuevo, prospectivo y con otro nombre** (no «H0 bis»). Requisito: kernel congelado *antes* de medir B/C, con su eficiencia certificada solo con benchmarks solo-A.

### D.2 Incertidumbre [ESTIMACIONES subjetivas, explícitas]

- **ρ_marg ≤ 0.50 (m=1, 8P):** exige P_A(m=1) ≥ 33.1 MAC/ns por núcleo (1.89× el actual), suponiendo que B se queda en su suelo de 31.7 µs (no puede bajar de ahí).
  - Mi conteo de µops para C1+C2+C3 da 37–56 MAC/ns alcanzables (70–85 % de cota a ≈5 GHz).
  - **P(alcanzar ≥ 33) ≈ 0.6–0.75.**
- **ρ ≤ 0.25:** exige ≥ 66 MAC/ns por núcleo. **P ≈ 0.15–0.25.**
- **ρ₄ ≤ 0.90 (m=4):** exige ≥ 73.5 MAC/ns por núcleo a m=4 (2.38×). **P ≈ 0.3–0.5.**
- **ρ₄ ≤ 0.50:** exige 132 MAC/ns (41 % del pico `vpdpbusd`). **P < 0.1.**
- **Riesgos del modelo:**
  - frecuencia no medida (±6 % entre inferencias);
  - mecanismo del sobrecoste frío no identificado;
  - posible saturación de L2 o LLC con un kernel 3× más rápido (≈30–45 GB/s por núcleo frente a ≈200+ GB/s de L2 en teoría; el agregado de LLC no lo verifiqué).
- **Lectura de fondo.** Aunque la residencia pase con un kernel mejor, a m≥4 seguirá siendo marginal salvo kernels muy cercanos al pico. La ventaja física de OMEGA en este i7 vive en el régimen de m pequeño y alto paralelismo de cómputo.

### D.3 Hipótesis nuevas, priorizadas (todas prospectivas)

1. **H-KQ (decisiva y barata).** «Un kernel Q4 residente (C1+C2+C3) logra P_A(m=1) ≥ 33.1 MAC/ns por P-core con 8P concurrentes.»
   - Solo-A, sin B/C.
   - Si FALLA, la línea queda físicamente cerrada en este i7 con kernels Q4 de 256 bits, de forma mucho más fuerte que hoy.
   - Si PASA, habilita H-R.
2. **H-R (residencia, criterio original sin relajar).**
   - Con el kernel H-KQ congelado: ρ_marg = c_A/c_B ≤ 0.50 (fuerte ≤ 0.25) a m=1, 8P, K=16 (adaptación de hardware ya ratificada).
   - B con páginas grandes y prefetch si eso abarata B (C7/C8, equidad).
   - Contadores o `BW_DRAM` medidos en la misma sesión.
   - S8/S16 ≥ 1.5 como en Conversacion.md.
3. **H-D (decode residente).** A con INT8 decodificado y residente en L2 frente al mejor B Q4 desde DRAM. Mide el «dividendo de residencia» propio de la recurrencia.
4. **H-LLC.** Barrido W de A que **supere la LLC** (≥ 6 MiB por worker), normalizado por byte, con el kernel H-KQ. Debe mostrar la transición LLC→DRAM que H0 nunca exploró. Con un kernel rápido también podría aparecer una rodilla L2→LLC.
5. **H-E.** P+E con shards proporcionales a la velocidad (descriptivo, secundario).
6. **Diagnóstico H-cold.** 4P, B con 4K frente a 2M para identificar el sobrecoste por página (no es un gate).

### D.4 Riesgo de contaminación con resultados previos

**Qué no debe heredarse:**
- el kernel `q4_kernel.cpp` (deficiente para esta pregunta);
- `P_instr` VNNI (limitado por latencia);
- el gate `D_read ≥ 1.00` (incompatible con el umbral);
- los umbrales relajados de MD/29 (ρ ≤ 2/3, ρ₄ ≤ 0.90);
- el criterio H0-K sin normalizar;
- cualquier selección (m_gate=4, W, K) hecha a la vista de datos de H0.

**Qué puede reutilizarse con verificación:**
- el harness A/B/C (estructura causal correcta);
- la política N=31, aleatorización y QA de copias B;
- la topología;
- la cifra de DRAM, solo como orientación y siempre volviendo a medirla.

**Fuentes de sesgo nuevas que hay que bloquear:**
- **(a) Iterar el kernel mirando ρ.** Prohibido: el ajuste se hace solo con throughput solo-A, se congela por hash y A/B/C se ejecuta una vez.
- **(b) Elegir umbrales con lo aprendido aquí.** Hay que usar los originales de Conversacion.md.
- **(c) Dar a B un trato peor que a A.** B debe recibir sus propias optimizaciones (páginas grandes, prefetch).
- **(d) Presentar el nuevo experimento como rescate de H0.** Debe registrarse como hipótesis nueva, informada por este análisis, y así lo declara este documento.

**Riesgo residual.** Que el diseño del protocolo nuevo esté informado por este FAIL es inevitable. Es legítimo si se declara y se preregistra antes de cualquier dato A/B/C.

---

## Fuentes (consultadas el 2026-10-11)

1. uops.info, base de datos `https://uops.info/instructions.xml` (Last-Modified: 29-mar-2026). Filas ADL-P/ADL-E de VPDPBUSD_VEX, VPDPWSSD_VEX, VPMOVZXBW, VPMOVSXBW, VPSRAW, VPSLLW, VPUNPCKLBW, VPAND, VBROADCASTSS, VCVTPH2PS, VCVTDQ2PS, VMULPS, VADDPS, VMOVD, VINSERTI128, VPSHUFB, VFMADD231PS.
2. Chips and Cheese, «Popping the Hood on Golden Cove», 2-dic-2021. https://chipsandcheese.com/p/popping-the-hood-on-golden-cove
3. Chips and Cheese, «A Preview of Raptor Lake's Improved L2 Caches», 23-ago-2022. https://chipsandcheese.com/p/a-preview-of-raptor-lakes-improved-l2-caches
4. Intel, especificaciones del i7-13700F. https://www.intel.com/content/www/us/en/products/sku/230491/intel-core-i713700f-processor-30m-cache-up-to-5-20-ghz/specifications.html
5. llama.cpp (master, leído el 2026-10-11):
   - https://raw.githubusercontent.com/ggml-org/llama.cpp/master/ggml/src/ggml-cpu/arch/x86/quants.c (`bytes_from_nibbles_32`, `mul_sum_us8_pairs_float`, `ggml_vec_dot_q4_0_q8_0`);
   - https://raw.githubusercontent.com/ggml-org/llama.cpp/master/ggml/src/ggml-cpu/arch/x86/repack.cpp (`gemv_q4_b32_8x8_q8_0_lut_avx`).
6. Wei et al., «T-MAC», arXiv:2407.00088 (v1 25-jun-2024, v2 25-mar-2025). https://arxiv.org/abs/2407.00088
   - 6b. README: https://github.com/microsoft/T-MAC
7. Microsoft Learn, «Large-Page Support». https://learn.microsoft.com/en-us/windows/win32/memory/large-page-support
8. A. Bertschi, «Battling the Prefetcher: Exploring Coffee Lake (Part 1)», 5-sep-2022 (i7-8700K). https://abertschi.ch/blog/2022/prefetching/
9. «Intel Redwood Cove 微架构评测», 23-abr-2025 (descripción de prefetchers, incluido el next-page). https://jia.je/hardware/2025/04/23/intel-redwood-cove/index.md
10. llama.cpp Discussion #572, «Performance 3x better when use performance core only on Intel gen 12th cpu», 28-mar-2023. https://github.com/ggerganov/llama.cpp/discussions/572
11. Wikipedia, «Roofline model» (referencia a Williams, Waterman y Patterson, CACM 52(4), 2009). https://en.wikipedia.org/wiki/Roofline_model

**Datos locales usados:**
- `T0\results\T0.1\h0_raw\h0_official_20261011b\*` (CSV crudos y summaries);
- `h0_derived_…`, `h0_corrections_…`, `h0_eviction_cost_…`;
- `T0\results\T0.0\phase1_*.csv`;
- `T0\src\build\h0_q4_kernel.obj` (desensamblado) y `phase1_q4_kernel.asm`;
- `T0\src\phase1_instr_x64.asm` (codificación `c4 e2 7d 52` = VPDPWSSD).
