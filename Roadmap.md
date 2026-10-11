# OMEGA — Roadmap (plataforma i7)

Fecha: 2026-10-09. Idioma: español.
Fuente de verdad única: `Conversacion.md` (esta carpeta). Versión leída: **3328 líneas** (no 2291; ver Obs-1).
Referencia consultada, no normativa: respaldo `C:\Projects\OMEGA\BACKUP_T0_RESTART\PRE_SPEC_PURGE_2026-10-10\Roadmap.md`.

---

## TABLERO DE FASES (lo actualiza el juez en este archivo)

Ciclo de estados y condiciones: sección 4.3. Una fase solo puede pasar a EN_CURSO si la fase anterior de esta tabla está en `INSPECCIONADA_POR_OPUS`.

| Orden | Fase | Objetivo (resumen) | Depende de | AMB que deben estar resueltas antes de EN_CURSO | ESTADO | Fecha / evidencia |
|---:|---|---|---|---|---|---|
| 1 | T0.0 | Preparación de medición en el i7 | — | AMB-05 (CERRADA, MD/22) | INSPECCIONADA_POR_OPUS (Completed 2026-10-11) | AMB-05 resuelta por Sol 2026-10-09 (MD/22.md). Reinspección Opus: COMPLETED (R1 SHA de T0.md corregido, 953/953). Kernel Q4 optimizado + remedición oficial (MD/24–28): 79 series completas, 0 inválidas; D_read AVX2 1.0078 / VNNI 1.0403 (gate PASS); frecuencia efectiva UNAVAILABLE_WITH_QUALIFIED_TOOLS. Inspección Opus 2026-10-11: DESVIACIONES solo documentales (MD/OPUS_T0_0_INSPECCION.md), mediciones y cifras conformes → vuelve a EN_CURSO; correcciones documentales aplicadas por opencode y verificadas por el juez 2026-10-11 (SHA256SUMS 953/953, sin TBD) → VERIFICADA_POR_JUEZ → reinspección Opus: COMPLETED (sección «Reinspección» de MD/OPUS_T0_0_INSPECCION.md, 2026-10-10 20:28 locales; verificación 953/953 posterior a R1 declarada allí; el registro intermedio del reloj no se conserva en otro archivo: laguna declarada, sin cronología inventada) |
| 2 | T0.1 | H0: microkernel Q4 lineal (A/B/C y FLOPs constantes según AMB-22) | T0.0 | AMB-02, 03, 04, 05, 10, 12, 13, 14, 15, 18, 19, 21, 22 (TODAS CERRADAS: MD/22, MD/29) | INSPECCIONADA_POR_OPUS (Completed operativo 2026-10-11) con estado terminal FALLIDA_LINEA_MUERTA_PHYSICAL_PREMISE (veredicto de Sol, MD/34; limitada al kernel/configuración/protocolo examinados; el Completed no habilita H1) | T0.0 Completed 2026-10-11; 13 AMB resueltas por Sol (MD/29.md); sesión oficial H0 `h0_official_20261011b` completa (47 bloques, medición válida: R_noise 0.0128, expulsión 8 MiB PASS, delta_BC≈0.0006/0.009); gates: H0-R1 FAIL (rho_1=0.913), H0-R4 FAIL (rho_4=0.994), H0-M PASS (S4=1.762), H0-K: PASS literal histórico pero `NOT_ESTABLISHED` científicamente (C_L2_knee/B_L2_operational=null, MD/34 D-B); veredicto **H0_FAIL firme** (Sol, MD/34); correcciones en `T0
esults\T0.1\h0_corrections_h0_official_20261011b.json`; ver `T0\results\T0.1\h0_derived_h0_official_20261011b.json` |
| 3 | T0.2 | H1: bloque recurrente realista (A/B/C y FLOPs constantes según AMB-22) | T0.1 | AMB-02, 03, 04, 12, 22 | NOT_STARTED | NOT_STARTED por H0_FAIL firme (MD/34, 2026-10-11); no habilitada |
| 4 | T0.3 | Veredicto del gate físico por propiedades | T0.2 | AMB-02, 04, 10, 19 | NOT_STARTED | no habilitada: requiere H1 (H0_FAIL firme, MD/34) |
| 5 | T1.1 | Aprendizaje sintético pequeño | T0.3 | AMB-01, 06, 11, 16 | PENDIENTE | — |
| 6 | T2.1 | Trazas del maestro offline | T1.1 | AMB-07, 08, 09, 11, 20 | PENDIENTE | — |
| 7 | T2.2 | Destilación con estado objetivo anterior | T2.1 | — | PENDIENTE | — |
| 8 | T2.3 | Scheduled sampling | T2.2 | — | PENDIENTE | — |
| 9 | T2.4 | Unrolls cortos 8/16/32 con truncated BPTT | T2.3 | — | PENDIENTE | — |
| 10 | T3.1 | Banco congelado y retriever (construcción offline) | T2.4 | AMB-08, 16 | PENDIENTE | — |
| 11 | T3.2 | Entrenar consulta/top-k/escritura/detección con núcleo congelado | T3.1 | — | PENDIENTE | — |
| 12 | T3.3 | Descongelar parcialmente el núcleo y afinar conjuntamente | T3.2 | — | PENDIENTE | — |
| 13 | T4.1 | BF16/FP16 estable | T3.3 | — | PENDIENTE | — |
| 14 | T4.2 | Q8 | T4.1 | — | PENDIENTE | — |
| 15 | T4.3 | Q4 con fine-tuning consciente de cuantización | T4.2 | — | PENDIENTE | — |
| 16 | T4.4 | Validación nativa Q4 en el i7 | T4.3 | — | PENDIENTE | — |

---

## 0. Propósito y reglas de lectura

- Este documento traslada el programa de `Conversacion.md` a la plataforma real. Mismas etapas, mismo orden, mismas dependencias, mismos criterios y valores. **Único cambio permitido: la lógica de hardware Ryzen AI 5 330 → i7-13700F.**
- `Conv.La-Lb` = líneas de `Conversacion.md`. «Texto entre comillas angulares» = cita literal.
- **[i7]** marca todo cambio por hardware, con qué cambió y por qué. Lo que no lleva [i7] se conserva tal cual.
- Todo valor etiquetado «esperado» (ficha pública del i7-13700F: núcleos, L2, LLC, SMT, ISA, DDR) es TBD-VERIFY aunque la marca no se repita en cada uso; ninguno es dato hasta verificarse en T0.0.
- `TBD-VERIFY` = hecho de la máquina a comprobar. `TBD-MEASURE` = valor a medir. Ningún valor de hardware se inventa.
- `AMB-nn` = ambigüedad abierta (sección 7), no resuelta aquí.
- Los únicos criterios científicos de paso/fallo son los escritos en `Conversacion.md` (sección 6). El ciclo de estados de las fases (sección 4.3) es el flujo operativo pedido por el usuario; no añade umbrales científicos.
- Principio de método conservado: el LLM es «investigador asistente, no fuente de verdad. Cada propuesta debe terminar en código, medición y comparación» (Conv.L167).

---

## 1. Contrato permanente

| # | Cláusula | Relación con `Conversacion.md` |
|---|---|---|
| C1 | `d = 512` | «Núcleo inicial: d=512» (Conv.L3287-3288); «d=512 como candidato principal» (Conv.L3140) |
| C2 | Núcleo recurrente `16d² = 4,194,304` pesos únicos | `P_bloque = 4d² + 12d² = 16d²` (Conv.L2574-2589); «≈4.19M pesos únicos» (Conv.L3289) |
| C3 | Cero evidencia científica de modelos sustitutos/reducidos; tamaños pequeños solo `ENGINEERING_SMOKE_ONLY` | Cláusula del usuario. Choca con T1 (`d=64–128`, Conv.L3151-3155) y con las filas `d≠512` de H1 (Conv.L2693) → AMB-01, AMB-02 |
| C4 | «X parámetros» = parámetros entrenables únicos del núcleo; el banco se contabiliza aparte | Conv.L2497, L2942-2944 |
| C5 | Q4 + una escala FP16 por cada 32 pesos: `q_efectivo ≈ 0.5625` B/peso → núcleo ≈ **2.25 MiB**; el ejecutable reporta bytes físicos reales | Conv.L2591-2598, L2611, L3290 |
| C6 | Kernel nativo: **no existe**; se reconstruye desde cero cuando lo requiera T0 (H0/H1, A/B/C) | Instrucción del usuario. `Conversacion.md` exige un «ejecutable» que reporte «bytes físicos asignados» (Conv.L2611) y un «kernel real» (Conv.L974, L995) |
| C7 | GPU GTX 1650 SUPER 4 GiB: solo validaciones, **nunca** evidencia de la tesis CPU | Cláusula del usuario. Coherente con «Entrenamiento: ninguno para el gate» (Conv.L3305-3306) |

