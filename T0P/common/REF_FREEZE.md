# Freeze común S-12

API congelada: `reference64()` mantiene r64 en double y calcula S desde códigos/escalas canónicos; `S+` y `B` se redondean conservadoramente operación a operación; `verify_s12()` compara el error elevado contra B. `reference()` y `verify()` conservan el cálculo FP32 y la tolerancia `1e-5+1e-5|y_ref|` solo para diagnóstico.

Se mantienen intactas las 20 celdas históricas. Las nuevas celdas por M={1,4,8,16} son entrada nula, activación de coordenada 0, cancelación de grupos con S/|r|≈2049 y cancelación exacta más residuo FP16 con S/|r|≈9.55e11. Hashes congelados antes del QA: `NEW_FIXTURES_FROZEN.json`, SHA-256 `c5357fb756dad2afd7f755fff5ea14889b0ca06b6a8f694a61d3987d0f6591ed`.

El barrido de activación cubre 512 posiciones en 512 lotes por M; Torch, flat y repack dan igualdad exacta en todos. Las pruebas comunes de comparador y representación pasan: límites inclusivos/exclusivos en ambos signos, NaN, Inf, tamaño inconsistente, S=0 exacto y mutaciones negativas de código/bit de escala pese a activación nula. `exact_group_dots()` prueba la API contra el oráculo independiente. Las reevaluaciones externas están en `../ref_torch/REF_FREEZE.md` y `../ref_ggml/REF_FREEZE.md`.

Pendiente: la interpretación de cancelación casi exacta está abierta a Sol porque MD/38 truncó su definición. La captura de dots internos ggml tampoco está disponible.



