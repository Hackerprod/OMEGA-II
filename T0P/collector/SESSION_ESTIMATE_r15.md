# Estimación de duración de la sesión R15

Actualizada el 2026-10-12 con el smoke R15 completo `smoke_r15_privilege_probe_20261012`. Es una estimación de planificación; no usa los valores de SMOKE para adjudicar la línea.

## Mediciones del smoke

- Harness: 18 eventos; 3 calibraciones para un R común `R=16,384` (`W=288, O=1024, m=1, K=16`), 9 observaciones A/B/C y 6 observaciones A/A. Marcador del harness: `SMOKE_R15_CAPTURE PASS`; D9: `PASS`.
- Tiempos QPC en los eventos: residencia, 553.981499 s / 9 = 61.553500 s por observación; A/A, 172.709613 s / 6 = 28.784936 s por observación.
- Búsqueda de R: 98,301 filas de prueba (32,767 por variante A/B/C); intervalo QPC conjunto = 365.927874 s para una unidad `W,m,K`, cuyo R común fue 16,384.
- EMON validó 144,000 grupos y escribió el footer `2401.300s real 355.284s user 2046.012s system`. S-20 resultó `INVALID`, razón `FREQ_WINDOW_EXCURSION` (120 muestras; 35,243 filas de reloj); por ello el SMOKE no es adjudicable y `numeric_metrics_used=false`.
- Observación de proceso: EMON inició a las 13:28:56 y cerró a las 15:56:57 (≈2.467 h), discrepante con su footer de 2,401.300 s (0.667 h). Conservo ambos valores. Para las cuentas por evento uso el footer EMON y QPC; agrego una provisión de 1.800 h por sesión, correspondiente a esa diferencia observada, sin suponer que el proceso/runner repetirá el mismo retraso.
- QA del kernel R15: 2,048 coordenadas (512 × m=1,4,8,16) más 60 casos de barrido en 31.021 s. QA se añade una sola vez.

## Conteos del plan corregido

- Cualificación G_ggml/G_Torch/H-KQ/D′/plateau: 25,668 IDs de evento (31 réplicas según el plan).
- Residencia: 420 condiciones × 31 réplicas = 13,020 observaciones, cubriendo las 420 condiciones A/B/C y el R común requerido.
- Búsqueda/calibración `R(W,m,K)`: 140 unidades comunes × tres variantes = 420 eventos.
- A/A: 124 pares × dos observaciones = 248 eventos.
- Total de eventos de residencia, R y A/A: 13,688.
- Márgenes: dos por cada una de las 13,020 observaciones de residencia = 26,040 ventanas, cada una ≥0.200 s con carga continua y misma condición. Ya están incluidos en el tiempo integral medido por evento; no se vuelven a sumar.
- D8 posterior, preflight/lector, cierre y revalidación: provisión fija de 1.0 h. D8 y márgenes no se convierten en una métrica científica sin controles válidos.

## Cuentas explícitas

| Componente | Cuenta | Horas |
|---|---|---:|
| Residencia A/B/C | 553.981499 / 9 × 13,020 / 3,600 | 222.618 |
| Búsqueda R común | 365.927874 × 140 / 3,600 | 14.231 |
| A/A | 172.709613 / 6 × 248 / 3,600 | 1.983 |
| EMON/setup/transiciones/márgenes/cierre no atribuibles al kernel | (2,401.300 − 553.981499 − 172.709613 − 365.927874) × (13,688 / 18) / 3,600 | 276.439 |
| QA | 31.021 / 3,600 | 0.009 |
| Provisión preflight, lector, D8, cierre | supuesto fijo | 1.000 |
| Diferencia observada proceso-footer | (8,881 − 2,401.300) / 3,600, aplicada una vez | 1.800 |

No se midió una corrida de cualificación de 25,668 eventos. Para esa parte uso tres escenarios: mínimo 2.5 s/ID (17.825 h); base al promedio de residencia medido de 61.553500 s/ID (438.876 h); y alto 120 s/ID (855.600 h). La provisión residual se reparte por los 13,688 eventos porque el smoke no permite separar causalmente márgenes, transiciones y captura.

| Escenario | Cualificación | Duración estimada | Chrome cerrado |
|---|---:|---:|---:|
| Bajo | 2.5 s/ID | 535.9 h | 535.9 h |
| Base | 61.553500 s/ID | 957.0 h | 957.0 h |
| Alto (wall conservadora) | 120 s/ID; residuo por tiempo de proceso observado | 2,740.6 h | 2,740.6 h |

Para el escenario alto, el residuo se calcula con el tiempo de proceso observado (8,881 s menos los 1,092.619 s QPC de eventos) y se escala por 13,688/18; se usa ese valor de pared en lugar del footer EMON. Bajo y base usan el footer de 2,401.300 s y añaden una sola vez la discrepancia de 1.800 h. Esta sensibilidad amplia preserva la diferencia aún no explicada entre el footer y el tiempo de vida del proceso.

Son escenarios de ingeniería, no intervalos de confianza. La cualificación no se cronometró; el smoke usó una sola unidad R(W,m,K), N=3 y S-20 detectó una excursión de frecuencia. El escalado supone que las duraciones por evento representan otras condiciones y que el residuo se repite por evento; ambas hipótesis requieren validación en una cualificación autorizada. La anterior estimación de 116 h queda retirada. Mantén Chrome cerrado durante toda la sesión prevista, desde preflight hasta cierre.
