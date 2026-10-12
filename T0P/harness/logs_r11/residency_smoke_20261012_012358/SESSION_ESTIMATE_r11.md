# Estimación de sesión oficial R11

- Duración total estimada: **109.65 h** (4.57 días).
- Captura EMON: **11.61 GB** estimados, 6,265,679 grupos.
- JSONL crudo: **0.07 GB**; eventos por aplicación: **8900.04 GB**.
- Espacio estimado total de captura: **8911.72 GB** (sin margen libre operativo).

R común por W,m: `{"W1024_m1": 70154, "W1024_m16": 30054, "W1024_m4": 120213, "W1024_m8": 60107, "W12288_m1": 1696, "W12288_m16": 106, "W12288_m4": 424, "W12288_m8": 212, "W1280_m1": 56099, "W1280_m16": 24032, "W1280_m4": 96128, "W1280_m8": 48064, "W1536_m1": 46735, "W1536_m16": 28889, "W1536_m4": 80084, "W1536_m8": 40042, "W16384_m1": 1272, "W16384_m16": 80, "W16384_m4": 318, "W16384_m8": 159, "W1792_m1": 40101, "W1792_m16": 17179, "W1792_m4": 68715, "W1792_m8": 34358, "W2048_m1": 35077, "W2048_m16": 15027, "W2048_m4": 60107, "W2048_m8": 30054, "W2560_m1": 28050, "W2560_m16": 17338, "W2560_m4": 48064, "W2560_m8": 24032, "W256_m1": 79004, "W256_m16": 4938, "W256_m4": 19751, "W256_m8": 9876, "W288_m1": 70363, "W288_m16": 4398, "W288_m4": 17591, "W288_m8": 8796, "W3072_m1": 23385, "W3072_m16": 14455, "W3072_m4": 40071, "W3072_m8": 20036, "W4096_m1": 17539, "W4096_m16": 10842, "W4096_m4": 30054, "W4096_m8": 15027, "W512_m1": 140000, "W512_m16": 86538, "W512_m4": 239899, "W512_m8": 119950, "W6144_m1": 3391, "W6144_m16": 212, "W6144_m4": 848, "W6144_m8": 424, "W768_m1": 93607, "W768_m16": 40101, "W768_m4": 160402, "W768_m8": 80201, "W8192_m1": 2544, "W8192_m16": 159, "W8192_m4": 636, "W8192_m8": 318}`.

Extrapolación desde cinco celdas SMOKE_ONLY: vecino más cercano en log(O), log(m) y log(K), con escala lineal O×m. R apunta a 2,5 s para K=1 por W,m y se comparte entre variantes y K. Son cifras de planificación; no son medidas oficiales.
