# Especificación del cualificador T0P R10

## Entrada y ejecución única

`qualify_r10.py` es la única entrada del cualificador. Consume directamente el `raw.jsonl`, `summary.jsonl` y `plan.json` de T0P, valida JSON estricto (claves únicas, RFC 8259, UTF-8, números finitos, sin bytes finales) y verifica el SHA-256 canónico del plan antes de leer resultados. No regenera ni reescribe el plan.

```powershell
python T0P\collector\qualify_r10.py --raw <captura>\raw.jsonl --summary <captura>\summary.jsonl --plan T0P\collector\plan.json --output T0P\collector\logs_r10\qualification.json
```

Los datos `SMOKE` producen `SMOKE_ONLY`, `numeric_metrics_used=false` y `RESIDENCY=NOT_STARTED`; sus cifras nunca alimentan los umbrales. Para datos `OFFICIAL`, solo se usan filas `VALID` con S-20 estimable, afinidad efectiva correcta y páginas grandes donde el proveedor las requiere. Frecuencia TSC frente a EMON ≤3 %, frecuencia activa y potencia positivas, flags térmicos actuales apagados, estado térmico presente y margen TCC ≥5 °C son condiciones de instrumento.

## Selección y criterios congelados

La vara de referencia se selecciona dentro de cada unidad (frontera, m, P-core/8P) usando exclusivamente su propia mediana de tiempo entre variantes elegibles; los empates se resuelven por identificador ASCII. OMEGA no participa en la selección. El gate se expresa como razón de rendimiento, equivalente a `mediana(t_OMEGA)/mediana(t_referencia)`:

- `G_ggml ≥0,90` para ggml flat/repack.
- `G_Torch ≥1,20` entre `mm`, `matmul`, `linear` y `mv` donde corresponda.
- Ambas fronteras MD/35, `m∈{1,4,8,16}`, cada P-core y 8P; no se compensan celdas.
- `H-KQ`: para OMEGA 8P y `m=1,8`, mediana agregada y mínimo entre medianas por worker `≥33,1 MAC/ns`.
- `D′_hot=0,5625·P_Ω,8P,m=1/BW_DRAM,8P ≥2,00`; BW usa 256 MiB por worker en 8P, lectura load-only y cuatro cargas vectoriales de 256 bits.
- `P_instr`: U=8 y cadenas `{1,2,4,8,10,12,14}`. `δ12,14=|P12−P14|/((P12+P14)/2)≤3 %`.

Las filas sin `VALID` no entran en medianas ni razones. Un dato inválido de EMON/frecuencia/potencia/térmica, afinidad, página o cobertura detiene la cualificación en `HOLD_INSTRUMENTATION`. El runner permite un único reintento global con el mismo plan, semilla y orden; el segundo fallo cierra en HOLD. Con instrumento válido, un umbral incumplido devuelve `FALLIDA_CUALIFICACION`, `CLOSED_Q4_256_THIS_I7`, `RESIDENCY=NOT_STARTED`. Plateau inválido devuelve `INVALID_IMPLEMENTATION` y también corta antes de residencia.

## Proveedores auxiliares r10

El harness emite `BW_DRAM` y `P_instr` como proveedores nativos, fijados a P-cores. BW exige large pages verificadas para 256 MiB por worker ×8. P_instr ejecuta VPDPBUSD con U=8 y cadenas requeridas. El smoke N=3 enlaza estos proveedores, los cuatro proveedores de competencia, EMON y la derivación de telemetría; la salida queda etiquetada `SMOKE`.

## Evidencia y límites de uso

`--self-test` cubre PASS y cada frontera de gate sobre una matriz oficial sintética completa, H-KQ por mediana y mínimo de worker, plateau, telemetría inválida, segundo intento inválido, cobertura de un P-core/8P, selección ASCII independiente de OMEGA y negativas del parser. La matriz oficial y los proveedores no se han medido en sesión oficial en este r10. No se creó `AUTHORIZED.json`, no se inicia residencia ni se interpretan métricas SMOKE como resultados científicos. La integración de `AUTHORIZED.json` permanece cerrada.