Afirmación que se intenta probar (literal, Conv.L3317-3319): «Un núcleo procedural de aproximadamente 4M parámetros puede reutilizarse desde caché con suficiente eficiencia para dejar la mayor parte del presupuesto de DRAM disponible para memoria externa; después habrá que demostrar por separado que esa memoria puede sustituir una fracción significativa del conocimiento paramétrico.» Afirmación que NO se intenta: «Un modelo de 4M equivale a uno de cientos de millones» (Conv.L3315).

Tesis (Conv.L2469): workspace latente matricial, núcleo pequeño de pesos compartidos shardeado entre las L2 privadas; `K` aporta composición secuencial, `m` espacio de trabajo e intensidad aritmética, la RAM capacidad de conocimiento mediante lecturas externas e infrecuentes.

---

## 2. Plataforma i7

### 2.1 Hechos dados (usuario)

| Ítem | Valor |
|---|---|
| CPU | Intel Core i7-13700F (Raptor Lake), híbrida P+E |
| Hilos lógicos | 24 (según el sistema) |
| GPU | NVIDIA GTX 1650 SUPER, 4 GiB — solo validación (C7) |
| SO | Windows 11 |
| Disco | C: |

### 2.2 Pendientes (no derivables de `Conversacion.md` ni de lo dado)

Los valores «esperados» provienen de la ficha pública del procesador; se usan solo para mostrar proyecciones condicionales y **no** valen como dato hasta verificarse en T0.0.

| Símbolo | Qué | Esperado (ficha, no verificado) | Estado |
|---|---|---|---|
| `N_P`, `N_E` | Nº de P-cores y E-cores físicos | 8 P (con SMT) + 8 E (sin SMT) = 24 hilos | TBD-VERIFY |
| `L2_P` | L2 privada por P-core | 2 MiB | TBD-VERIFY |
| `L2_E` | L2 de E-cores | 4 MiB compartida por cluster de 4 E-cores (no privada) | TBD-VERIFY |
| `LLC` | L3 compartida | 30 MB | TBD-VERIFY; LLC útil: TBD-MEASURE |
| RAM | Capacidad, tipo, velocidad, canales poblados | DDR4 o DDR5 (la ficha admite ambas); tipo, módulos y canales TBD | TBD-VERIFY |
| ISA | AVX2 / AVX-VNNI / AVX-512 | sin AVX-512 | TBD-VERIFY (CPUID) |
| Relojes | Frecuencia sostenida P y E bajo carga | — | TBD-MEASURE |
| Potencia | PL1/PL2, comportamiento térmico | — | TBD-MEASURE |
| Entorno | Bare metal (no VM) | — | TBD-VERIFY |
| `BW_D(n)`, `BW_L2,i`, `BW_L2,Σ`, `r`, `v_i`, pico MAC | Mediciones de T0.0 | — | TBD-MEASURE |
| Contadores | Herramienta Intel equivalente a uProf en Windows | — | TBD-VERIFY |

`n_w` = nº de workers primarios (uno por núcleo físico). Su valor en el i7 está abierto: AMB-03.

---

## 3. Proyección Ryzen → i7

| # | Ryzen en `Conversacion.md` | i7 [proyección] | Razón |
|---|---|---|---|
| P1 | «1 Zen 5 y 3 Zen 5c», 4 núcleos/8 hilos, 4 MB L2, 8 MB L3 (Conv.L2500) | `N_P` P + `N_E` E, 24 hilos; `L2_P`, `L2_E`, `LLC` TBD-VERIFY | Cambio de máquina |
| P2 | Plataforma de gate «Ryzen AI 5 330 bare metal, Windows 11» (Conv.L3281-3282) | i7-13700F bare metal (TBD-VERIFY), Windows 11 nativo; WSL2 solo como contraste (Conv.L2896) | Cambio de máquina; regla de Windows nativo sin cambio |
| P3 | «Workers primarios: 4, uno por núcleo físico» (Conv.L3284-3285); «4 threads físicos» (Conv.L3141) | `n_w` workers, uno por núcleo físico, afinidad fija; qué núcleos: AMB-03 | En el Ryzen «4» = todos los núcleos físicos; en el i7 no hay equivalente único (P/E, L2 de E compartida) |
| P4 | «4T vs 8T» (Conv.L2887-2889, L3303) | `n_w` hilos físicos vs fila SMT (2 hilos en P-cores); solo P-cores tienen SMT (esperado, TBD-VERIFY) | Topología híbrida |
| P5 | Sharding «1 Zen 5 + 3 Zen 5c» por `v_i` (Conv.L2622-2645) | Misma regla sobre P/E: `P_i = P_total·v_i/Σv_j`, `B_i ≤ B_L2útil,i`; heterogeneidad P/E en lugar de Zen 5/5c | Se conserva la lógica; cambia el tipo de heterogeneidad |
| P6 | Presupuesto L2: «0.50–0.70 MiB de pesos por núcleo» = 50–70 % de 1 MiB; total 2.0–2.8 MiB (Conv.L2560-2572) | `0.50–0.70 × L2_P` por P-core (esperado 1.0–1.4 MiB); total `n_w ×` eso; E-cores: TBD-MEASURE (L2 compartida). Se barre hasta la rodilla real (Conv.L2572) | Proporcional al L2 por núcleo; sujeto a AMB-18 |
| P7 | Núcleo «2–2.5 MiB» dictado por el chip (Conv.L2498) | El tamaño lo fija el contrato (2.25 MiB); en el i7 el presupuesto P6 indica la holgura, no el tamaño | El contrato manda (C1-C2) |
| P8 | `d=512`: «≈0.56 MiB promedio por núcleo» (Conv.L3291, L2616) | `2.25/n_w` MiB por worker con reparto uniforme (esperado: 0.5625 si `n_w=4`; 0.281 si `n_w=8`); real por `v_i` | Depende de AMB-03 |
| P9 | H0: bytes por núcleo `{256,384,512,640,768,896,1024,1280}` KiB = `{0.25,…,1.25} × 1 MiB` (Conv.L2682-2683) | `{0.25,0.375,0.5,0.625,0.75,0.875,1.0,1.25} × L2_P` (esperado `{512,768,1024,1280,1536,1792,2048,2560}` KiB); E-cores TBD | Mismas fracciones del L2 por núcleo; ligado a AMB-02 y AMB-18 |
| P10 | B con `K=8` ≈18 MiB «supera ampliamente los 8 MB de L3 y obliga a que el camino no compartido toque DRAM» (Conv.L2772-2780) | B ocupa `2.25·K` MiB; toca DRAM solo si `2.25·K > LLC útil`. Con LLC esperado 30 MB, `K=8` (18 MiB) cabría nominalmente en LLC (LLC útil TBD-MEASURE); `K=16` (36 MiB) no | La premisa del criterio cambia → AMB-04 |
| P11 | Buffer de expulsión de C: «32–64 MiB» (Conv.L2786) frente a 4 MB de L2 + 8 MB de L3 (Conv.L2500) | «Buffer particionado» pensado para destruir la residencia de L2 y LLC del i7 (Conv.L2786); tamaño TBD-MEASURE. Regla de tamaño y forma de comprobar su eficacia: AMB-21 | La capacidad de caché a destruir es otra |
| P12 | Rodillas «d=384/512 → L2, 640 → frontera, 768 → L3, 1024 → DRAM» (Conv.L2869-2874) | Los bytes por `d` no cambian (`9d²/2²⁰` MiB: 1.27 / 2.25 / 3.52 / 5.06 / 9.00); su régimen en el i7 depende de `n_w`, `L2_P`, `LLC` (ver 5.2.3). Con `n_w=8` P-cores esperados, `d≤768` cabe con holgura en L2 agregada útil y `d=1024` (1.125 MiB/worker = 0.5625×`L2_P` esperado) cabe en el presupuesto 0.50–0.70×`L2_P` salvo en su tramo inferior (<0.5625) → las rodillas podrían no caer en esos `d` | Cambio de capacidades → AMB-02 |
| P13 | `r_4` = (BW_L2 Zen 5 + 3·BW_L2 Zen 5c)/BW_DRAM con 4 núcleos; «10–20× plausible» no se asume (Conv.L2506-2544) | `r_n = BW_L2,Σ(workers)/BW_D(n)`, con `BW_D(n)=min(BW_canales, n·BW_por núcleo)`; ningún rango se asume | Fórmula conservada; valores TBD-MEASURE |
| P14 | Configuración de memoria ASUS (16 GB soldados, dual-channel con SO-DIMM) (Conv.L2546) | Registrar módulos, canales poblados y velocidad efectiva junto a cada resultado | Se conserva la regla; cambia el hecho |
| P15 | AMD uProf / `AMDuProfPcm -m memory` (Conv.L2898-2918) | Herramienta Intel con contadores de DRAM/L2/LLC en Windows: TBD-VERIFY. Si no hay eventos finos, vía por tiempo de pared (Conv.L2920-2926) | Cambio de fabricante |
| P16 | «comparar rutas AVX2 y AVX-512» (Conv.L2928) | Detección CPUID y comparación de las rutas que existan (AVX-512 esperado ausente) | ISA distinta → AMB-05 |
| P17 | «bajo 15–28 W», «límite térmico y energético de una laptop» (Conv.L3264, L2542) | Límites de potencia/térmicos del desktop: registrar (TBD-MEASURE) | Clase de equipo distinta |
| P18 | «La laptop será plataforma de validación de inferencia, no de entrenamiento principal» y lista «La laptop hará» (Conv.L2496, L3092-3099) | El i7 asume ese papel: H0, H1, profiling, ejecución Q4, pruebas de caché, validación de checkpoints, tareas sintéticas diminutas | Sustitución directa de plataforma |
| P19 | «No se entrenará desde cero un modelo lingüístico significativo en el Ryzen AI 5 330» (Conv.L3090) | Se proyecta al i7 como plataforma de validación; alcance exacto: AMB-06 | La razón original (laptop) no se repite literalmente |
| P20 | Entrenamiento lingüístico «en GPU» (Conv.L3101); «antes de gastar GPU» (Conv.L3145) | La GPU local es solo validación (C7); la GPU de entrenamiento no está definida: AMB-07 | Contrato del usuario |
| P21 | «→ validación nativa en laptop» (Conv.L3249) | Validación nativa Q4 en el i7 (CPU), kernel reconstruido (C6) | Sustitución de plataforma |
| P22 | «escalado lineal a 16 o 32 núcleos» no demostrable (Conv.L3268) | [i7] el i7 tiene `N_P`+`N_E` núcleos físicos (esperado 16; TBD-VERIFY); lo medido vale para este i7 y no demuestra escalado a otras máquinas | Sin cambio de lógica |

