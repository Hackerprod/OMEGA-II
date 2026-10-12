# COLLECTOR_SPEC_r14

## Plan D2

`residency_plan_r14.py` genera un plan JSON determinista y un schedule de smoke separado. Verifica exactamente 420 condiciones: 60 arquitectónicas (`W=288`, `O=1024`) y 360 de barrido (incluye `W=1024`, `O=3640`); 140 unidades de calibración (20 arquitectónicas y 120 de barrido); réplicas 1–31; eventos A/B/C serializados; 124 pares A/A; IDs únicos y unión exacta de IDs. La prueba muta el plan ejecutado con un ID faltante y duplicado y exige HOLD.

```powershell
python -B T0P\collector\residency_plan_r14.py generate
python -B T0P\collector\residency_plan_r14.py verify
python -B T0P\collector\residency_plan_r14.py self-test
```

MAC esperado por celda: `512 * O * m`; la verificación del plan confirma fórmula y parámetros en cada condición.

## Captura y S-20 (D10/D13)

`telemetry_r14.py` prepara intentos y procesa EMON. Importa como validador único `T0P\validator\s20_validator.py`; el archivo se consume sin editar. `qualify_r14.py` es el calificador de entradas/captura y delega S-20 al mismo módulo, sin mantener una implementación paralela. Los fixtures mutan de verdad el mapeo TSC/QPC y la cobertura EMON y comprueban rechazo.

El almacén D13 es `T0P\validator\artifact_store.py`, sin editar. Las entradas crudas se crean como nuevas, se registran por SHA256 y quedan write-once. `residency_derive_r14.py derive` solo añade artefactos derivados y IDs derivados; el path de salida debe estar dentro del intento. Plan, schedule, episodios, marcas, márgenes, EDP, EMON y logs se conservan como crudos cuando existen.

## Derivación D9/D11

El derivador valida la unión exacta de IDs del plan con IDs ejecutados/derivados; falta, duplicado u huérfano produce HOLD. Cada celda requiere control S-20 válido antes de aportar métrica. Comprueba R común por unidad `W,m,K` para A/B/C, marcas de episodios y límites de margen, identidad de buffers A/C, independencia de B y el cómputo `T_X,r(K)=Σt/R`, `c_X,r=(T(16)-T(1))/15` cuando existe la serie completa. `AA31` se forma por `m` arquitectónico; `δBC` por `m`; `ρ`, `max(S8,S16)`, ruido A/A y atribución L2 usan los umbrales definidos en S-19. Estados y rutas HOLD se prueban con fixtures por rama. El modo SMOKE informa `numeric_metrics_used=false` y evita adjudicar cifras.

## Comandos de QA

```powershell
python -B T0P\collector\residency_plan_r14.py self-test
python -B T0P\collector\qualify_r14.py --self-test
python -B T0P\collector\residency_derive_r14.py self-test
python -B T0P\collector\telemetry_r14.py self-test
```

`generate`, `verify` y `smoke-schedule` escriben solo en `T0P\collector\logs_r14\obj_r14` o la ruta de salida explícita autorizada. Use `-B` para evitar `__pycache__`.

La comprobación posterior de un intento SMOKE completo es de solo lectura y valida D13, la unión de schedule/eventos, R común, direcciones/SHA de A/C, timestamps y márgenes, MAC, marcas S-20 y cada contador del manifiesto de expulsión:

```powershell
python -B T0P\collector\verify_residency_smoke_r14.py --attempt T0P\harness\logs_r14\obj_r14\<attempto>
```
