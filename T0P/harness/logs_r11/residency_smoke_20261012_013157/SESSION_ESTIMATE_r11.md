# Estimación de sesión oficial R11

- Duración total estimada: **104.17 h** (4.34 días).
- Captura EMON: **11.01 GB** estimados, 5,950,524 grupos.
- JSONL crudo: **0.08 GB**; eventos por aplicación: **0.27 GB**.
- Espacio estimado total de captura: **11.35 GB** (sin margen libre operativo).

R común por W,m: `{"W1024_m1": 69771, "W1024_m16": 21262, "W1024_m4": 85048, "W1024_m8": 42524, "W12288_m1": 1676, "W12288_m16": 105, "W12288_m4": 419, "W12288_m8": 210, "W1280_m1": 55792, "W1280_m16": 17003, "W1280_m4": 68009, "W1280_m8": 34005, "W1536_m1": 46480, "W1536_m16": 31080, "W1536_m4": 56658, "W1536_m8": 28329, "W16384_m1": 1257, "W16384_m16": 79, "W16384_m4": 315, "W16384_m8": 158, "W1792_m1": 39882, "W1792_m16": 12154, "W1792_m4": 48615, "W1792_m8": 24308, "W2048_m1": 34886, "W2048_m16": 10631, "W2048_m4": 42524, "W2048_m8": 21262, "W2560_m1": 27896, "W2560_m16": 18654, "W2560_m4": 34005, "W2560_m8": 17003, "W256_m1": 75349, "W256_m16": 4710, "W256_m4": 18838, "W256_m8": 9419, "W288_m1": 67108, "W288_m16": 4195, "W288_m4": 16777, "W288_m8": 8389, "W3072_m1": 23257, "W3072_m16": 15552, "W3072_m4": 28350, "W3072_m8": 14175, "W4096_m1": 17443, "W4096_m16": 11664, "W4096_m4": 21262, "W4096_m8": 10631, "W512_m1": 139235, "W512_m16": 93104, "W512_m4": 169723, "W512_m8": 84862, "W6144_m1": 3352, "W6144_m16": 210, "W6144_m4": 838, "W6144_m8": 419, "W768_m1": 93095, "W768_m16": 28371, "W768_m4": 113481, "W768_m8": 56741, "W8192_m1": 2514, "W8192_m16": 158, "W8192_m4": 629, "W8192_m8": 315}`.

Extrapolación desde cinco celdas SMOKE_ONLY: vecino más cercano en log(O), log(m) y log(K), con escala lineal O×m. R apunta a 2,5 s para K=1 por W,m y se comparte entre variantes y K. Son cifras de planificación; no son medidas oficiales.