---

## 4. Orden, dependencias, fases y estados

### 4.1 Etapas

```
T0  Gate físico (sin entrenamiento)            Conv.L3133-3145
 ├─ T0.0 Preparación de medición               Conv.L2500-2546, L2622-2662, L2894-2928
 ├─ H0   Microkernel Q4 lineal (A/B/C: AMB-22)   Conv.L2668-2686
 └─ H1   Bloque recurrente realista (A/B/C: AMB-22)  Conv.L2688-2718
         Criterios por propiedad                Conv.L2804-2892
   «Si H0/H1 no pasan, la línea muere antes de gastar GPU.» (Conv.L3145)
T1  Aprendizaje sintético pequeño              Conv.L3147-3166
T2  Destilación del núcleo sin banco           Conv.L3168-3217
T3  Memoria externa                            Conv.L3219-3239
T4  Cuantización                               Conv.L3241-3252
```

- Dependencias escritas: T0 precede a todo gasto de GPU (Conv.L3145). T3 parte de un núcleo «inicialmente congelado» (Conv.L3227), es decir del núcleo de T2. T4 parte de «BF16/FP16 estable» (Conv.L3246). El resto del orden es el de la numeración T0→T4.
- Dentro de T0: H0 y H1 se presentan en ese orden («Conviene separar dos gates», Conv.L2666). T0.0 no es una etapa nueva: agrupa las mediciones previas que la conversación exige («una de las primeras mediciones del gate», Conv.L2544; «Primero se mide cada núcleo físico», Conv.L2624).
- Sin etapa asignada en la conversación: ledger de contabilidad y comparación factorial D0/D1/R0/R1 (Conv.L2930-3084) → AMB-08. No son fase propia; quedan como requisito pendiente de asignación.

### 4.2 Fases

La división en fases sigue la secuencia que la propia conversación escribe dentro de cada etapa; no añade etapas. Orden estrictamente secuencial (columna «Depende de»).

| Fase | Objetivo | Depende de | Salida que habilita IMPLEMENTADA | Conv. |
|---|---|---|---|---|
| T0.0 | Preparación de medición (ítems T0.0-a…i de 5.1); incluye el microkernel mínimo necesario para `v_i` y pico MAC | — | Tabla 2.2 sin TBD-VERIFY; `v_i` por núcleo físico, pico MAC por cada ruta ISA disponible, `BW_D(n)`, `BW_L2,Σ` y `r` por cada conjunto candidato de workers, herramienta de contadores o vía por tiempo de pared, condiciones de reloj/potencia registradas | L2500-2546, L2622-2662, L2841-2849, L2894-2928 |
| T0.1 | H0: microkernel Q4 lineal, variantes A/B/C y barrido a FLOPs constantes (5.2.2) según la resolución de AMB-22, barrido de 5.2.1 | T0.0 | Ejecutable que reporta bytes físicos asignados; tabla de resultados del barrido completo (incluido el barrido a FLOPs constantes si AMB-22 lo asigna a H0) con las métricas de 5.2.5 | L2611, L2668-2686, L2720-2749, L2751-2802 |
| T0.2 | H1: bloque recurrente realista, A/B/C según la resolución de AMB-22, barridos de 5.2.2 | T0.1 | Tabla de resultados del barrido completo (barrido a FLOPs constantes según AMB-22) con métricas de 5.2.5 | L2688-2718, L2720-2749, L2751-2802 |
| T0.3 | Veredicto del gate por propiedades (residencia, matrixización, rodillas, SMT) y respuesta a las 7 preguntas de 5.2.6 | T0.2 | Informe con ρ, η(m), cociente `MAC/s(m=8 u 16)/MAC/s(m=1)`, rodillas, SMT, comparados con los umbrales escritos de 6.1 | L2120-2137, L2804-2892, L3145, L3256-3274 |
| T1.1 | Entrenamiento diminuto desde cero sobre las 6 tareas sintéticas | T0.3 | Resultados por tarea (entrenabilidad) | L3147-3166 |
| T2.1 | Generar trazas del maestro offline | T1.1 | Trazas del maestro congelado | L3170, L3212 |
| T2.2 | Entrenar el estado siguiente desde el estado objetivo anterior, con la pérdida de 5.4; currículo y corridas separadas por `m` según la resolución de AMB-20 (Conv.L3198-3208) | T2.1 | Checkpoints y curvas por ejecución | L3179-3208, L3213 |
| T2.3 | Scheduled sampling | T2.2 | Checkpoints y curvas | L3214 |
| T2.4 | Unrolls cortos de 8, 16 y 32 tokens con truncated BPTT | T2.3 | Checkpoints y curvas | L3215 |
| T3.1 | Banco congelado (keys/values offline desde corpus o estados del maestro, solo datos permitidos) y retriever pequeño | T2.4 | Banco con cobertura y tamaño reportados; ledger según resolución de AMB-08 | L3221-3227, L3239 |
| T3.2 | Con núcleo congelado: formulación de consultas, top-k, escritura de evidencia en slots, detección de recuperación insuficiente | T3.1 | Checkpoints y resultados | L3230-3235 |
| T3.3 | Descongelar parcialmente el núcleo y afinar de forma conjunta | T3.2 | Checkpoints y resultados | L3237 |
| T4.1 | Modelo estable en BF16/FP16 | T3.3 | Checkpoint de referencia | L3246 |
| T4.2 | Q8 | T4.1 | Checkpoint Q8 y comparación con T4.1 | L3247 |
| T4.3 | Q4 con fine-tuning consciente de cuantización | T4.2 | Checkpoint Q4 y comparación con T4.2 | L3248, L3252 |
| T4.4 | Validación nativa Q4 en el i7 (CPU, kernel reconstruido) | T4.3 | Resultados nativos frente a T4.3 | L3249 |

### 4.3 Ciclo de estados

Roles: **opencode** implementa; **juez** verifica; **Sol** decide ciencia cuando `Conversacion.md` lo requiere y resuelve ambigüedades; **Opus** inspecciona.

| Transición | Condición exacta | Quién la declara |
|---|---|---|
| PENDIENTE → EN_CURSO | (1) La fase anterior del tablero está en `INSPECCIONADA_POR_OPUS`. (2) Las AMB listadas para la fase en el tablero están resueltas por Sol y registradas en este archivo. (3) Los TBD que la fase necesita como entrada están resueltos por fases anteriores. | Juez |
| EN_CURSO → IMPLEMENTADA | Existe la «Salida» de la fase (tabla 4.2), producida con los parámetros exactos de la sección 5 y el contrato C1-C7 (incluido `ENGINEERING_SMOKE_ONLY` para los tamaños que la resolución de AMB-01/AMB-02 clasifique como sustitutos o reducidos). | opencode |
| IMPLEMENTADA → VERIFICADA_POR_JUEZ | El juez comprueba sobre los archivos en disco (no sobre el informe) que la salida existe, que los parámetros coinciden con la sección 5 y que no hay desviaciones del contrato ni de `Conversacion.md`. En T0.3 (y en T0.1 si AMB-19 resuelve que H0 es un gate propio), además, Sol emite el veredicto científico aplicando los criterios escritos de 6.1; en T1.1-T4.4, Sol evalúa el resultado (la conversación no fija umbrales, 6.3). | Juez (Sol para el veredicto científico, cuando aplica) |
| VERIFICADA_POR_JUEZ → INSPECCIONADA_POR_OPUS (**Completed**) | Opus inspecciona la fase cerrada contra este Roadmap y `Conversacion.md` y no encuentra desviaciones. Si encuentra alguna, la fase vuelve a EN_CURSO. | Opus |

