# Referencia ggml CPU

La revisión S-12, rutas, resultados y limitaciones están en [REF_FREEZE.md](REF_FREEZE.md) y [REF_FREEZE.json](REF_FREEZE.json). Matriz por celda: `adapter/QA_RESULTS.md`; salida cruda: `adapter/qa_output.txt`. Se conservaron los resultados previos con sufijo `_preS12`. No se cronometró.

Estado gobernante S-13 en `REF_FREEZE.md`: compila repack con `__AVXVNNI__`; la QA integral pasa S-12, la cancelación exacta y los dots aislados. El gate ggml permanece FAIL porque el callback plano del trait solo se alcanza en m=1; m=4,8,16 no lo alcanzaron en la ejecución observada. No se cronometró.

Cierre S-13: gates GGML 80/80 PASS; callback plano alcanzado en M=1,4,8,16. `QueryWorkingSetEx` recorre todas las p?ginas efectivas despu?s de ejecutar los kernels; 230 consultas, 0 fallos.


E3: `prepare_static` y `prepare_dynamic` reciben la candidata Flat/Repack; el modo combinado existe solo para QA. Flat prepara Q8_0 reutilizado; Repack empaqueta directamente Q8_0x4, excepto M=1 que necesita Q8_0. `STATIC_ROUTE_ISOLATION` confirma que cada ruta prepara ?nicamente su almacenamiento est?tico. `run_once` no contiene controles QA.

Comprobaci?n final E3: `STATIC_ROUTE_ISOLATION` y `DYNAMIC_ROUTE_ISOLATION` pasan en la corrida gobernante; el gate exige ambos.
