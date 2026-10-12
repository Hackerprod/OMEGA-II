# T0P Q4 OMEGA

Kernel AVX-VNNI-256 de geometría variable y QA no cronometrada. Mantiene `d=512`, `m={1,4,8,16}` y admite un único O en ejecución para el barrido S-19, desde 912 hasta 58.256 filas (múltiplos de 8).

## Archivos

- `q4_kernel.hpp/.cpp`: API de tamaños dependientes de O, preparación Q4 y ejecución sin instrumentación.
- `qa_main.cpp`: S-12 contra referencia independiente, identidad binaria en las 40 celdas históricas, representación efectiva, colas, coordenadas, ceros, grupos y páginas.
- `qa_kernel_gate.py` y `PAGE_EXPECTED_KERNEL.json`: tamaños `288·O`, `672·m`, `4·m·O`, `64·m·O`, geometría de página y contrapruebas negativas.
- `build.cmd`: compilación MSVC, `dumpbin /disasm` y ejecución de la QA completa; no mide tiempo.
- `KERNEL_DESIGN.md`: contrato, tabla W/O/bytes, colas, semilla de expansión y cobertura.
- `logs/STATIC_INSTRUCTION_COUNTS.md`: recuento estático del recorrido principal y las colas.

Desde la raíz del proyecto: `cmd /c T0P\kernel\build.cmd`. Objetos bajo `obj\`, salidas y registros bajo `logs\`. QA completa: 16 O × 4 m, diez fixtures, 512 coordenadas y aislamiento exacto de 16 grupos.

No contiene temporización del operador ni mediciones de rendimiento. El binario requiere AVX-VNNI-256 y large pages para las asignaciones verificadas.

`SHA256SUMS_kernel.txt` enumera los artefactos presentes y verifica cada SHA-256; se excluyen el propio manifiesto y el registro de verificación. No se incluye `__pycache__`.