Reglas:
- `INSPECCIONADA_POR_OPUS` = Completed: cierra la fase y es el **único** habilitador para iniciar la siguiente. Ninguna fase inicia sin la anterior Completed.
- Si el juez detecta desviación en la verificación, la fase vuelve a EN_CURSO.

Estados terminales / de excepción:

| Estado | Cuándo | Fuente | Quién |
|---|---|---|---|
| BLOQUEADA | La fase no puede iniciar o continuar: AMB no resuelta, TBD de entrada sin valor, dependencia no Completed, o falta de recurso (p. ej. GPU de entrenamiento, AMB-07). Sale a PENDIENTE/EN_CURSO al levantarse la causa. | Secciones 0 y 7 de este documento (y la condición 3 de PENDIENTE → EN_CURSO) | Juez |
| FALLIDA_LÍNEA_MUERTA | En T0.3 (o en T0.1 según la resolución de AMB-19): H0/H1 no pasan según los criterios de 6.1 (p. ej. «Si A y C resultan prácticamente iguales, la hipótesis L2-residente ha fallado»). Cierra la línea: no se inicia T1 ni se gasta GPU. | «Si H0/H1 no pasan, la línea muere antes de gastar GPU.» (Conv.L3145); Conv.L2836 | Sol (veredicto), juez lo registra |
| FALLIDA | T1.1-T4.4: Sol declara que el resultado no cumple el objetivo de la fase. `Conversacion.md` no escribe umbrales para estas etapas (6.3), por lo que la declaración debe citar el razonamiento de Sol; este documento no fija umbral. Qué ocurre después lo decide Sol. | Conv.L3166, L3252; AMB-11 | Sol, juez lo registra |

---

## 5. Detalle por etapa

### 5.1 Fase T0.0 — Preparación de medición

Pequeña y concreta. Cada ítem sale de la conversación; [i7] indica lo que cambia.

| ID | Ítem | Salida | Fuente |
|---|---|---|---|
| T0.0-a | Inventario de máquina: modelo de CPU, núcleos/hilos, L2 por núcleo, L3, RAM (módulos, canales, velocidad), bare metal | Tabla 2.2 completada (TBD-VERIFY → dato). [i7] sustituye la descripción del Ryzen y el caveat ASUS | Conv.L2500, L2546, L1295-1301 |
| T0.0-b | Topología: tipo de cada núcleo físico (P/E), parejas SMT, CPU lógica ↔ núcleo físico, qué núcleos comparten L2 | Mapa de topología; «No debe asumirse qué número de CPU lógica corresponde» al núcleo rápido | Conv.L2645, L1255-1263 |
| T0.0-c | CPUID / ISA | Rutas disponibles; comparación medida «en lugar de asumir que la ruta más ancha será siempre la más rápida». [i7] AVX-512 esperado ausente (AMB-05) | Conv.L2928 |
| T0.0-d | `v_i`: MAC/s sostenidos de cada núcleo físico con el mismo microkernel | Base del sharding `P_i = P_total·v_i/Σv_j` | Conv.L2624-2645 |
| T0.0-e | Pico MAC/s medido (denominador de η): «kernel pequeño residente en L1/L2 y la misma ruta de instrucciones», no el TOPS publicitario | Pico por ruta ISA | Conv.L2841-2849 |
| T0.0-f | Ancho de banda: `BW_D(n)` para varios `n`; `BW_L2,i` por núcleo y `BW_L2,Σ` con kernel real; cociente `r` | `r` medido; no se asume el rango 10–20× | Conv.L2504-2544, L1970-1982, L2023-2029 |
| T0.0-g | Herramienta de contadores (tráfico DRAM agregado, L2, LLC) y su alternativa | [i7] herramienta Intel equivalente a uProf: TBD-VERIFY. Si faltan eventos finos: tiempo de pared, tráfico DRAM agregado, tamaño de working set, expulsión controlada, curva de latencia contra bytes residentes | Conv.L2898-2926 |
| T0.0-h | Método de tiempo | `QueryPerformanceCounter` + afinidad fija + mediana de múltiples ejecuciones; `RDTSCP` por worker cuando sea útil; nativo Windows | Conv.L2896, L2900-2909 |
| T0.0-i | Condiciones de reloj y potencia | Frecuencias sostenidas P/E; [i7] límites de potencia/térmicos del desktop en lugar de 15–28 W | Conv.L2542, L3264 |

### 5.2 T0 — Gate físico (Conv.L3133-3145)

Configuración base escrita: «Q4 sintético / bloque A/B/C / d=512 como candidato principal / 4 threads físicos / m/K barridos» (Conv.L3137-3143). [i7] «4 threads físicos» → `n_w` hilos físicos (P3, AMB-03).

Reglas de ejecución conservadas (Conv.L1255-1263, L2647-2662):
- Un hilo fijado por núcleo físico, mismo shard siempre, sin migración; no confundir SMT con núcleos físicos.
- Pesos, escalas, metadatos y buffers deben caber simultáneamente; nada externo barre la L2 entre iteraciones.
- Las barreras no deben destruir el beneficio (Conv.L1263).
- SMT: «no añade L2; no añade unidades vectoriales; comparte el frontend y backend»; es «una fila experimental, no la configuración base».

#### 5.2.1 Fase T0.1 — H0: microkernel Q4 lineal (Conv.L2668-2686)

Comprueba exclusivamente: residencia; transición GEMV → small-GEMM; amortización del desempaquetado; comportamiento L2/L3/DRAM.

| Parámetro | Conversación | i7 |
|---|---|---|
| `m` | `{1, 2, 4, 8, 16, 32}` (`m=32` de estrés) | igual |
| `K` | `{1, 2, 4, 8, 16}` | igual |
| Bytes de pesos por núcleo | `{256, 384, 512, 640, 768, 896, 1024, 1280}` KiB | [i7] P9: `{0.25…1.25} × L2_P` (esperado `{512, 768, 1024, 1280, 1536, 1792, 2048, 2560}` KiB); E-cores TBD |

#### 5.2.2 Fase T0.2 — H1: bloque recurrente realista (Conv.L2688-2718)

| Parámetro | Conversación | i7 |
|---|---|---|
| Barrido | `d ∈ {384, 512, 640, 768, 1024}`, `m ∈ {1, 4, 8, 16, 32}`, `K ∈ {1, 2, 4, 6, 8}` | igual; filas `d≠512` frente a C3: AMB-02 |
| Subconjunto más probable para la primera arquitectura entrenable | `d = 512`, `m ∈ {4, 8, 16}`, `K ∈ {4, 6, 8}` | igual |
| Interacción slot-a-slot | `O(Km²d)` frente a proyecciones `O(KmP)`; pequeña con `m≤16`, `d=512`, `P≈4.2M` | igual |
| FLOPs constantes `Km = C` (Conv.L2720-2749; sección hermana de H0 y H1, asignación a fase: AMB-22) | `C=32`: `(32,1),(16,2),(8,4),(4,8),(2,16),(1,32)`; `C=64`: `(16,4),(8,8),(4,16),(2,32)`. «El primer barrido es principalmente físico.» | igual |

#### 5.2.3 Tamaños por `d` (Conv.L2603-2611) y su proyección

Bytes totales con `q_efectivo=0.5625`: `16d² × 0.5625 = 9d²` B (independientes de la máquina). Promedio por worker con reparto uniforme = total/`n_w` (real: por `v_i`).

| `d` | Pesos únicos | Q4+escalas | Ryzen /4 (régimen escrito) | i7 `n_w=4` (esperado) | i7 `n_w=8` (esperado) |
|---:|---:|---:|---|---:|---:|
| 384 | 2,359,296 | 1.27 MiB | 0.32 MiB, L2 holgada | 0.316 MiB | 0.158 MiB |
| 512 | 4,194,304 | 2.25 MiB | 0.56 MiB, **candidato principal L2** | 0.5625 MiB | 0.281 MiB |
| 640 | 6,553,600 | 3.52 MiB | 0.88 MiB, frontera L2 | 0.879 MiB | 0.439 MiB |
| 768 | 9,437,184 | 5.06 MiB | 1.27 MiB, L3 compartida | 1.266 MiB | 0.633 MiB |
| 1024 | 16,777,216 | 9.00 MiB | 2.25 MiB, excede L3; DRAM | 2.25 MiB | 1.125 MiB |

