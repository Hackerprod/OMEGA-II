# Estimación de sesión oficial R11

- Duración total estimada: **130.47 h** (5.44 días).
- Captura EMON: **14.00 GB** estimados, 7,538,876 grupos.
- JSONL crudo: **0.08 GB**; eventos por aplicación: **0.27 GB**.
- Espacio estimado total de captura: **14.34 GB** (sin margen libre operativo).

R común por W,m: `{"W1024_m1": 70124, "W1024_m16": 29565, "W1024_m4": 118258, "W1024_m8": 59129, "W12288_m1": 1707, "W12288_m16": 107, "W12288_m4": 427, "W12288_m8": 214, "W1280_m1": 56075, "W1280_m16": 23642, "W1280_m4": 94565, "W1280_m8": 47283, "W1536_m1": 46716, "W1536_m16": 28928, "W1536_m4": 78781, "W1536_m8": 39391, "W16384_m1": 1280, "W16384_m16": 80, "W16384_m4": 320, "W16384_m8": 160, "W1792_m1": 40084, "W1792_m16": 16900, "W1792_m4": 67597, "W1792_m8": 33799, "W2048_m1": 35062, "W2048_m16": 14783, "W2048_m4": 59129, "W2048_m8": 29565, "W2560_m1": 28038, "W2560_m16": 17362, "W2560_m4": 47283, "W2560_m8": 23642, "W256_m1": 76781, "W256_m16": 4799, "W256_m4": 19196, "W256_m8": 9598, "W288_m1": 68383, "W288_m16": 4274, "W288_m4": 17096, "W288_m8": 8548, "W3072_m1": 23375, "W3072_m16": 14475, "W3072_m4": 39420, "W3072_m8": 19710, "W4096_m1": 17531, "W4096_m16": 10856, "W4096_m4": 29565, "W4096_m8": 14783, "W512_m1": 139941, "W512_m16": 86656, "W512_m4": 235997, "W512_m8": 117999, "W6144_m1": 3412, "W6144_m16": 214, "W6144_m4": 853, "W6144_m8": 427, "W768_m1": 93568, "W768_m16": 39449, "W768_m4": 157793, "W768_m8": 78897, "W8192_m1": 2559, "W8192_m16": 160, "W8192_m4": 640, "W8192_m8": 320}`.

Extrapolación desde cinco celdas SMOKE_ONLY: vecino más cercano en log(O), log(m) y log(K), con escala lineal O×m. R apunta a 2,5 s para K=1 por W,m y se comparte entre variantes y K. Son cifras de planificación; no son medidas oficiales.
