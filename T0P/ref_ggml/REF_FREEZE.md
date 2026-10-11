# Freeze de referencia ggml CPU

## HISTORIAL

S-12, S-13 y S-14 quedan identificados como historial en `REF_FREEZE.json`; el HOLD por privilegio ausente de ejecuciones anteriores no describe el estado gobernante. Se conservan las lagunas: `qa_gate.py_preS14` no es idéntico byte a byte al gate previo y los `.lib/.exp` históricos no se recuperaron. No se afirma reproducción byte a byte. Los fixtures congelados no se modificaron.

## GOBERNANTE — S-15

La corrida completa terminó con código 0. La candidata plana conserva `ggml_mul_mat` con `use_ref=false`; repack ejecuta las funciones upstream GEMV/GEMM. El buffer Y de ggml continúa enlazado directamente a la región common de large pages. Las consultas de `QueryWorkingSetEx` verifican las páginas reales de salida.

| Candidata | QA_NUMERICAL | DISPATCH_QUALIFIED | PAGE_POLICY | REFERENCE_READY |
|---|---|---|---|---|
| ggml plana | PASS | PASS | PASS | PASS |
| ggml repack | PASS | PASS | PASS | PASS |

Ambas pasan 40/40 celdas, S-12, controles exactos, barrido de 512 coordenadas por forma, cancelación exacta, aislamiento de group dots y despacho. El gate exige los 240 registros congelados por fixture, forma, candidata y buffer; todos tienen páginas consultadas válidas y grandes.

Logs gobernantes: [`adapter/qa_s15_governing.txt`](adapter/qa_s15_governing.txt), SHA-256 `a736fc9201eb5e0b24b1fc36efd992d6a999136940cf5f069dd995149713cea5`; gate [`adapter/qa_gate_s15_governing.txt`](adapter/qa_gate_s15_governing.txt), SHA-256 `d9c3b9f6c7fe01992a040d4812a24e6749f314004256df9a4246f6a4396216e0`. Hashes de `main.cpp`: `11bc6be35d0c1cfca2afc02015a3c825d9981f2dc7400d42ecfeb371c55e585c`; `qa_gate.py`: `be7d608d3d03bcfaaeb9cca175129d8ba296e98c96bdda982b0c46b221b2dabe`; `PAGE_EXPECTED_S15.json`: `aa8e25c5f1b0e93b0741ca32169757a9da1f980211151a91ffc383276c52b027`. La evidencia de dispatch S-14 se conserva como contexto y la corrida S-15 acredita el despacho efectivo de esta ejecución. Ver identidades y trazabilidad en `REF_FREEZE.json`.


## GOVERNING_S16

Reevaluaci?n con el parser S-16 de `adapter/qa_s15_governing.txt` intacto: `adapter/qa_gate_s16_governing.txt`. `ggml_flat` y `ggml_repack`: QA_NUMERICAL, DISPATCH_QUALIFIED, PAGE_POLICY y REFERENCE_READY = PASS. Crudo SHA-256 a736fc9201eb5e0b24b1fc36efd992d6a999136940cf5f069dd995149713cea5; adaptador sin cambios: `main.cpp` 11bc6be35d0c1cfca2afc02015a3c825d9981f2dc7400d42ecfeb371c55e585c y `adapter.exe` 3dcbad85a709739474bff7a176be4c5b7e85e7939c14db67316843aa071109c7. Hashes del parser, pruebas y pol?tica: ver bloque GOVERNING_S16 de `../common/REF_FREEZE.md`; pruebas S-16 PASS.