[i7] Las columnas i7 son total/`n_w` con reparto uniforme y no son datos hasta T0.0 (`n_w` abierto: AMB-03). Su régimen frente a `L2_P` esperado = 2 MiB y LLC es TBD-MEASURE. El régimen real (L2 / LLC / DRAM) en el i7 es TBD-MEASURE; la traslación del criterio de rodillas es AMB-02.

#### 5.2.4 Variantes A/B/C (asignación a T0.1/T0.2: AMB-22) (Conv.L2751-2802)

Cada variante ejecuta exactamente las mismas formas, MACs, normalizaciones, no linealidades y barreras (Conv.L2753).

| Variante | Definición | i7 |
|---|---|---|
| A — recurrente residente | `K` aplicaciones, misma dirección de pesos, mismo núcleo Q4 | — |
| B — profundidad no compartida | `K` aplicaciones, `K` copias distintas, mismas formas y FLOPs; `d=512, K=8` ≈ 18 MiB | [i7] P10: 18 MiB puede quedar en LLC; DRAM forzada solo si `2.25·K > LLC útil` → AMB-04 |
| C — recurrente expulsado | Igual que A, expulsando el núcleo entre aplicaciones; «control causal, no un baseline de velocidad»; expulsión fuera de la región de tiempo del kernel o con coste registrado aparte | [i7] P11: «buffer particionado» pensado para destruir la residencia de L2 y LLC (tamaño TBD-MEASURE; regla y comprobación: AMB-21) |
| Evidencia deseada | `A ≪ B ≈ C` en coste marginal por iteración, especialmente con `m=1` o `m=4` | — |
| Atribución (Conv.L2120-2137) | A, B y C usan el mismo `m`. A↔B mide residencia y weight sharing; A↔C confirma que la ganancia procede de caché; `m=1`↔`m>1` mide matrixización y amortización de dequant; `Km` constante mide el intercambio profundidad↔workspace. «Sin esa separación, una mejora obtenida por small-GEMM podría atribuirse erróneamente al núcleo recurrente.» | — |

#### 5.2.5 Métricas a registrar

De Conv.L1376-1386: bytes leídos por los controladores de memoria; L2 hits y fills; LLC misses; ciclos por aplicación; tiempo por forward; escalado con núcleos; coste de cada barrera; throughput real de MAC; cambio abrupto al cruzar la capacidad útil de L2. De Conv.L1879 y L2434-2435: fracción del pico de MAC; instrucciones de desempaquetado por MAC. [i7] Las métricas de contadores dependen de T0.0-g.

#### 5.2.6 Fase T0.3 — Veredicto: preguntas que T0 responde (Conv.L3256-3264, proyectadas; criterios en 6.1)

1. ¿Un bloque de ≈2.25 MiB puede permanecer distribuido en las L2 de los `n_w` workers? [i7: «las cuatro L2» → L2 de los workers; AMB-03]
2. ¿La repetición compartida evita tráfico DRAM frente a capas distintas?
3. ¿`m=8` o `m=16` convierte suficientemente el trabajo en small-GEMM?
4. ¿Dónde están las rodillas L2 y L3?
5. ¿Cuánto perjudica la heterogeneidad P-core/E-core? [i7: era 1×Zen 5 + 3×Zen 5c]
6. ¿SMT ayuda o estorba?
7. ¿Qué `d, m, K` ofrece el mejor tiempo real bajo los límites de potencia del desktop? [i7: era 15–28 W]

No puede demostrar (Conv.L3268: «escalado lineal a 16 o 32 núcleos»; [i7] lo medido vale para este i7): calidad lingüística, capacidad del banco, estabilidad de entrenamiento, equivalencia con un Transformer grande. Sí puede «matar o conservar el mecanismo físico central» (Conv.L3266-3274).

### 5.3 T1 — Aprendizaje sintético pequeño (Conv.L3147-3166) — fase única T1.1

- Depende de: T0 aprobado.
- «Se entrena desde cero únicamente un modelo diminuto»: `d = 64–128`, `m = 4–8`, `K = 2–4`.
- Tareas: copiar; recuperar una clave; composición de dos hechos; actualizar slots; decidir READ/THINK; evitar colapso de slots.
- «Esto comprueba entrenabilidad, no capacidad lingüística.»
- Contrato: `d = 64–128 < 512` → bajo C3 solo puede ser `ENGINEERING_SMOKE_ONLY`. Contradicción abierta: AMB-01.
- Plataforma: «tareas sintéticas diminutas» están en la lista del equipo de validación (Conv.L3099) → [i7] P18.

### 5.4 T2 — Destilación del núcleo sin banco (Conv.L3168-3217) — fases T2.1-T2.4 = pasos (1)-(4) de abajo

- Maestro: Transformer denso preentrenado, congelado (identidad no fijada: AMB-09).
- Entrada del estudiante: token actual; estado previo; slots; representaciones resumidas del contexto.
- Pérdida (la conversación dice «puede combinar», Conv.L3179): `L = λ_CE·L_token + λ_KL·D_KL(p_T‖p_S) + λ_h·Σ_r ‖A_r S^(r) − H_T^(ℓ_r)‖²`, con `A_r` proyección de slots al espacio del maestro.
- Currículo: `K = 2 → 4 → 6`; `m = 4, 8 y 16` como ejecuciones separadas; no cambiar `m` y `K` simultáneamente en una misma corrida inicial.
- Evitar BPTT conversacional completo: (1) trazas del maestro offline; (2) estado siguiente entrenado desde el estado objetivo anterior; (3) scheduled sampling; (4) unrolls cortos de 8, 16 y 32 tokens con truncated BPTT.
- «La destilación reduce el problema de descubrimiento, pero no elimina los FLOPs de entrenamiento del estudiante.»
- Contexto escrito (Conv.L3103-3129): weight sharing no abarata los FLOPs frente a `K` capas no compartidas; conserva activaciones de las `K` vueltas; hay dos recurrencias (profundidad `K` y persistencia del workspace entre tokens).
- Plataforma y método: «en GPU mediante inicialización parcial y destilación desde un maestro denso» (Conv.L3101; la inicialización parcial no está especificada: AMB-09). [i7] La GPU local no sirve como evidencia (C7): AMB-07.
- `d = 512` por contrato.

### 5.5 T3 — Memoria externa (Conv.L3219-3239) — fases T3.1 (banco y retriever), T3.2 (entrenamiento con núcleo congelado), T3.3 (descongelado parcial)

- Primera versión: banco congelado; keys/values creados offline desde corpus o estados del maestro; retriever pequeño entrenable; núcleo inicialmente congelado.
- Se entrena primero: formulación de consultas; selección top-k; escritura de evidencia en slots; detección de recuperación insuficiente. Después se descongela parcialmente el núcleo y se afina conjuntamente.
- El banco se construye exclusivamente desde datos permitidos para entrenamiento; se reportan cobertura y tamaño.
- Arquitectura objetivo (Conv.L2310-2379): `S_t ∈ R^{m×d}`; READ (`Q_t = Query(S_{t−1}, x_t)`, `R_t = Retrieve(M, Q_t)`, `WriteEvidence`) → THINK (`S^(r+1) = S^(r) + G_r ⊙ F_θ(S^(r), E_roles, E_r)`, `r = 0…K_t−1`, pesos de `F_θ` shardeados en las L2) → CHECK (slot de control: emitir / pensar otra vuelta / consultar otra vez) → EMIT (`LMHead(S_t,output)`). El banco almacena datos y representaciones, no fragmentos de pesos.
- Roles de slot sugeridos (Conv.L2154-2161): 1–2 control y consulta; 4–8 evidencia; 4–8 trabajo intermedio; 1–2 salida y verificación; con embeddings de rol y puertas de escritura.
- Los parámetros dependientes de `r` deben ser diminutos y residentes (Conv.L1561); componentes prelude/coda/ancla/puerta: Conv.L1541.
- Recuperación conservadora e iterativa: THINK inicial → varias consultas → READ → integrar → THINK → verificar contradicciones → READ AGAIN si procede (Conv.L2289-2299). Un ANN certifica vecinos, no suficiencia de conocimiento (Conv.L2260-2268).

### 5.6 T4 — Cuantización (Conv.L3241-3252) — fases T4.1-T4.4 = los cuatro pasos del orden

- Orden: BF16/FP16 estable → Q8 → Q4 con fine-tuning consciente de cuantización → validación nativa. [i7] «en laptop» → en el i7 (P21).
- «No comenzaría entrenando directamente con el núcleo Q4»: la recurrencia puede amplificar errores repetidos; primero separar fallos de optimización de fallos de cuantización.

