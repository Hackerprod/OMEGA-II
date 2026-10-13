# OPUS — Auditoría ciega de fidelidad T0′ (T0P-Q4-RESIDENCY), 2026-10-12

**Alcance.** Auditoría de solo lectura. Este informe es el único archivo que he creado. No ejecuté builds, mediciones, EMON ni Codex.

**Leído:**
- `Roadmap.md` (§0, §6.1, tablero, L193, L410–412);
- `T0.md` §10 completo (L182–226: S-2…S-24 y FREEZE);
- `MD/51.md` completo y `MD/OPUS_H0_ANALISIS_INDEPENDIENTE.md`;
- fuentes r16: `harness/{main,residency,provider_aux,provider_omega,topology}_r16.*` y `build_r16.cmd`; `collector/{official_run,qualify,residency_derive,residency_plan}_r16.py`;
- `validator/s20_validator.py` (partes), `evict/evict.cpp`, `kernel/q4_kernel.cpp` (intrínsecos), `common/common.cpp` (verify_s12);
- `evict/analyze_d8_r6.ps1` (L32–36) y `ESTADO_PROYECTO.md`.

**Hashes comprobados.** Coinciden con MD/51: `kernel/q4_kernel.cpp` 6d50162a…, `residency_r15.cpp` fdebbda9…, `official_run_r15.py` 8e39015d…, `qualify_r15.py` 69224c4e…, `residency_derive_r15.py` 8534317c… y `s20_validator.py` 8257cf3d….

