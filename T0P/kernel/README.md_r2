# T0P Q4 OMEGA

Kernel AVX-VNNI-256 y QA no cronometrada para una matriz `1024×512`, con `m={1,4,8,16}`. El layout de ronda 2 empaqueta dos bloques K4 en los nibbles bajo/alto de una carga de 32 bytes.

## Archivos

- `q4_kernel.hpp/.cpp`: API de preparación estática Q4, preparación dinámica INT8 y kernel puro.
- `qa_main.cpp`: comprobaciones S-12, representación efectiva, aislamiento de dots, barrido de coordenadas, ceros, finitud y páginas.
- `qa_kernel_gate.py` y `PAGE_EXPECTED_KERNEL.json`: gate de cobertura de las asignaciones efectivas, con fórmula de página S-16.
- `build.cmd`: compilación MSVC, `dumpbin /disasm` y ejecución de QA; no mide tiempo.
- `KERNEL_DESIGN.md`: decisiones de layout, flujo y semántica.
- `logs/STATIC_INSTRUCTION_COUNTS.md`: conteo estático del bucle K4 extraído de `dumpbin /disasm`, incluyendo cargas/broadcasts y spills.

Desde la raíz del proyecto: `cmd /c T0P\kernel\build.cmd`. La salida cruda queda bajo `T0P\kernel\logs\`; los objetos y el ejecutable permanecen en esta carpeta.

`qa_kernel.exe` requiere AVX-VNNI-256 anunciado por CPUID y regiones large-page disponibles para los buffers consultados. Termina con código distinto de cero si falla cualquier gate. La QA usa las 40 celdas actuales de `T0P\common\data` (20 celdas históricas y 20 addenda). No contiene temporización del operador.
