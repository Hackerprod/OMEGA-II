# Estimación de sesión oficial R11

- Duración total estimada: **116.15 h** (4.84 días).
- Captura EMON: **12.64 GB** estimados, 6,792,099 grupos.
- JSONL crudo: **0.08 GB**; eventos por aplicación: **0.27 GB**.
- Espacio estimado total de captura: **12.98 GB** (sin margen libre operativo).

R común por W,m: `{"W1024_m1": 68034, "W1024_m16": 29780, "W1024_m4": 119120, "W1024_m8": 59560, "W12288_m1": 1706, "W12288_m16": 107, "W12288_m4": 427, "W12288_m8": 214, "W1280_m1": 54403, "W1280_m16": 23814, "W1280_m4": 95254, "W1280_m8": 47627, "W1536_m1": 45323, "W1536_m16": 31172, "W1536_m4": 79356, "W1536_m8": 39678, "W16384_m1": 1280, "W16384_m16": 80, "W16384_m4": 320, "W16384_m8": 160, "W1792_m1": 38889, "W1792_m16": 17023, "W1792_m4": 68090, "W1792_m8": 34045, "W2048_m1": 34017, "W2048_m16": 14890, "W2048_m4": 59560, "W2048_m8": 29780, "W2560_m1": 27202, "W2560_m16": 18709, "W2560_m4": 47627, "W2560_m8": 23814, "W256_m1": 76661, "W256_m16": 4792, "W256_m4": 19166, "W256_m8": 9583, "W288_m1": 68276, "W288_m16": 4268, "W288_m4": 17069, "W288_m8": 8535, "W3072_m1": 22678, "W3072_m16": 15598, "W3072_m4": 39707, "W3072_m8": 19854, "W4096_m1": 17009, "W4096_m16": 11698, "W4096_m4": 29780, "W4096_m8": 14890, "W512_m1": 135768, "W512_m16": 93379, "W512_m4": 237718, "W512_m8": 118859, "W6144_m1": 3411, "W6144_m16": 214, "W6144_m4": 853, "W6144_m8": 427, "W768_m1": 90778, "W768_m16": 39736, "W768_m4": 158943, "W768_m8": 79472, "W8192_m1": 2559, "W8192_m16": 160, "W8192_m4": 640, "W8192_m8": 320}`.

La duración incluye las 420 condiciones, N=31, R episodios, cinco warmups, flush, acondicionamiento, barreras y EMON escalado desde el smoke. Excluye preflight, QA/reloj/expulsión, cualificación previa y sus controles finales, cuyos tiempos no están calibrados en esta captura; por eso el total end-to-end será mayor.

Extrapolación desde cinco celdas SMOKE_ONLY: vecino más cercano en log(O), log(m) y log(K), con escala lineal O×m. R apunta a 2,5 s para K=1 por W,m y se comparte entre variantes y K. Son cifras de planificación; no son medidas oficiales.