**Rutas.** Todas son relativas a `C:\Projects\OMEGA\Ejercise\T0P\` salvo que se indique otra raíz.

**Leyenda de clasificación.**
- (a) Cambio de método prospectivo y declarado por Sol.
- (b) Relajación de un criterio científico.
- (c) Incoherencia entre textos o entre la spec y el código.

## Veredicto global: DESVÍOS GRAVES

**Cadena de enmiendas: sin relajaciones ocultas.** No encontré ninguna enmienda que convierta en PASS un FAIL científico sin dejarlo registrado.
- S-22, S-23 y S-24 están declaradas como posteriores a datos observados o como prospectivas.
- Conservan G_ggml 0,90, G_Torch 1,20, H-KQ 33,1, D′ 2,00, ρ 0,50/0,25, δBC 0,15, S 1,50/2,00, A/A 0,05, plateau 3 %, N=31, 8 P, 16 W y 420 condiciones.
- El código r16 transcribe esos umbrales correctamente (`collector/qualify_r16.py:17-24,472-510`; `collector/residency_derive_r16.py:403-476`).

**Lo grave está en el código r16 y en el diseño.**
- Un control final del código está invertido.
- La calibración de R viola S-21 R4.
- La cadena oficial confía en estados autodeclarados.
- Hay dos confusores de diseño que pueden producir un FAIL espurio. Según S-19, ese FAIL cerraría la línea sin reintento.

---

## Hallazgos

### 1. BLOQUEANTE (c): el control frío D8 final está invertido

**Evidencia.**
- `collector/official_run_r16.py:420-425` calcula `cold_llc_hit = 1 - cold_M3/cold_D2` y falla si `cold_llc_hit < .90`. Es decir, exige que ≥90 % de los accesos fríos ACIERTEN en LLC.
- S-19 §3 y S-22 (`T0.md:216,222`) exigen lo contrario: «fracción de accesos fríos que FALLAN en LLC ≥ 0,90».
- La fixture positiva (`official_run_r16.py:356`, `cold_M3=100` de `cold_D2=100000`) es un «frío» que no falla en LLC, y aun así pasa.
- El analizador de cualificación sí usa el sentido correcto (`evict/analyze_d8_r6.ps1:35-36`, `$old -ge .90` con OFFCORE como denominador). La revalidación final r16, en cambio, usa otro denominador (D2) y el sentido invertido.

**Efecto.** Una expulsión real válida (fallo LLC ≈0,996) daría HOLD al terminar la campaña. Una expulsión fallida pasaría.

**Recomendación.** Juez: corregir antes de AUTHORIZED.json, con una contraprueba de «frío que acierta en LLC ⇒ HOLD». Sol: fijar si el denominador gobernante frío es OFFCORE (S-22 «originales») o D2.

### 2. ALTA (c): R global único en lugar de R(W,m,K)

**Evidencia.**
- `harness/residency_r16.cpp:771-811` usa un solo `calibrated_r` y exige que TODOS los eventos de calibración del calendario alcancen los objetivos con el mismo R. Eso mezcla K=1/16 y m=1…16.
- La fila registra `"R_policy":"R(W,m,K)_common_A_B_C_fixed_N31"` (L831), lo cual es falso.
- S-21 R4 (`T0.md:220`) y S-24 §3.1 (`T0.md:226`) exigen R por (W,m,K), común solo a A/B/C.

**Efecto.** Infla R en las unidades largas. El derivador tampoco lo detecta, porque `r_by_unit` lee el mismo valor en todas.

**Recomendación.** Juez: corregirlo, más una prueba negativa que rechace un R idéntico entre K distintos si la regla de duplicación habría dado valores distintos.

### 3. ALTA (riesgo de diseño, no tratado en ningún texto): barreras con espera bloqueante entre aplicaciones

**Evidencia.**
- Los workers esperan con `std::condition_variable` entre cada orden: tres rondas por aplicación (`harness/residency_r16.cpp:113-121, 429-458, 497-500`).
- No hay espera activa ni control de estados C. Ningún texto de T0.md §10 ni de MD/46–51 trata C-states (búsqueda sin resultados).

**Riesgo físico.**
- Un P-core que entra en C6 durante la espera pierde su L1/L2 (power-gating).
- Mientras tanto, el SO puede planificar otros hilos en ese core y contaminar su L2.
- Las dos cosas destruyen la ventaja de A, que vive en L2, y empujan ρ hacia 1.

**Consecuencia.** El sesgo va contra la hipótesis. Con S-19 («FAIL válido ⇒ cierre sin reintento», `T0.md:216`), un FAIL de ρ podría ser artefacto del harness y aun así cerrar la línea.

**Hipótesis no descartada.** Este mismo mecanismo también es compatible con el plano L2 = LLC de H0 (mi análisis A.2 lo atribuyó a cómputo). NO VERIFICADO: no medí residencias C6 ni a_i.

**Recomendación.** Sol, antes de autorizar, debe exigir:
- barreras de espera activa (`_mm_pause`) para los workers;
- publicar por condición `a_i = ΣREF_TSC/(f_TSC·Δt)`, ya disponible en S-20 C;
- o un evento de residencia C6 de core;
- un control A con K=16 que demuestre que la segunda aplicación y siguientes leen de L2.

### 4. ALTA (riesgo de diseño): la excursión de frecuencia hace casi segura un HOLD global

**Evidencia.**
- MD/51 §5 (`MD/51.md:278-304`) registra 1 excursión inválida en 120 muestras-core del smoke.
- S-20 E y S-24 (`T0.md:218,226`): una muestra inválida invalida el intento. Hay un único reintento global y no se descartan ventanas.

**Cálculo de orden de magnitud (ESTIMACIÓN).**
- La campaña tiene ≈32 102 eventos; en residencia son ×8 cores, ≈1,1·10⁵ muestras-core.
- Con la tasa del smoke se esperan cientos o miles de excursiones, y P(intento limpio) ≈ 0.
- La prueba acotada de §5 (36 bloques ≈ 288 muestras-core) pasaría por azar ≈9 % de las veces con esa misma tasa. No puede demostrar viabilidad.

**Matiz.** La tasa del smoke procede de r15, con las interferencias ya señaladas por Sol (coordinador en LP0, flush de X/Y). Puede bajar en r16, pero no está demostrado.

**Recomendación.** Sol: fijar antes de autorizar qué tasa de excursión hace viable la campaña y con qué potencia estadística la demuestra la prueba acotada. Sin eso, el resultado previsible es HOLD sin conclusión científica.

### 5. ALTA (c): la cadena oficial r16 adjudica sobre estados autodeclarados y no está conectada

**Evidencia.**
- `collector/official_run_r16.py:127` invoca `harness_r16.exe --acquire-r16 <fase>`, pero `harness/main_r16.cpp:194-213` no tiene esa opción: devolvería uso inválido (rc=2).
- Los validadores de fase (`official_run_r16.py:159-263`) aceptan campos declarados por la evidencia (`"kernel_qa":"PASS"`, `"bw_dram":"PASS"`, `"valid_scientific_inputs":true`, `"verdict"`). No invocan `qualify_r16.py`, `residency_derive_r16.py` ni `s20_validator.py` sobre crudos. Es el mismo defecto que Sol cerró como D10/D14 abiertos (MD/51 §1).
- `harness/provider_aux_r16.cpp:121-122` escribe `"status":"PASS"` y `"disassembly_qa":"PASS"` según la etiqueta OFFICIAL, no según las comprobaciones de afinidad y sink de esa muestra.
- L131 valida la QA de desensamblado buscando cualquier `"PASS"` posterior a `"state"`, no con un parser estricto (S-18 A6 / D14).

**Recomendación.** Juez/Codex: que el ejecutable adquiera y que el orquestador recalcule desde crudos. Ningún campo de estado escrito por el productor debe gobernar.

### 6. ALTA (c): S-20 en modo «temporal»; sin controles de frecuencia entre condiciones

**Evidencia.**
- `collector/telemetry_r16.py:102,114,128` fija `mode="temporal"`.
- `validator/s20_validator.py:747-749` devuelve sin aplicar FREQ_CONDITION_MISMATCH (≤3 %), FREQ_CORE_DISPERSION (≤5 %) ni FREQ_REPLICA_DRIFT (±3 %).
- En modo full, L766 exige condiciones literalmente "A","B","C" (MD/51 D10). El validador es idéntico a r15 (mismo hash).

**Efecto.** Los límites de S-20 que justifican la segmentación de S-24 («deriva evaluada sobre el conjunto», `T0.md:226`) no se aplicarían.

**Recomendación.** Juez: ningún paquete con este adaptador puede presentarse a Sol como cierre de D10.

### 7. MEDIA (c): D11 sin corregir en r16

**Evidencia.**
- `collector/residency_derive_r16.py:440` atribuye L2 solo con la réplica 1.
- L449 exige `capture_pair == ["Q","R"]`, que confunde D8 con el barrido.
- L404 y L411 comprueban la positividad solo de medianas, no de cada marginal (S-19 §5: «marginal no positivo invalida la celda»).
- L457 busca la rodilla desde i=1, dentro del plateau inicial.

Son las mismas objeciones de MD/51 D11, todavía presentes en r16.

**Recomendación.** Juez/Codex.

### 8. MEDIA (c): D12 sigue usando la tolerancia S-12 en el barrido de coordenadas

**Evidencia.**
- `harness/residency_r16.cpp:957-968` valida las 512 coordenadas con `verify_s12`, que tiene cota B.
- S-12 y MD/51 D12 exigen igualdad exacta en ese barrido, con escalas unitarias y salidas enteras (`T0.md:200`; `MD/51.md:158-164`).

**Recomendación.** Juez: reutilizar la QA standalone aceptada (`kernel/qa_main.cpp`).

### 9. MEDIA (b/c, post-hoc declarado): S-22 cambia el criterio caliente tras observar su FAIL

**Evidencia.**
- S-22 (`T0.md:222`) sustituye «LLC caliente ≤0,10 (L3_MISS/offcore)», que había fallado, por H2 y H3|2 con denominador D2. Este denominador es mucho mayor y hace el umbral 0,10 muy laxo: los valores observados son ≤7·10⁻⁵.
- Está declarado como posterior y el FAIL queda registrado. No toca gates de residencia.
- Queda una incoherencia de métricas: el frío conserva OFFCORE y el caliente usa D2. El código r16 mezcla ambos (hallazgo 1).

**Recomendación.** Sol: aceptable como (a) declarado si el informe final lo cita como control requalificado post-hoc. Unificar los denominadores en el código.

### 10. MEDIA (a declarado, debilita un control): S-23 retira INST_RETIRED tras el FAIL de LP0

**Evidencia.**
- S-23 (`T0.md:224`) convierte R1 FAIL en `QUALIFIED_FOR_REQUIRED_CHANNELS_WITH_DECLARED_LIMITATION`.
- La cualificación del lector pierde así su única comprobación independiente de conteo de PMU (S-20 D2).
- No convierte un FAIL científico. P_instr queda respaldado por el sink exacto 672·C·U·R (`harness/provider_aux_r16.cpp:118,135`), así que el «conteo dinámico comprobado» de S-19 §1 sigue cubierto.
- Verificado en código: INST_RETIRED solo es requerido como presencia en el grupo y no entra en ningún gate (`validator/s20_validator.py:34-61,160,1025-1077`).

**Lectura.** El exceso de +1,4 % en LP0 (interrupciones/DPC) también roba tiempo al worker 0. S-23 lo mantiene en los gates sin descontarlo, lo cual es conservador.

**Recomendación.** Ninguna acción adicional. Registrar la limitación en el informe final.

### 11. MEDIA (a declarado): S-24 §3 reduce la duración por muestra en residencia

**Qué cambia.**
- S-20 (`T0.md:218`) exigía 2 s acumulados de kernel por muestra, «sin rellenar con esperas ni expulsiones».
- S-24 exige 2,0–2,5 s de región continua (incluye flush, acondicionamiento y barreras) y solo 100–150 ms de kernel_only.

**Validez estadística.**
- Se conservan N=31, el estimador marginal, la matriz y el control A/A P95 ≤0,05, que falla cerrado. Por tanto no puede convertir un FAIL en PASS.
- Sí sube la varianza por muestra. Con la relación pared/kernel ≈6,9–8,4 del smoke (MD/51:214), el kernel efectivo baja ≈5–7× frente a S-20, y el error típico sube ≈2,2–2,6× si el ruido es blanco (ESTIMACIÓN). Aumenta el riesgo de HOLD por A/A, no el de falsos PASS.

**Lo no registrado.**
- Las ventanas de frecuencia, caché y energía pasan a estar dominadas por flush y acondicionamiento, con una composición distinta entre A (1 flush por episodio) y B/C (K flushes).
- El control de diferencia de frecuencia ≤3 % entre A/B/C compara entonces regiones con mezclas de trabajo distintas. Puede dar HOLD espurio o certificar una frecuencia que no es la del kernel.
- MD/51 lo menciona para caché, pero no para frecuencia.

**Hallazgo nuevo en r16 (trabajo ajeno dentro de la región).** S-24 prohíbe trabajo ajeno en la región, pero r16 hace dentro de ella:
- un hash de salida por aplicación (`harness/residency_r16.cpp:453`);
- formateo JSON por aplicación (L511-513).

**Recomendación.** Sol: decidir si el gate de frecuencia entre condiciones debe calcularse sobre ventanas acotadas al kernel. Juez: retirar el hash y la serialización de la región.

### 12. MEDIA (a declarado + riesgo): segmentación con Chrome y orden de residencia por W

**Evidencia.**
- S-24 §4 sustituye la «única sesión oficial» (`T0.md:184`) por segmentos bajo el mismo session_id. Está declarado (MD/51:260).
- `collector/residency_plan_r16.py:164-174` ordena los bloques W-mayor, con `RESIDENCY_PER_SEGMENT=31`: cada segmento contiene un solo W con sus 31 réplicas.

**Riesgo.**
- La curva g(W), la rodilla y la atribución L2 comparan puntos adquiridos en segmentos y días distintos. La deriva entre segmentos queda confundida con W: térmica, Chrome, reasignación física de páginas grandes, mapeo de bancos y slices.
- Los gates de W=288 (ρ, δBC, S_m, A/A) sí quedan dentro de un mismo segmento, lo que es bueno.
- Intercalar réplicas entre W (réplica-mayor) desacoplaría esta deriva.

**Otro problema del plan.** BW_DRAM y P_instr no aparecen en el plan (grep sin resultados en `residency_plan_r16.py`). S-24 exige BW_DRAM y la serie H-KQ/D′ en el mismo segmento, y el plan no lo codifica.

**Recomendación.** Sol: el orden de bloques. Juez: incluir las unidades auxiliares en el plan.

### 13. MEDIA (c): endurecimiento en la cadena y conclusiones con etiqueta física

**Evidencia.**
- S-2 (`T0.md:186`): D′<2 ⇒ HOLD (`KERNEL_COMPETENT_BUT_INSUFFICIENT_HEADROOM`).
- S-7 (`T0.md:196`): G_Torch<1,20 ⇒ HOLD.
- S-19 (`T0.md:216`) las sustituye: cualquier fallo ⇒ `CLOSED_Q4_256_THIS_I7`.

**Lectura.** Está declarado («sustituye las disposiciones incompatibles») y es más estricto, no más laxo. Pero un criterio de aceptación de ingeniería (superar en un 20 % a PyTorch FP32) puede cerrar la línea sin medir residencia. El código lo hace sin indicar qué gate cerró (`collector/qualify_r16.py:519-524` pone `campaign=CLOSED_Q4_256_THIS_I7` para cualquier gate).

**Recomendación.** Sol: que el informe final distinga «cerrada por competencia frente a PyTorch» de «cerrada por H-KQ/D′ (margen físico)».

### 14. MEDIA (b, declarado): S-20 B acepta PL1/PL2 ilegibles como limitación

**Evidencia.** S-19 §2 (`T0.md:216`) decía: si PL1/PL2 no se leen con interfaz cualificada ⇒ HOLD, y «se elimina la salida permisiva UNAVAILABLE». S-20 B (`T0.md:218`) acepta `NOT_READABLE_WITH_AVAILABLE_INTERFACE` como limitación declarada, después de sondear el instrumento.

**Lectura.** Es relajación de un control instrumental, no científico, y está declarada.

**Recomendación.** Ninguna; citarla en el informe final.

### 15. BAJA (c): B expulsa cada copia justo antes de su aplicación

**Evidencia.** `harness/residency_r16.cpp:434` expulsa la copia de cada aplicación inmediatamente antes de usarla. S-19 §3 dice: «B: expulsar las K copias antes del episodio… sin flush añadido».

**Lectura.** El estado frío resultante es equivalente y el flush queda fuera de kernel_only, pero el texto y el código divergen.

**Recomendación.** Sol ratifica o el juez alinea.

### 16. BAJA (a, reinterpretación de mi B9): prefetch software desactivado para todos

**Evidencia.** S-19 §6 (B8/B9) desactiva el prefetch software para todas las variantes, en lugar de darle a B su mejor versión (mi D.4(c)).

**Sesgo residual.** Va a favor de A. Su magnitud está acotada por η_BW (≤3–4 %) si B está limitado por DRAM. No es decisivo salvo con ρ en la frontera de 0,50.

**Recomendación.** Sol: registrarlo como sesgo conocido.

### 17. BAJA (c): reintento y códigos de salida sin tipar

**Evidencia.**
- `collector/qualify_r16.py:982` marca `retry_remaining` para cualquier HOLD, aunque sus motivos no sean reintentables (páginas, afinidad, plateau).
- `RETRYABLE_REASONS` está definido pero no se usa (`collector/official_run_r16.py:24`).
- `qualify_r16.py` main devuelve 0 también con HOLD (L1003-1008), contra S-13 E5.

**Recomendación.** Juez.

### 18. BAJA (c): mapa de LP fijo en la expulsión paralela

**Evidencia.** `evict/evict.cpp:53-54` fija LP = 2·w, sin pasar por el mapa de `topology_r16` (D7: «el mapa debe alimentar a todos los productores»).

**Recomendación.** Juez.

### 19. BAJA (c, riesgo de pérdida de contexto): documentación desfasada

**Evidencia.**
- `harness/HARNESS_SPEC_r16.md:28` es una copia renombrada de r15 y dice «R se duplica hasta… 2.5 s agregados de kernel». Contradice S-24 y el código r16.
- `collector/COLLECTOR_SPEC_r16.md` también es igual a r15 salvo el nombre.
- `ESTADO_PROYECTO.md` (raíz) está fechado el 2026-10-11 y dice «opencode retirado / Codex».
- `Roadmap.md:412` resume T0′ sin G_Torch, H-KQ, cierre S-19 ni segmentación. T0′ no tiene fila en el tablero, y no está definido qué habilita un RESIDENCIA_PASS.

**Recomendación.** Juez: actualizar. Sol: asignar fase y el efecto de un PASS sobre T0.2.

### 20. BAJA (a): S-3 elige m=1 como gobernante sabiendo ya que H0 dio ρ₄≈0,99

**Evidencia.** S-3 fija m=1 como gobernante. El Roadmap §6.1 dice «m=1 o 4» (`Roadmap.md:345`).

**Lectura.** Está permitido por el texto literal y es prospectivo, pero es una elección hecha a la vista de H0 (mi D.4 lo advertía).

**Recomendación.** Registrarlo en el informe final.

---

## Cobertura de B1–B7 (verificado en fuentes, no en STATUS)

| Defecto | Estado | Evidencia |
|---|---|---|
| **B1** Kernel lento | Cerrado en diseño (`VPDPBUSD`, activaciones preparadas una vez, escalado FMA): `kernel/q4_kernel.cpp:26-93,169-185`. | El rendimiento (H-KQ ≥33,1) NO está verificado: no hay cifras de ingeniería en disco. |
| **B2** Gate `D_read≥1` | Cerrado: D′≥2,00 y H-KQ en S-2/S-19, y en código. | `collector/qualify_r16.py:463-510` usa la misma serie 8P/m1/prepared. |
| **B3** Coste amortizado | Cerrado: marginal (T16−T1)/15 por réplica. | `collector/residency_derive_r16.py:395-401`. La positividad por marginal sigue abierta (hallazgo 7). |
| **B4** Cuantización QPC | Cerrado: TSC con fences por worker. | `harness/residency_r16.cpp:450-452`, `t_app = max e − min s` en L504-509. |
| **B5** Expulsión de X/Y | Cerrado en r16: solo pesos; acondicionamiento idéntico de X/Y. | `harness/residency_r16.cpp:433-446, 533-536`. La regresión r15 está corregida. Ver hallazgo 15 sobre el momento de expulsión de B. |
| **B6** Expulsión LLC incompleta | Método cerrado (CLFLUSHOPT sobre todas las líneas de pesos, `evict/evict.cpp:34-37`). | El control final r16 está INVERTIDO (hallazgo 1), así que la revalidación de fin de campaña no protege. |
| **B7** Frecuencia | Parcial: instrumento THREAD/REF_TSC y validador existen. | En r16 los controles entre condiciones no se aplican (modo temporal, hallazgo 6). El riesgo de C-states (hallazgo 3) no está tratado. |

---

## Lo que no pude comprobar (NO VERIFICADO)

- Si existen residencias C6 o halts entre aplicaciones en el smoke. Los crudos EMON quedaron fuera de alcance por la restricción de memoria.
- El rendimiento real del kernel frente a H-KQ, G_ggml y G_Torch.
- Si `prepare_activations` asigna memoria por llamada en la frontera dinámica de OMEGA.
- La implementación de los 180 s de acondicionamiento por segmento y la reexpulsión al inicio y al cierre de cada segmento: el plan solo lo declara como metadato (`residency_plan_r16.py:288`).
