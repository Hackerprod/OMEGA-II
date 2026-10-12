# Harness R8: estado de la integración

Copia de trabajo aislada del harness. Compila con `build_r8.cmd`; las fuentes y artefactos de esta revisión llevan `_r8` o se guardan en `logs_r8`/`obj_r8`.

## Implementado y ejecutado

- Ocho workers persistentes, afinidad, cinco warmups, gates numéricos y large pages.
- Smoke N=3 cronometrado en bloques activos de al menos 2,5 s, con márgenes ≥200 ms. No hay pausas dentro del bloque. EMON observó 42–43 ventanas/bloque y 100 % cobertura en la última integración.
- Preflight llama al verificador estricto del collector antes del smoke; prueba rechazo tras alteración de un byte y rechazo oficial sin autorización. El smoke nunca crea `AUTHORIZED.json`.
- Salida JSONL cruda y resumen mediana/IQR/MAD/mín/máx.

## Límites abiertos

`R` aún se obtiene al repetir hasta alcanzar duración, no por calibración común por celda ni por regla del máximo entre proveedores/variantes. El plan se verifica, pero las celdas y condiciones consumidas aún no se despachan desde su orden serializado. Solo se ejecuta `mm_out`; faltan las 16 formas `O`, m=1/4/8/16 y las otras rutas Torch. El analizador externo solo deriva ventanas/cobertura/hueco y fracciones L2/LLC; `f_activo`, potencia 0x611 y flags térmicos siguen ausentes. Por ello los registros quedan `PENDING_BLOCK_ANALYSIS`, no `VALID`. No hay parser JSONL estricto, estadísticos por celda o modo `--official` en el binario harness. El gate de página no grande se prueba negativamente.

## Esquema crudo actual

Cada línea JSON es una muestra worker/replica con `label`, `provider`, `cell`, `m`, `replica`, `worker`, `lp`, `N`, `R`, `t_app_us`, QPC de margen/inicio/fin, TSC inicio/fin, `large_pages`, `affinity`, `telemetry` y `status`. `summary.json` contiene una línea JSON por proveedor con `N`, mediana, IQR, MAD, mínimo, máximo, cruce QPC y etiqueta. Este esquema describe el prototipo; no es todavía un contrato de parser estricto.

## Límite geométrico

`common::O` permanece 1024. `provider_omega_r8.cpp` y los proveedores de referencia conservan esa restricción porque sus preparadores/fixtures de referencia están ligados a la celda congelada. Ampliar la geometría sin cambiar contratos requiere preparar y validar nuevos buffers en los adaptadores del harness.
