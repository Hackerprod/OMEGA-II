# Harness T0.0-d/e/f/i dinámica — mediciones de etapa 2

Autorización del juez recibida; ejecución secuencial capturada en `stage2_raw/` y `logs/COMMANDS.json`. Orden oficial: e, d, f, i dinámica P/E/MIXED.

N=31; warmup=5; mediana; IQR+MAD; cero descarte de outliers; KERNEL_CALLS_PER_SAMPLE=128. BW_D usa n={1,2,4,8,16} y 125,829,120 B por worker. Dinámica: 120 s con P=8, E=8, MIXED=16.

Los CSV crudos por repetición y resúmenes conservan los valores medidos. `BW_L2` y `r` representan bytes lógicos Q4 y NO se declaran tráfico físico validado.

Cuantizador float→INT8 sigue sin especificación; correctness y d/e/f usan entradas INT8+escalas ya formadas. El contador PDH Processor Frequency fue invariante a 2100 MHz en todas las muestras; revisar su interpretación antes de atribuirlo a frecuencia efectiva. La propiedad `ExitCode` del proceso controlado quedó nula en JSON, aunque cada wrapper finalizó con 0 y se capturó el footer `dynamic_load_complete`; no se repitieron cargas.