### 5.7 Contabilidad y comparación (Conv.L2930-3084; etapa no asignada: AMB-08)

- Ledger: `P_core`; `P_shell` (embeddings + prelude + coda + head + gates); `P_retriever`; `P_memory-learned`; `B_memory-static`; `B_index`. `P_trainable-total = P_core + P_shell + P_retriever + P_memory-learned`; `B_persistent-total = B_pesos cuantizados + B_memory-static + B_index`. Banco estático: almacenamiento, no «parámetros»; banco aprendido: ambos.
- Factorial obligatoria: D0/D1 (densa sin/con banco), R0/R1 (recurrente sin/con banco). Contrastes `R0−D0`, `D1−D0`, `R1−R0`, `(R1−R0)−(D1−D0)`. «Sin D1, cualquier ganancia de R1 podría ser simplemente RAG convencional».
- Cuatro regímenes de «igual tamaño»: parámetros únicos; FLOPs por token; almacenamiento persistente total; latencia o bytes DRAM por token.
- Descripción del sistema: «4M de núcleo + X MB de shell + 1 GB de memoria + Y MB de índice».
- Barrido de capacidad del estudio de ML: `2×, 4×, 8×, 16×, 30×`, midiendo dónde colapsa la capacidad (Conv.L1501-1507).

### 5.8 Presupuesto por token (Conv.L2163-2248)

- Meta inicial 50 tok/s = 20 ms/token; para esa meta no hace falta rediseñar primero la cabeza (Conv.L2202).
- Presupuesto DRAM/token = `BW_D / 50` [i7: `BW_D` TBD-MEASURE; la conversación usaba 60 GB/s → 1.2 GB/token], reservando tráfico para: primera carga del núcleo, cabeza, activaciones, índice ANN, escrituras, memoria del sistema, accesos aleatorios (Conv.L2216-2224).
- Cómputo del núcleo: `2PKm` ops/token. [proyección con `P = 4,194,304`, solo aritmética] `K=6, m=16`: 805,306,368 ops/token = 40.3 GOP/s a 50 tok/s (la conversación usaba `P=24M`: 4.608 GFLOP/token, 230.4 GOP/s). Capacidad del i7: TBD-MEASURE.

---

## 6. Criterios de paso/fallo (tal como están escritos)

### 6.1 Gate de T0 (Conv.L2804-2892)

«No usaría un speedup total arbitrario como única condición. Separaría las propiedades.» (Conv.L2806)

**Residencia.** `c_A(K) = (T_A(K) − T_A(1))/(K−1)`, `c_B(K)` análogo, `ρ_resident = c_A(K)/c_B(K)`. Para `d=512`, `m=1` o `4`, `K=8`:
- `ρ ≤ 0.50` → gate mínimo aprobado
- `ρ ≤ 0.25` → evidencia fuerte
- `ρ ≈ 1.00` → no existe ventaja útil de residencia
- «C debe acercarse a B. Si A y C resultan prácticamente iguales, la hipótesis L2-residente ha fallado.»
- [i7] umbrales sin cambio numérico; su premisa (B en DRAM a `K=8`) no se cumple necesariamente: AMB-04.
- [MD/34, 2026-10-11] Lectura ORIGINAL inequívoca de este gate (se conserva; no se borra): mínimo `ρ≤0.50`, fuerte `ρ≤0.25`, estimador MARGINAL `c(K)=(T(K)−T(1))/(K−1)`, matrixización en `m=8/16`, `K=8`. La adaptación legítima al i7 es solo `K_gate=16` (B de 18→36 MiB para superar la LLC nominal). Los criterios que gobernaron la ejecución histórica de T0.1 (MD/29–32: `ρ₁≤2/3`, `ρ₄≤0.90`, `m_gate=4`, coste promedio amortizado) constan en §7 como DESVIACIÓN CIENTÍFICA reconocida por Sol (MD/34 D-C); ambos resultados se reportan (H0_FAIL con las dos lecturas: ρ original (c_A/c_B, marginal) K=8 0.91402, K=16 0.90630 (el operador max(A/B,A/C) de MD/29 con referencia K=1 de C improvisada da 0.9069, solo informativo); matrixización S8≈1.759, S16≈1.766 PASS).

**Matrixización.** En A, `η(m) = MAC/s efectivos / pico MAC/s medido`.
- `MAC/s(m=8 u 16) / MAC/s(m=1) ≥ 1.5` → gate mínimo; `≥ 2.0` → señal fuerte.
- «Si la curva permanece plana desde m=1, la hipótesis de convertir el batch interno en small-GEMM no aporta lo esperado.»

**Rodillas de caché.** «Debe aparecer una transición visible: d=384/512 → régimen L2 / d=640 → frontera / d=768 → L3 / d=1024 → DRAM». «Si no aparece ninguna discontinuidad o degradación progresiva al cruzar esos tamaños» (Conv.L2876): el núcleo no se mantiene residente; el sharding no funciona; las activaciones expulsan los pesos; o el kernel no usa la jerarquía como se esperaba. [i7] traslación: AMB-02 (P12).

**SMT.** Resultado reportado aparte (`n_w` físicos vs SMT [i7, era 4T/8T]). Puede aceptarse para producción si mejora el tiempo total; nunca cuenta como duplicación de L2 ni de capacidad residente.

**Consecuencia.** «Si H0/H1 no pasan, la línea muere antes de gastar GPU.» (Conv.L3145). Qué combinación de propiedades constituye «pasar»: AMB-10.

### 6.2 Predicciones falsables registradas (Conv.L2432-2451)

1. La fracción del pico de MAC sube claramente entre `m=1` y `m=8` o `16`.
2. Las instrucciones de desempaquetado por MAC caen al aumentar `m`.
3. La diferencia A↔B crece principalmente con `K`, no con `m`.
4. A↔C desaparece cuando se fuerza la expulsión.
5. A FLOPs constantes, la latencia cae al principio y después se aplana o sube por interacción entre slots.

Forma esperada: curva con rodilla (m pequeño limitado por L2/dequant/ocupación; m intermedio small-GEMM eficiente; m grande limitado por cómputo, registros y atención entre slots), no «superlineal».

### 6.3 T1–T4

`Conversacion.md` no escribe umbrales numéricos de paso para T1–T4. Solo: T1 «comprueba entrenabilidad, no capacidad lingüística» (Conv.L3166); T4 separa fallos de optimización y de cuantización (Conv.L3252). No se añaden criterios (AMB-11).

---

## 7. Ambigüedades abiertas (no resueltas)

