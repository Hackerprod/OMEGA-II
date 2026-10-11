# T0.0-i dinámica — observaciones PDH y estado de frecuencia efectiva

Carga Q4 AVX2 controlada ya realizada: 120 s por condición; workers P=8, E=8, MIXED=16. Estas filas conservan solo la lectura cruda/descriptiva de PDH.
**PDH nominal constante, NO frecuencia efectiva:** CookedValue devolvió exactamente 2100 MHz en todas las instancias y muestras. No interpretar como frecuencia efectiva, frecuencia sostenida ni resultado por núcleo; no se repitió la carga.
Estado gobernante tras el intento único SEP/VTune de MD/28: `EFFECTIVE_FREQUENCY=UNAVAILABLE_WITH_QUALIFIED_TOOLS`; detalles por target, eventos y razones en `phase1_raw/effective_frequency_attempt.json`. No se infiere frecuencia desde throughput ni desde PDH.

| Conjunto de carga | Tipo núcleo | Muestras | Mediana MHz | Mín. MHz | Máx. MHz |
|---|---|---:|---:|---:|---:|
| P | E-core | 960 | 2100.000 | 2100.000 | 2100.000 |
| P | P-core | 1920 | 2100.000 | 2100.000 | 2100.000 |
| E | E-core | 960 | 2100.000 | 2100.000 | 2100.000 |
| E | P-core | 1920 | 2100.000 | 2100.000 | 2100.000 |
| MIXED | E-core | 960 | 2100.000 | 2100.000 | 2100.000 |
| MIXED | P-core | 1920 | 2100.000 | 2100.000 | 2100.000 |

TEMPERATURE_TELEMETRY=UNAVAILABLE; POWER_TELEMETRY=UNAVAILABLE; PL1/PL2=UNAVAILABLE_WITH_ALLOWED_INTERFACES.
Instancias PDH ppm_processor_N se enlazaron por sufijo N al logical_processor N de b_topologia.csv.
Los CSV crudos PDH y los registros por condición conservan muestras, comando, worker count y exit code.
El JSON de Start-Process dejó process_exit_code=null en las tres condiciones; los wrappers PowerShell terminaron con 0 y cada stdout nativo incluye dynamic_load_complete.
