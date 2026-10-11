# Auditoría de contabilidad: `v_i` frente a `BW_L2,i`

## Resultado

No hay un factor 2 de bytes entre las dos series al usar las definiciones que implementa el harness. La expectativa de 1.26 GB/s usa `0.5625 B` por cada unidad que el código denomina “MAC”; ese cociente corresponde a bytes Q4 por **peso** o por MAC convencional, mientras que el contador de `v_i` duplica el número convencional de MACs (`2*O*I`). El valor medido llamado `MAC_per_second` es, por su fórmula, operaciones de multiplicación más suma por segundo (FLOP/s), no MAC/s convencional.

## Fórmulas / parámetros idénticos

- Forma usada por `makeWorker`: `O=512, I=512, m=1`, AVX2; Q4 persistente con `512*(512/32)*18 = 147,456 B` de payload por matriz.
- En `writeRawSeries`, ambos caminos ejecutan `KERNEL_CALLS_PER_SAMPLE=128` llamadas Q4. `doWork` establece `bytes=payload_bytes*calls=18,874,368 B` y `macs=2*O*I*calls=67,108,864` unidades contadas por muestra.
- `v_i` (`--run-d`) pasa `byteMetric=false`: `amount=macs`; cada `d_vi_*_raw.csv` registra esos dos acumulados y el `group_elapsed_ns` de una serie de un solo núcleo.
- `BW_L2,i` (`--run-f`) pasa `byteMetric=true`: `amount=bytes`, con el mismo shape, `m=1`, ruta AVX2 y 128 llamadas; cada `f_bw_l2_i_*_raw.csv` confirma `macs=67,108,864` y `logical_bytes=18,874,368` por muestra. El throughput se divide por el tiempo de pared de su muestra.
- Un peso representa `18/32=0.5625 B`. La cuenta del harness representa dos operaciones por peso: `2*512*512`. Por tanto `bytes / valor_contado = 0.28125 B`; `2.24e9 * 0.28125 ≈ 0.63e9 B/s`, coherente con los `BW_L2,i` P observados (~0.63 GB/s).
- En términos de MAC convencional, la llamada hace `O*I=262,144 MAC`; 128 llamadas hacen `33,554,432 MAC`, aproximadamente la mitad del campo `macs`. El throughput convencional P sería ~`1.12e9 MAC/s`; multiplicado por `0.5625 B/MAC` vuelve a ~`0.63 GB/s`.

## Evidencia cruda y límites

Por ejemplo, en la repetición 0 del núcleo `CORE_API_00_G0_MASK_0_1`, ambos CSV crudos registran `logical_bytes=18,874,368` y `macs=67,108,864`; los tiempos son ~31.94 ms (`d_vi`) y ~31.09 ms (`BW_L2,i`) en esas muestras separadas. Las medianas separadas no tienen que multiplicarse exactamente entre sí; la relación esperada es la cuenta lógica por unidad, no igualdad entre mediciones independientes.

Conclusión: la diferencia de factor 2 de la hipótesis inicial es contabilidad/nomenclatura del numerador `macs`, no una configuración distinta de shape o llamadas ni un error en los bytes de `BW_L2,i`. `BW_L2,i` cuenta bytes **lógicos leídos por el kernel**; no prueba transferencias físicas desde L2. No se modificaron resultados ni se repitió ninguna serie.
