# OMEGA - ESTADO DEL PROYECTO (leer primero tras un reinicio de contexto)

Actualizado: 2026-10-11 (tras sesión oficial H0 e inspección Opus de T0.1).

## 1. Situacion
Reinicio COMPLETO de OMEGA desde T0 (usuario, 2026-10-09). Hardware: i7-13700F + GTX 1650 SUPER 4 GiB (solo validación), Windows 11, disco C:. Contrato permanente: d=512, nucleo 16 d^2 = 4,194,304 pesos, cero evidencia de modelos sustitutos/reducidos.
Fuentes normativas: `Roadmap.md` (tablero de 16 fases), `T0.md` (spec unica; correcciones en el MISMO archivo; registro en §9), `Conversacion.md` (verdad cientifica, escrita para otro hardware), `MD\N.md` (respuestas de Sol; vigentes 22..32).
Roles: el JUEZ (Claude) lidera y puede rechazar ordenes desviadas de Sol; Sol = arquitecto cientifico; **Codex (gpt-6-luna) = desarrollo** (opencode retirado; el shell de Codex fallo en la ultima tarea y el juez escribio `src/h0_derive.py`); Opus solo para fidelidad del Roadmap/validar T0.md/inspeccionar fases y T. Skill del juez: `~/.claude/skills/omega-judge/SKILL.md`.

## 2. Estado de fases
- T0.0 = INSPECCIONADA_POR_OPUS (Completed 2026-10-11).
- T0.1 / H0 = VERIFICADA_POR_JUEZ con veredicto del juez **H0_FAIL FIRME (Sol, MD/34)** (sesion oficial `h0_official_20261011b`, 47 bloques, medicion valida: R_noise 0.0128, expulsion 8 MiB PASS, delta_BC 0.0006/0.009). Gates: R1 FAIL (rho_1=0.913), R4 FAIL (rho_4=0.994), M PASS (S4=1.762), K = PASS literal historico / NOT_ESTABLISHED cientifico (artefacto por no normalizar por byte, MD/34 D-B). Inspeccion Opus: `MD\OPUS_T0_1_INSPECCION.md` = DESVIACIONES (cifras CONFORMES; FAIL se sostiene tambien con el criterio original de Conversacion.md). Adjudicado por Sol en MD/34.
- **Adjudicación de Sol (MD/34, nueva sesión OMEGA): H0_FAIL FIRME** (→ FALLIDA_LINEA_MUERTA_PHYSICAL_PREMISE, limitada al kernel/configuración/protocolo examinados; sin repetición para rescatarlo). H0-K científico = NOT_ESTABLISHED (C_L2_knee/B_L2_operational = null). Desviaciones científicas de MD/29 vs Roadmap §6.1 reconocidas y registradas (Roadmap §6.1/§7). Correcciones documentales hechas (T0.md §4.1/§9, Roadmap, `h0_corrections.py/json`); pendiente reinspección Opus → Completed (cierre operativo, no habilita H1).
- T0.2 y T0.3 = NOT_STARTED (H0_FAIL). T1–T4 no habilitadas por esta campaña. Cualquier investigación futura necesita hipótesis y protocolo nuevos y prospectivos.
Datos: `T0\results\T0.1\h0_derived_h0_official_20261011b.json`, crudos en `h0_raw\h0_official_20261011b\`. El primer lanzamiento `h0_official_20261011` abort en preflight (bug de comillas WMI en el runner, sin series ni datos); runner corregido y relanzado como `...b`.

- **Campaña T0′ `T0P-Q4-RESIDENCY` APROBADA por Sol (MD/35, 2026-10-11)**, EN_PREPARACIÓN: criterios en `T0.md §10`; pasos: (1) congelar vara externa ggml Q4_0 (+ PyTorch si Sol lo aprueba, S-7), (2) kernel propio nuevo, (3) cualificación (≥90 % y D′≥2), (4) protocolo A/B/C ejecutable → revisión de código por Sol → congelar → sesión única. Sin medición oficial hasta entonces.

## 3. Reglas vigentes
Cero desviacion (ambiguedades a Sol ANTES de medir; el juez nunca ratifica ciencia); hashes sobre bytes exactos; raiz limpia; un solo MD por spec; no D:; Spanish only; el juez cierra/reabre Chrome (`--restore-last-session`) sin pedirlo; antes de reportar a Sol: commit (y push a GitHub cuando exista remoto; hoy `origin` es una ruta local) y decir a Sol que lea el repo local con sus herramientas (`C:\Projects\OMEGA\Ejercise`).

## 4. Siguiente paso
1. Reportar a Sol (rutas locales): resultados H0, inspeccion Opus, 3 decisiones (veredicto H0; H0-K sin normalizar; umbrales relajados vs Roadmap §6.1).
2. Segun Sol: cerrar T0.1 (Completed) o repetir/corregir; documentales de Opus (hash Roadmap en T0.md, §9, plan JSON).
3. Conectar remoto GitHub (pendiente URL del usuario).