| ID | Ambigüedad | Referencia |
|---|---|---|
| AMB-01 | T1 entrena `d=64–128`; C3 lo reduce a `ENGINEERING_SMOKE_ONLY`. ¿T1 se queda como smoke, se hace a `d=512` o cambia? | Conv.L3149-3155 vs C3 |
| AMB-02 | Filas `d ∈ {384, 640, 768, 1024}` de H1 y criterio de rodillas: ¿son caracterización de hardware permitida o «sustitutos» prohibidos? ¿Cómo se traslada al i7 el criterio, si con `n_w=8` P-cores esperados `d≤768` cae con holgura en L2 y `d=1024` (1.125 MiB/worker = 0.5625×`L2_P`) cabe en el presupuesto 0.50–0.70×`L2_P` salvo en su tramo inferior? ¿Aplica la misma pregunta a los tamaños por núcleo de H0 (Conv.L2682-2683), que no están ligados a `d`? | Conv.L2693, L2867-2881 |
| AMB-03 | Workers primarios en un i7 híbrido: «4, uno por núcleo físico» eran todos los núcleos del Ryzen. ¿4 P-cores literales, los `N_P` P-cores, o P+E (E con L2 compartida)? | Conv.L2649-2653, L3284-3285 |
| AMB-04 | Umbrales de ρ fijados a `K=8` porque B tocaba DRAM («supera ampliamente los 8 MB de L3»). En el i7 B(`K=8`) puede vivir en LLC. ¿Mismos umbrales a `K=8`, evaluar a `K=16`, otra cosa? | Conv.L2780, L2828-2834 |
| AMB-05 | **CERRADA (Sol, MD/22.md, 2026-10-09).** La regla «comparar rutas AVX2 y AVX-512» presupone AVX-512. Resolución: ISA_REF = referencia escalar/correctness (no compite por rendimiento); ISA_BASE = AVX2; ISA_OPT = AVX-VNNI de 256 bits si CPUID lo confirma (si no, NOT_AVAILABLE y AVX2 es la única ruta vectorial contractual). AVX-512: NOT_AVAILABLE si CPUID no lo anuncia (no se emula ni se sustituye por una comparación ficticia). La elección AVX2 vs AVX-VNNI se hace por medición, tras demostrar equivalencia de correctness, en T0.1 (H0). No se añaden rutas ISA tras observar timings (requeriría preregistro). | Conv.L2928 |
| Resolución de Sol (MD/24.md) sobre adecuación del kernel | Tras medir T0.0 con el kernel tal cual (≈1.12e9 MAC/s por P-core, ≈0.64 GB/s de pesos frente a `BW_D(8)`=67.3 GB/s), Sol declaró el kernel `NOT_H0_READY` y autorizó UNA fase acotada de optimización dentro de T0.0 antes de T0.1; T0.0-e se reporta como `P_instr` (techo de instrucción) y `P_Q4_peak` (pico operativo sobre m); gate previo a T0.1 (revisado por Sol en MD/25 tras ver el código real): `HOT_PATH_CLEAN` PASS y `D_read = BW_Q4_logical_8P/BW_DRAM_read_8P ≥ 1.00` en AVX2 o AVX-VNNI (el gate D_mem ≥ 0.50 de MD/24 queda SUPERSEDED); si no, `KERNEL_MEMORY_PRESSURE_INADEQUATE` y T0.1 = HOLD. Detalle en T0.md 3.1. | Roadmap 5.1 (T0.0-e), 5.2 |
| Resoluciones de Sol (MD/23, MD/26–28) sobre cierre de T0.0 | MD/23: RAM 2 módulos/2 canales FIRMWARE_REPORTED (SEP = CONFLICTING_OBSERVATION), PL1/PL2 = UNAVAILABLE_WITH_ALLOWED_INTERFACES, formato Q4 sintético y N=31/warmup 5/mediana/IQR+MAD. MD/26: acumulación FP32 ratificada solo para T0 (group_dot entero exacto; salida |y−y_ref| ≤ 1e-5+1e-5|y_ref|; AVX2≡VNNI bit a bit); bloqueantes B1–B4. MD/27: GO de la remedición oficial con `TIMED_REGION_MIN`=100 ms, calibración ≥150 ms, `SERIES_INVALID_TIMER_TOO_SHORT`, ventana 8P sin join, P_ref por menor ID. MD/28: sesión oficial aceptada (D_read AVX2 1.007798 / VNNI 1.040325, gate PASS); `KERNEL_MEMORY_PRESSURE_ADEQUACY`=PASS; A (4P) y C (8P+8E) = DEFERRED_TO_T0_1; heterogeneidad E previa = artefacto del kernel antiguo; faltan 16 series P-core m=1 y un único intento de frecuencia efectiva (resultado: UNAVAILABLE_WITH_QUALIFIED_TOOLS); la ruta ISA para H0 no se congela hasta T0.1; las 13 AMB de T0.1 se presentan a Sol solo con T0.0 Completed. Detalle en T0.md 3.1. | Roadmap 5.1, 5.2 |
| Resolución de Sol (MD/29.md, 2026-10-11) de las AMB de T0.1: **AMB-02, 03, 04, 10, 12, 13, 14, 15, 18, 19, 21, 22 CERRADAS** (AMB-05 ya cerrada) | d=512 único evidencial, presión de caché variando W (no d; 11 tamaños W de 262,152 a 3,145,734 B); primario 8 P físicos sin SMT, ruta AVX-VNNI-256; K_gate=16 (K=8 descriptivo), m_gate=4; K_CAN={1,2,4,8,16}, M_H0={1,2,4,8,16,32}, FLOPs constantes K·m∈{32,64} solo A/B descriptivo (K=6, K=32, Km=96 retirados); H0_PASS = A/A R_noise≤0.05 ∧ expulsión validada ∧ H0-R1 (rho_1≤2/3, delta_BC≤0.15) ∧ H0-R4 (rho_4≤0.90) ∧ H0-M (S4≥1.50) ∧ H0-K (rodilla persistente ≥1.15×); STRONG_RESIDENCY_SIGNAL iff rho_1≤0.50; barridos y criterios antiguos (eta≈0.6, 2× mínimo, variantes D/E) retirados o descriptivos; microoperadores/bloques → T3, Δh → T2; E como cluster de 4 con 4 MiB L2 compartida; presupuesto L2 = 0.70× límite inferior de la rodilla medida; H0 gate propio (H0_PASS/H0_FAIL→FALLIDA_LINEA_MUERTA_PHYSICAL_PREMISE/H0_INVALID); expulsión 8→16 MiB/worker; A/B/C y FLOPs constantes pertenecen a H0. Umbrales numéricos de H1 se fijan al abrir T0.2. Detalle normativo en T0.md 4.1. | Roadmap 5.2.1–5.2.6, 6.1 |
| Resoluciones de Sol (MD/22.md) sobre SMT y P/E | La fila SMT (6.1) se mide en T0.1 (H0) como `CHARACTERIZATION_ONLY` salvo que T0.1 la declare parte de un gate; T0.0 solo verifica presencia, siblings, afinidad y topología. Heterogeneidad P/E: T0.0 solo inventario/topología; T0.1 caracteriza prospectivamente (P-core homogéneo, E-core homogéneo/cluster si procede, P+E mixto como secundario) y selecciona `PRIMARY_WORKER_POLICY` (homogénea salvo razón cuantitativa); T0.2 la revalida en el bloque más realista y compara la mejor alternativa P/E si T0.1 mostró diferencia material. La comparación exacta de workers/normalización se congela con las AMB de T0.1. | Roadmap 5.2.6, 6.1 |
| AMB-06 | ¿La prohibición «No se entrenará desde cero un modelo lingüístico significativo» en la plataforma de validación se aplica igual a la CPU del i7? | Conv.L3086-3099 |
| AMB-07 | «El entrenamiento lingüístico se hará en GPU»: la GPU local es solo validación (C7). ¿Qué GPU entrena T2–T4? | Conv.L3101, L3145 |
| AMB-08 | Ledger y factorial D0/D1/R0/R1 con cuatro regímenes: «obligatoria», pero sin etapa asignada; tampoco la tiene el barrido de capacidad 2×, 4×, 8×, 16×, 30× (Conv.L1501-1507). | Conv.L2930-3084, L1501-1507 |
| AMB-09 | No fijados: maestro (modelo, tamaño, `d`), tokenizer y `V`, corpus de entrenamiento/evaluación. `V=32,000`, `d=768` son ejemplos. Tampoco está especificada la «inicialización parcial» (Conv.L3101). | Conv.L3170, L2167-2176 |
| AMB-10 | Qué combinación de propiedades (residencia, matrixización, rodillas, SMT) significa «H0/H1 pasan»; ρ se mide con `m=1` o `4` y η con `m=8` u `16`. | Conv.L2806, L2828, L2854, L3145 |
| AMB-11 | T1–T4 no tienen criterios numéricos de paso ni regla de selección de qué `(m, K)` de T0 pasa a T2. | Conv.L2698, L3147-3252 |
| AMB-12 | Conjuntos inconsistentes: `K` en H0 `{1,2,4,8,16}`, H1 `{1,2,4,6,8}`, decisión final `{1,2,4,6,8,16}`; `m=2` en H0 y no en la final; `K=32` en FLOPs constantes; `Km=96` del barrido previo ausente después. | Conv.L2681, L2695, L3294-3297, L2731, L2403 |
| AMB-13 | Barrido previo de Sol (barrido A/B/C original `K∈{1,2,4,8,16,32}` × `Pq∈{0.25,0.5,0.75,1.0,1.25,2.0}`×L2 útil agregada, Conv.L1361-1374; Barrido 1 con `K ∈ {4,8}`, `m ∈ {1,…,32}`; Barrido 2 `Km ∈ {32,64,96}`; variantes D «Q4 fusionado» y E «predesempaquetados a Q8/int8») y criterios previos («rodilla clara», «al menos ≈2×», y «Si m=16 no sube esa fracción [del pico de MAC] de ~0.2 a ~0.6, mi argumento de GEMM se cae», Conv.L1879): ¿siguen vigentes junto al gate final o quedan sustituidos? | Conv.L2381-2430, L1388-1395, L1361-1374 vs L2804-2892, L3299-3303 |
| AMB-14 | Los tres experimentos de la etapa de microoperadores/bloques (break-even de tamaño de bloque con kernel real; coherencia temporal por bloques; distancia de reutilización `M(C)`) y el de descomposición Δh: ¿retirados tras el viraje al núcleo recurrente o pendientes (p. ej. para el banco)? | Conv.L971-977, L190-228, L447 |
| AMB-15 | La tesis requiere L2 **privadas** con shards fijos; los E-cores comparten L2 por cluster. ¿Cómo se tratan? | Conv.L1265-1272, L2469 |
| AMB-16 | Componentes de la arquitectura objetivo sin etapa ni criterio: halting/CHECK, política READ AGAIN, cabeza selectiva, diseño del banco/ANN, KV/atención en las vueltas. | Conv.L1781, L2284-2299, L2358-2377 |
| AMB-17 | La meta de 50 tok/s no está ligada a ningún gate ni define qué sistema la debe cumplir. Sin fase asociada; no bloquea ninguna transición. | Conv.L2202 |
| AMB-18 | Presupuesto L2: 60–75 % (Conv.L1293) frente a 0.50–0.70 MiB/núcleo (Conv.L2563). ¿60–75 % de L2, 50–70 % proporcional, o 0.50–0.70 MiB absolutos en el i7? P6 y P9 dependen de esto. | Conv.L1293, L2560-2572 |
| AMB-19 | La conversación llama a H0 y H1 «dos gates» (Conv.L2666) y dice «Si H0/H1 no pasan, la línea muere» (Conv.L3145). ¿H0 se evalúa como gate propio (veredicto y posible FALLIDA_LÍNEA_MUERTA) antes de iniciar H1, o ambos se juzgan juntos en T0.3? ¿Qué criterios de 6.1 aplican a H0, que no barre `d`? | Conv.L2666, L3145 |
| AMB-20 | El currículo de K y las corridas separadas por `m` (Conv.L3198-3208): ¿aplican a cada uno de los pasos (1)–(4) de T2 o solo al (2)? | Conv.L3198-3215 |
| AMB-21 | Conv.L2786 da 32–64 MiB para un chip con 4 MB de L2 + 8 MB de L3 (Conv.L2500). Abierto: (a) regla de tamaño en el i7 (¿proporcional, solo «mayor que L2+LLC», otra?); (b) si se exige una comprobación independiente de que la expulsión funciona antes de interpretar A≈C (Conv.L2836); (c) con qué instrumento. | Conv.L2500, L2786, L2836 |
| AMB-22 | El barrido a FLOPs constantes (Conv.L2720-2749) y las variantes A/B/C (Conv.L2751-2802) no se asignan explícitamente a H0 ni a H1. ¿En qué fase o fases se ejecutan? (`m=2` y `K=16` de `C=32/64` solo existen en el conjunto de H0; `K=32`, par `(32,1)`, no existe ni en H0 ni en H1: ver AMB-12). | Conv.L2668-2802, L3139 |

