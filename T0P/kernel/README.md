# T0P Q4 OMEGA

Kernel AVX-VNNI-256 de geometría variable y QA no cronometrada. Mantiene `d=512`, `m={1,4,8,16}` y admite un único O en ejecución para el barrido S-19, desde 912 hasta 58.256 filas (múltiplos de 8).

## Archivos

- `q4_kernel.hpp/.cpp`: API de tamaños dependientes de O, preparación Q4 y ejecución sin instrumentación.
- `qa_main.cpp`: S-12 contra referencia independiente, identidad binaria en las 40 celdas históricas, representación efectiva, colas, coordenadas, ceros, grupos y páginas. El barrido llama a `run()` completo en todos los puntos; O=1024 compara las 512 salidas por coordenada y los otros quince O comparan las primeras y últimas ocho.
- `qa_kernel_gate.py` y `PAGE_EXPECTED_KERNEL.json`: geometría de página y cobertura diferenciada del barrido; el gate rechaza `qa_run_selected_tiles`, coordenadas omitidas y registros duplicados.
- `build_d12_r4.cmd`: recompila y ejecuta la QA completa, y conserva registros D12 aparte. `verify_kernel_object.ps1` compara el objeto integrado con el objeto exacto probado por QA (y, si no es byte-idéntico, compara el desensamblado normalizado).
- `KERNEL_DESIGN.md`: contrato, tabla W/O/bytes, colas, semilla de expansión y cobertura.
- `logs/STATIC_INSTRUCTION_COUNTS.md`: recuento estático del recorrido principal y las colas.

Desde cualquier directorio, con herramientas MSVC ya instaladas: `cmd /c C:\Projects\OMEGA\Ejercise\T0P\kernel\build_d12_r4.cmd`. Para probar el objeto ya integrado al harness, se pasa su ruta como primer argumento; el baseline O=1024 se resuelve desde el directorio del ejecutable y admite ruta explícita como segundo argumento de `qa_kernel.exe`. QA completa: 16 O × 4 m, diez fixtures, 512 coordenadas y aislamiento exacto de 16 grupos.

La QA enlaza `obj\q4_kernel.obj` directamente. El harness debe enlazar ese mismo objeto (omitiendo su recompilación de `q4_kernel.cpp`) para identidad binaria; compruébalo con `powershell -File T0P\kernel\verify_kernel_object.ps1 -IntegratedObject <ruta-al-objeto-del-harness>`. Si el harness recompila con otras banderas, el script registra el hash diferente y exige igualdad del desensamblado normalizado; esa comprobación no sustituye la opción preferida de enlazar el objeto probado.

No contiene temporización del operador ni mediciones de rendimiento. El binario requiere AVX-VNNI-256 y large pages para las asignaciones verificadas.

`SHA256SUMS_kernel.txt` enumera los artefactos presentes y verifica cada SHA-256; se excluyen el propio manifiesto y el registro de verificación. No se incluye `__pycache__`.
