# Collector R12

## Entrada y fail-closed

`qualify_r12.py` verifica el SHA-256 canónico del plan y parsea JSON estricto. Rechaza claves duplicadas, valores no finitos, desbordamientos y texto final. Exige filas OFFICIAL, cobertura del plan y validación S-20; una referencia candidata obligatoria ausente produce HOLD.

## D3: G apareado

La selección usa su propia serie de 31 eventos `reference_selection_*`; la comparación usa otras 31 observaciones `competency`, sin reutilizar. La tasa usa MAC útiles y tiempo normalizado por R. Para cada réplica calcula PΩ/Pref y toma la mediana de los cocientes apareados, nunca el cociente de medianas de tiempos. Con trabajo y R iguales equivale a Tref/TΩ. En 8P el tiempo del equipo es el intervalo concurrente desde el primer inicio al último fin. Referencias obligatorias ausentes producen HOLD.

## D4: trabajo y H-KQ

El trabajo aceptado es `512·O·m`, contrastado con O=1024 y m del evento congelado; el log no puede sustituirlo. H-KQ usa solo OMEGA/prepared/8P/O1024/m1/N31: mediana de tasa del equipo dividida por ocho y mínimo de las ocho medianas por worker, ambas ≥33,1 MAC/ns. D′ reutiliza esas 31 observaciones.

## D5: BW_DRAM

Exige 256 MiB por worker/pasada, prepared, 8P, 31 réplicas, R común y mapa P-core. La tasa cuenta bytes útiles sobre pared concurrente. La prueba negativa resta 64 bytes y exige HOLD.

## D6: P_instr

Exige OFFICIAL, prepared, O=1024/m=1, nueve geometrías congeladas, ocho workers, N=31, cinco warmups, un hash de desensamblado PASS y R fijo por configuración. Calcula tasas por core y evalúa, sin compensación por media, los tres contrastes simétricos frente a 14/U8 con límite 3 %. Si el instrumento o el desensamblado no cualifica, devuelve HOLD_INSTRUMENTATION.

## S-20, D13 y SMOKE

`telemetry_valid()` llama al validador único de `T0P\\validator`; ignora etiquetas de estado. Las pruebas mutan campos reales conservando texto VALID: una discrepancia TSC/QPC de 2 % y una captura EMON con cobertura 1 % y hueco de 1000 ms son rechazadas. D13 exige intento nuevo, crudos inmutables con SHA y lineage separado para derivados.

El cualificador devuelve SMOKE como `SMOKE_ONLY`, con `numeric_metrics_used=false` y residencia no iniciada. El auto-test final informa ocho casos sintéticos PASS, fixtures G=0,80 FAIL, fronteras H-KQ gobernantes, MAC=512·O·m, bytes BW inválidos, contrastes P_instr, dos mutaciones S-20 y D13 write-once PASS.

Evidencia final en `T0P\\collector\\logs_r12\\obj_r12\\synthetic_qa_r12_final2.txt`; smoke de proveedores en `T0P\\harness\\logs_r12\\obj_r12\\smoke_N3_r12_final3`, cualificado como `SMOKE_ONLY`. La medición real se bloqueó en preflight porque el host no tiene habilitado `SeLockMemoryPrivilege`; no se fabricaron cifras.
