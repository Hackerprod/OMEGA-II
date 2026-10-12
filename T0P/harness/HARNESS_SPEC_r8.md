# HARNESS_SPEC R8 (prototipo)

## Construcción y aislamiento

`build_r8.cmd` compila únicamente las copias `*_r8.cpp`/`*_r8.hpp`, produce `harness_r8.exe`, registra build en `logs_r8` y objetos en `obj_r8`. No inicia una sesión oficial ni escribe `AUTHORIZED.json`. Antes de calibrar/medir, `preflight_r8.ps1` ejecuta `collector.exe --plan-check` para validar JSON estricto, checksum SHA-256 y orden determinista. También rechaza un plan mutado un byte y prueba `collector.exe --official` sin autorización.

## Proveedores y temporización

El ejecutable contiene ocho workers persistentes en LP 0,2,…,14, cinco warmups, gates de representación/dots/S-12 y QueryWorkingSetEx. Cada réplica SMOKE repite el kernel sin pausas durante al menos 2,5 s entre marcas QPC/TSC; se dejan márgenes de trabajo ≥200 ms antes y después. En la integración 2026-10-11 se observaron 42–43 ventanas EMON por bloque y cobertura reportada de 100 %. Las pruebas son `SMOKE`, N=3 y no son cifras oficiales.

La versión todavía no calcula un R común. La regla requerida para cerrar esa parte es calibrar cada proveedor/variante con muestras piloto sin pausas, elegir el R mínimo que alcance 2,5 s para todos los miembros de la unidad comparativa (se acepta ≥2,0 s), guardar esa calibración y rechazar cualquier muestra oficial inferior a 2,0 s. El estado actual repite dinámicamente hasta 2,5 s y no sirve como adquisición oficial.

## Plan

El verificador estricto externo valida el plan completo antes del smoke. La ejecución del harness R8 aún usa celdas hard-coded: el orden, condición, variante y parámetros no están derivados de `events[].id/order_key`. El plan mutado se rechaza por checksum. El hash verificado registrado por preflight identifica la entrada consumida.

## Geometría

Las formas objetivo son OMEGA para los 16 O del barrido y O=1024, m={1,4,8,16}; ggml flat y Torch mm/matmul/linear/mv `_out` deben compartir formas, con Y de Torch en large pages y comprobación de despacho. R8 solo ejecuta O=1024, m=1 y Torch mm_out. `mkldnn_linear` está excluido. Los adapters actuales y `common::O=1024` limitan ampliar referencias sin construir fixtures/buffers propios en el harness.

## Telemetría

El esquema previsto por bloque incluye `coverage_pct`, `gap_upper_ms`, `windows`, `f_active=sum(THREAD)/sum(REF_TSC)` con calificación de f_TSC, potencia de paquete de delta 0x611 con `u_E`, flags térmicos 0x19C/0x1B1 y fracciones L2/LLC. El analizador usado por R8 actualmente produce conteo de ventanas, cobertura, hueco, márgenes, L2 y LLC. No calcula f_activo, P ni flags; por eso las muestras R8 quedan `PENDING_BLOCK_ANALYSIS` y no se deben promover a `VALID`.

## JSONL y resumen

JSONL crudo actual: una línea JSON por worker/replica con `label`, `provider`, `cell`, `m`, `replica`, `worker`, `lp`, `N`, `R`, `t_app_us`, marcas QPC (`t_pad_ini_qpc`, `t_ini_qpc`, `t_fin_qpc`, `t_pad_fin_qpc`), `tsc_start`, `tsc_end`, `large_pages`, `affinity`, `telemetry`, `status`. Resumen: JSONL de filas por proveedor con `N`, `median_us`, `IQR_us`, `MAD_us`, `min_us`, `max_us`, `qpc_crosscheck`, `telemetry`, `label`. Aún falta parser estricto, resumen por celda y motivo normalizado `VALID`/`INVALID_TELEMETRY`.

## Modos y negativos

`--smoke` está disponible con N=3. `--official` del collector falla cerrado sin AUTHORIZED.json. El binario harness todavía no implementa `--official` ni N=31/warmup=5 como selector de modo. Negativos cubiertos por la prueba del harness: página fuera de large pages, duración oficial <2 s e invalidación de telemetría; preflight cubre plan alterado y rechazo oficial sin autorización.
