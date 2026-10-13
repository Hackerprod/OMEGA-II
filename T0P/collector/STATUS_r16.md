# STATUS R16

- Estado: smoke de adquisición R16 completado, `SMOKE_ONLY`; no adjudica métricas científicas.
- Intento: `logs_r16/obj_r16/smoke_r16_c41b_finalbudget_20261012`.
- Cadena: preflight → QA/instrumentos/expulsión → cualificación mínima → corte automático → residencia; las transiciones realizadas pasaron.
- Residencia: 264/264 IDs en orden y multiplicidad; 20 unidades de calibración, 204 filas de muestra, 408 márgenes; verificador R16 PASS.
- EMON: `-l50199`; parada controlada `emon -stop`, exit 0. QPC padre 2.469,498957 s; crudo 84.707.948 bytes. Intento actual 460.042.675 bytes (<1 GiB).
- S20 diagnóstico: INVALID por `CACHE_DENOM_ZERO`, canales/cobertura/ventanas incompletos y lectura térmica inválida; cifras no usadas.
- D8 final (frío/caliente, ratio y S-22 sobre crudos) pendiente de D10. La prueba sintética D8 acepta ventanas distintas entre Q y R y exige igualdad solo dentro de cada captura/alcance.
- Pendiente: D10 validador desde crudos, D11 derivador, D12 QA integrada, D14 autorización, prueba acotada de 36 bloques y estimación. La selección/comparación científica de 18.414 eventos no se adquirió: sesión oficial prohibida en esta tarea.
- Limpieza: cuatro intentos R16 fallidos previos permanecen (310.310.597 bytes); la revisión automática bloqueó dos veces el borrado del primero y bloqueó el borrado de los otros tres. No se usó otro método. El smoke completado permanece para revisión.
- C: libre al actualizar: $free bytes. Smoke completado: $current bytes.
