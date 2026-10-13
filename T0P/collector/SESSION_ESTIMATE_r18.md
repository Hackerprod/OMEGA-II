# Estimación de sesión R18

Fecha: 2026-10-13. Todos los tiempos de campaña suponen Chrome cerrado. Es planificación; ningún smoke es adjudicable y no se abrió sesión oficial.

## Medidas R18

| Medición | Resultado ejecutado |
|---|---|
| P1 cadena N=3 | 4.140,863 s (69,014 min) desde QPC del padre; 142.301.061 B EMON; 264 eventos; último `residency|architecture|C|m16|K16|W288|O1024|r3`; `emon -stop`, stop=0, EMON=0 |
| P1 verificación/replay compacto | PASS; 264/264 eventos, 3 segmentos, 408 márgenes, 1.045 marcas de carga; intento de evidencia 242.298.030 B |
| P1 postproceso FULL | 20,9 min medidos para validar 113.356 filas de clock map y 68.766 intervalos; salida S-20 `INVALID` por cobertura/canales/energía/térmica del corte SMOKE |
| P2 S-25-D | 186 sondas; región de aplicación 4,7498–4,8660 s (mediana 4,7826 s); overhead medio 0,0965 %, p99 0,5076 %, máximo 0,6295 % frente al límite 1 %; binario `00eb6b90c74cd443cc3ccb053c1aeca0c14c8aa2c6e384b18563651378a67ec2` |
| P5 S-25-B N=1 | 1.287,509 s (21,458 min); 44.263.800 B EMON; 6 eventos y un segmento; stop=0, EMON=0; veredicto real HOLD porque S-20 temporal fue INVALID. S22 496/496 y probes 62/62 pasaron. |

La cadena N=3 tuvo presupuesto calculado de 5.167,8 s máximo y 575.124.275 B EMON estimados. El postproceso se mide aparte: el presupuesto de captura no debe cancelar la validación que sigue a un stop EMON confirmado. El replay final serializa resúmenes por ventana después de validar todas las ventanas; así queda debajo de 1 GiB.

## Estimación de campaña completa

| Componente | Cuenta y piso | Banda Chrome cerrado |
|---|---|---:|
| Cualificación | 18.414 eventos × 2,5 s = 12,79 h | 14,7–16,0 h con 15–25 % de margen |
| Residencia N=31 | 13.020 eventos × 2,5 s = 9,04 h | La cuenta completa por segmento se desglosa abajo |
| Acondicionamiento | 31 × 180 s = 1,55 h; medido en P1: mínimo 181,721 s por segmento | Incluido abajo |
| S-25 inicio/cierre | 31 × 62 sondas × 4,7826 s medianos = 2,55 h | Incluido abajo |
| Calibración `R(W,m,K)` | 420 eventos × 2,5 s = 17,5 min, más duplicaciones hasta alcanzar el objetivo | Incluido abajo; puede crecer |
| Controles A/A | 248 eventos × 2,5 s = 10,3 min | Incluido abajo |
| Prueba S-25-B FULL | 36 bloques de 60–75,4 s, 12 calibraciones, 3 × 180 s de acondicionamiento y probes medidas | 1,5–2,0 h; presupuesto EMON de seguridad hasta 2,37 h. No ejecutada |
| Gate, persistencia y S-20 | QA de gate observado 12,2 min; postproceso P1 20,9 min | 0,5–1,0 h |

Para cada uno de los 31 segmentos gobernantes: 420 residencias (17,5 min de piso), 180 s de acondicionamiento, 62 probes de apertura/cierre (4,94 min medidos), calibración prorrateada (0,57 min de piso) y A/A prorrateado (0,33 min). **Piso: 26,3 min por segmento; banda con calibración y variación operativa: 30–33 min.** El smoke N=3 midió 23,0 min por segmento de diagnóstico (69,014 min / 3); sus 60 eventos de residencia por segmento son una fracción de los 420 de campaña, por lo que no se extrapola como coste completo.

La banda total es **33–37 h con Chrome cerrado**, antes de pausas autorizadas entre réplicas. Incluye 18.414 eventos de cualificación, los 31 segmentos de residencia, S-25-B FULL y postproceso. Supone regiones mínimas de 2,5 s donde el plan no exige ventanas de 60–75,4 s y añade 15–25 % para duplicaciones de R, EMON y persistencia. No incluye esperas manuales.

## Evidencia y límites

- P1 y P5 usaron QPC del proceso padre, EMON lector oficial y cierre controlado. El smoke P5 N=1 es integración; el schedule FULL de 36 bloques se reserva al juez.
- El S-20 FULL de P1 se ejecutó desde crudos con configuración y lineage SHA-256. Su INVALID es trazable al corte smoke; no se convirtió en PASS ni se relajaron límites.
- Los estimates por evento son pisos. Los intervalos reales de calibración y las bandas pueden subir el total; recalcular antes de cada campaña desde schedule y tasa EMON observada.
