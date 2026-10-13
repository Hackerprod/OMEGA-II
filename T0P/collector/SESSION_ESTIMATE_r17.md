# Estimación de sesión R17

Fecha: 2026-10-12. Estimación para una campaña completa, con Chrome cerrado. Es presupuesto de planificación, no un PASS de adquisición: el smoke N=3 quedó en HOLD antes de la primera muestra de residencia y el S25-B FULL no se ejecutó.

## Medidas observadas en smokes R17

| Captura | Tiempo | EMON crudo | Tasa observada | Estado |
|---|---:|---:|---:|---|
| Cadena N=3 | 1.307,234 s (21,79 min) | 44.518.297 B | 34,06 kB/s | HOLD en R01/BEGIN, muestra 1 |
| S25-B N=1 | ≈679 s (≈11,3 min observados) | 23.645.815 B | ≈34,82 kB/s | HOLD; intento eliminado por el runner |

Se usa 34–35 kB/s como banda observada y 40 kB/s para presupuesto conservador de EMON. Cada captura física medida fue <1 GiB; el intento N=3 ocupó 89 MB al revisarlo, con 44,5 MB de salida EMON. C: tenía 25.621.712.896 B libres en la última comprobación. La cadena N=3 gastó 346,1 s en QA de kernel/instrumentación/expulsión y 385,1 s en cualificación `SMOKE_ONLY`; no son tiempos de cualificación científica FULL.

## Presupuesto de campaña

Los tiempos objetivo siguientes usan 2,5 s por evento como piso de región cuando el schedule la exige. Las calibraciones duplican R hasta cumplir región y kernel mínimo; como no se adquirió ninguna calibración de residencia antes del HOLD, su coste real puede superar el piso. El rango añade 15–25% de margen operacional; no incluye pausas manuales entre réplicas.

| Componente | Cuenta | Piso | Supuesto/rango |
|---|---:|---:|---|
| Adquisición de cualificación | 18.414 eventos × 2,5 s | 12,79 h | 2,5 s/evento; el smoke N=3 solo midió 6,42 min en modo diagnóstico |
| Residencia N=31 | 13.020 eventos × 2,5 s | 9,04 h | 420 celdas por cada una de 31 réplicas |
| Calibración R(W,m,K) | 420 eventos × 2,5 s | 17,5 min | 140 R-units; A/B/C comparten R; duplicaciones no medidas aumentan el tiempo |
| Control A/A | 248 eventos × 2,5 s | 10,3 min | 124 pares completos |
| Acondicionamiento de residencia | 31 × 180 s | 1,55 h | Una vez por réplica completa |
| Sonda S25-A, apertura y cierre | 31 × 2 × 31 × (2,5 s + 0,4 s de márgenes) | 1,55 h | 1.922 bloques; target 2,5 s, mínimo 2,0 s |
| S25-B FULL, tres segmentos | 36 bloques ≥60 s; objetivo 75,4 s, 12 calibraciones, 3 × 180 s, sondas y 45 s de reserva | 1,11–1,32 h | Se reserva al juez; no se ejecutó FULL |
| Preflight, QA e inicio EMON | N=3 medido | 5,8 min | Incluye QA; el preflight QPC aislado fue <1 ms |
| Calentamiento inicial | Plan | 5 s | No incluye la cualificación de cada fase |
| Persistencia/hash/postproceso | Sin muestra completa | 15–45 min | Supuesto de planificación, fuera de la región medida; no observado en una campaña completa |
| Tiempo ocioso planificado | Schedule | 0 min | No hay relleno; una pausa autorizada entre réplicas se añade aparte |

Piso sumado: aproximadamente 26,9–27,6 h de Chrome cerrado antes de pausas, según objetivo/minimum S25-B y persistencia. Con 15–25% de variación para calibración, adquisición y persistencia: **31–35 h**. La banda no sustituye el recálculo cuando se conozca R y el coste de S-20 FULL.

## Tiempo por segmento y presupuesto EMON

Para cada segmento de residencia completo (una réplica con los 16 W indivisibles): 420 eventos de residencia = 17,5 min; acondicionamiento = 3 min; 62 sondas S25-A = 3,0 min; A/A prorrateado = 0,33 min; calibración prorrateada = 0,57 min. Estimación: **≈24,4 min (0,41 h) por segmento R**, más cualquier pausa entre réplicas. Las 31 réplicas consumen alrededor de 12,6 h entre esas cuentas.

Al ritmo conservador de 40 kB/s, una ventana de 24,4 min produce ≈58,6 MB de EMON; se presupuesta 100 MB por segmento (<1 GiB). El cálculo real se regenera desde cada schedule antes del smoke y termina la captura con `emon -stop` tras el margen final; `-l` es solamente el límite de seguridad. La prueba S25-B FULL aporta ≈1,1–1,3 h adicionales repartidas en tres segmentos (≈22–27 min cada uno, según 60/75,4 s por bloque).

## Limitaciones medidas

El error detectado en S25-A fue de asociación de marcas: el código anterior comparaba la región de 4,780637 s con las marcas de la sonda de margen de 0,596 s, dando overhead 7,014149. El código R17 captura ahora las marcas antes del margen final y pasó verificación de orden fuente, build, QA enlazado y desensamblado. No se repitió captura física después del arreglo; los tiempos de sonda y los presupuestos por segmento siguen siendo estimaciones hasta una adquisición nueva autorizada.