**Registro histórico MD/29–34 (T0.1/H0, 2026-10-11; no se reescribe).** MD/29–32 gobernaron la ejecución histórica de H0, incluidas sus desviaciones científicas respecto de §6.1 (umbrales `ρ₁≤2/3` y `ρ₄≤0.90` en vez de `≤0.50`; `m_gate=4`; coste promedio; barrido W reducido), reconocidas por Sol en MD/34 D-C (solo `K_gate=16` es adaptación legítima de hardware). Adjudicación (MD/34 D-A): **H0_FAIL firme** → `FALLIDA_LINEA_MUERTA_PHYSICAL_PREMISE` (limitada al kernel/configuración/protocolo examinados; no refuta la recurrencia en general); T0.2/H1 y T0.3 `NOT_STARTED`; cualquier investigación posterior exige hipótesis y protocolo nuevos y prospectivos (no repetición para rescatar este FAIL). H0-K: `H0_K_LITERAL_MD29=PASS` (histórico) pero `H0_K_SCIENTIFIC=NOT_ESTABLISHED`, `C_L2_knee=null`, `B_L2_operational=null` (MD/34 D-B; criterio confundido con el trabajo).

**Campaña T0′ `T0P-Q4-RESIDENCY` (MD/35, 2026-10-11).** Protocolo nuevo y prospectivo aprobado por Sol, informado por el H0_FAIL firme de T0.1 (no lo cambia ni lo rescata); criterios en T0.md §10: competencia ≥90 % de ggml Q4_0 (m=1,4,8,16; por core y 8P), margen D′_hot≥2.00, residencia ρ≤0.50 marginal (K=16, m=1), matrixización max(S8,S16)≥1.50, control B≈C≤0.15, expulsión con CLFLUSHOPT, páginas grandes, barrido hasta ~128 MiB agregados. Medición oficial no autorizada hasta revisión y congelación del protocolo ejecutable. Estado: EN_PREPARACIÓN (sin fase numerada en el tablero hasta que Sol la asigne).

### TBD consolidados

TBD-VERIFY: `N_P`, `N_E`, `L2_P`, `L2_E` y su compartición, `LLC`, RAM (módulos/canales/velocidad), ISA por CPUID, bare metal, herramienta de contadores Intel en Windows.
TBD-MEASURE: frecuencias sostenidas P/E, límites de potencia/térmicos, `BW_D(n)`, `BW_L2,i`, `BW_L2,Σ`, `r`, `v_i`, pico MAC por ruta, LLC útil, L2 útil (rodilla real), bytes físicos reales del formato Q4, tamaño del buffer de expulsión de C, coste de barrera `T_sync`, capacidad en GOP/s frente a `2PKm`.

### Observaciones del autor (no normativas)

- Obs-1: el encargo indicaba 2291 líneas (número de líneas no vacías); el archivo tiene 3328 líneas en total. La trazabilidad usa la numeración del archivo real.
- Obs-2: el kernel se reconstruye desde cero (C6); antes de H0 conviene que su primera versión ya reporte bytes físicos asignados (Conv.L2611), porque P6, P9 y las columnas i7 de 5.2.3 dependen de ese dato.

---

## 8. Matriz de trazabilidad

| Ítem del roadmap | Conv. |
|---|---|
| Marco del programa (Transformer como evidencia, fallos pre-Transformer, contrato CPU/RAM, métrica calidad/bytes) | L7-167, L374-417 |
| Métricas globales del programa | L391-404 |
| Rechazos de diseño (todas las capas, todos los pesos, misma profundidad…) | L316-336 |
| Antecedentes: microoperadores, Δh, router jerárquico (sustituidos; AMB-14) | L171-280, L340-372 |
| Crítica de dispersión fina y dos medidas iniciales | L425-467 |
| Corrección: residencia física, ventana, falso negativo, tres experimentos | L469-988 |
| Reutilización en profundidad, `AI_DRAM = 2K/q` | L990-1080, L1092-1150 |
| `AI_L2 = 2/q`, techo `S_max`, presupuesto L2 60–75 % | L1153-1301 |
| Diseño A/B/C original, barrido `K`, `Pq`, métricas, criterio ≈2× | L1305-1395 |
| Cabeza de vocabulario, KV, scratchpad | L1399-1451, L2163-2204, L1858-1862 |
| Capacidad factual no crece con vueltas; barrido 2×–30× | L1455-1507 |
| Tensión hardware/aprendizaje; modulación por paso | L1511-1563 |
| Router binario, READ/THINK/VERIFY/WRITE | L1567-1619 |
| Cota del falso negativo | L1623-1691 |
| Arquitectura tras correcciones | L1695-1806 |
| Estado matricial `m`, `AI_L2 = 2m/q`, `S_res = K/(1+(K−1)/r)` | L1815-2029, L1894-1966 |
| `m` vs `K`, tabla de dependencias, `T(m)` | L2031-2137 |
| Workspace latente, roles de slot | L2139-2161 |
| Presupuesto 1.2 GB/token, `2PKm` | L2206-2248 |
| ANN no certifica suficiencia | L2250-2299 |
| Arquitectura READ/THINK/CHECK/EMIT | L2310-2379 |
| Barridos 1 y 2, variantes D/E, predicciones falsables | L2381-2451 |
| Tesis corregida | L2467-2483 |
| Plataforma de validación, definición de «X parámetros», núcleo 2–2.5 MiB | L2494-2500 |
| `r_n`, `BW_D(n)`, configuración de memoria | L2504-2546 |
| Presupuesto L2 0.50–0.70 MiB | L2548-2572 |
| Bloque `16d²`, `q_efectivo`, tabla `d` | L2574-2620 |
| Sharding por `v_i`, workers, SMT | L2622-2662 |
| H0 | L2666-2686 |
| H1 | L2688-2718 |
| FLOPs constantes y variantes A/B/C (asignación: AMB-22) | L2720-2802 |
| A/B/C exactas | L2751-2802 |
| Criterios (ρ, η, rodillas, SMT) | L2804-2892 |
| Medición en Windows | L2894-2928 |
| Ledger, factorial, regímenes | L2930-3084 |
| Estrategia de entrenamiento y coste de weight sharing | L3086-3129 |
| T0 | L3133-3145 |
| T1 | L3147-3166 |
| T2 | L3168-3217 |
| T3 | L3219-3239 |
| T4 | L3241-3252 |
| Qué valida la plataforma | L3254-3274 |
| Decisión final y afirmación a probar | L3276-3321 |
| Tablero, fases y ciclo de estados (4.2-4.3) | Fases: L3133-3252 (por fase en 4.2). Ciclo de estados y roles: instrucción del usuario, no de `Conversacion.md`; sus únicos estados de fallo científico se apoyan en L2836 y L3145 |
