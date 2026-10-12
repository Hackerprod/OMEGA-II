# Collector R13

`qualify_r13.py` es la copia R13 del cualificador. Valida el SHA-256 canónico del plan, JSON estricto, cobertura de réplicas, trabajo MAC según la geometría y telemetría S-20. Las muestras con etiqueta `SMOKE` solo verifican rutas y no producen gates de rendimiento.

El manifiesto `SHA256SUMS_collector_r13.txt` cubre `qualify_r13.py`, esta especificación y `hashes_r13.ps1`. Se genera y verifica con:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File T0P\collector\hashes_r13.ps1
python -B T0P\collector\qualify_r13.py --self-test
```

Los resultados r13 se guardan bajo `T0P\harness\logs_r13\obj_r13`; no se modifican los resultados oficiales ni el plan congelado.