# Qualification results r6 — R1, D10, D13

## R1 / D2-INST_RETIRED

Desensamblado: tres instrucciones por iteración y cuatro fuera; `I0=3×200000000000+4=600000000004`. EMON `--dry-run -m -C "INST_RETIRED.ANY"` confirmó `Event Set 0`, una sola vez como grupo efectivo, Fixed Counter 0 (`0x309`). La sintaxis continua aplicable según el `emon_help.txt` local es `emon -t 0 -C "INST_RETIRED.ANY" -m -c -u -f <raw_nuevo> <helper.exe> <reporte_nuevo> <mapa_nuevo>`: con aplicación, `-t 0` llega hasta la salida; `-f` crea archivo; al usar un solo evento/grupo no hay multiplexación. El comando de colección real se bloqueó antes de ejecutarlo por la prohibición expresa de escribir MSR.

La tabla lista las cinco capturas solicitadas por los ocho LP. En las 40 celdas, EMON no se ejecutó: `I_EMON` no existe, `I0` esperado es 600000000004 y el error relativo no se puede calcular. No significa conteo cero.

Cada celda indica `I_EMON / I0 / error relativo`; `NO_MEDIDO` = captura no ejecutada; `N/C` = error no calculable.

| Captura | LP0 | LP2 | LP4 | LP6 | LP8 | LP10 | LP12 | LP14 |
|---|---|---|---|---|---|---|---|---|
| 1 | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C |
| 2 | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C |
| 3 | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C |
| 4 | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C |
| 5 | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C | NO_MEDIDO / 600000000004 / N/C |
Estado: `HOLD_INSTRUMENTATION — BLOQUEO_MSRS_PROHIBIDOS`: 0/5 colecciones y 0/40 valores EMON obtenidos; las 40 comparaciones porcentuales no son calculables. Comando bloqueado (no lanzado):

```text
C:\Program Files (x86)\Intel\oneAPI\vtune\2026.4\bin64\emon.exe -t 0 -C "INST_RETIRED.ANY" -m -c -u -f <emon_raw_nuevo> C:\Projects\OMEGA\Ejercise\T0P\telemetry\reader\inst_cli_helper_r6.exe <helper_report_nuevo.csv> <helper_clock_map_nuevo.csv>
ERROR: BLOQUEADO_POR_REGLA_DE_TAREA — EMON configura/usa contadores PMU y requiere escrituras de registros de control; el usuario prohibió escribir MSR.
```

La carga nativa DUMMY (no es captura EMON ni sustituye la tabla) compiló y ejecutó una vez: N=200000000000, resultado de cada worker=N, afinidad PASS en sus LP, región ≥20 s. Medidas (s): LP0 40,0099956; LP2 39,8391377; LP4 39,4665976; LP6 39,3696642; LP8 39,5820163; LP10 39,4575673; LP12 39,3863988; LP14 39,3456597. QPF reportada 10.000.000; regresión de su mapa QPC/UTC 9.999.999,954 Hz; incertidumbre 0,504 ms. Reporte y mapa con sidecars SHA256 en `telemetry/logs/inst_cli_r6_dummy_session/attempt-002/`.

- `helper_report_r6.csv`: `11ceb60e7249595ef37ab6890116cc0e708dad9aa78d9ba2e2ff931c83dafda5`.
- `helper_clock_map_r6.csv`: `d8c84c6bc1c64b0c52983b1f221410cfd27a52d3ae87df2ba9c70a6a14623d76`.
- `helper_validation_r6_final.json` resume PASS del parser R1, ligado a ambos crudos.
- La salida archivada del dry-run de EMON está en `telemetry/logs/inst_cli_r6_emon_dry_run/attempt-001/emon_dry_run_r6.txt` (SHA256 `43ff9b1a8e9580c8c1313e2f50beeb6bbaf14745d99b1cd818017baf238f0985`).

## D10 / S-20 sobre capturas reales

- `conditioning_joint_raw.txt`: `VALID`, 8 muestras P, 48 estados canal/muestra, 3600 grupos EMON; cobertura mínima 97,520651 %, hueco máximo 2,850194 ms.
- `capture_blocks_r2`: `VALID`, 40 bloques, 240 estados, 3600 grupos EMON. Cobertura `C_low`: mínimo 96,259731 %, media 96,460447 %, máximo 96,711966 %; hueco máximo 3,504884 ms; mínimo 41 ventanas. Todas las 40 coberturas cumplen el cálculo conservador R2. Resultados y linajes hashados por D13 en `validator/artifacts_r6/`.

Las configuraciones efectivas y marcas consumidas están en `validator/artifacts_r6/qualification_inputs/*/attempt-002/`; EDP, mapa, marcas y configuración están vinculados por SHA256. `attempt-001` de preparación se descartó antes de validación por un error de serialización JSON y tiene `preparation_status_r6.json` enlazado; nunca se reutilizó ni se usó para resultados.

## Pruebas deterministas

Comando desde `T0P/validator`:

```text
python -B -m unittest discover -s tests -p 'test_*.py' -v
Ran 34 tests in 3.518s
OK
```

La CLI se ejecutó también contra ambos juegos reales con `--mode temporal`: conditioning terminó `exit=0, state=VALID, samples=8, results=48`; evict terminó `exit=0, state=VALID, samples=40, results=240`.

Pruebas incluidas (nombres de `unittest`):

Parser estricto: `test_duplicate_clock_map_sample_is_rejected`, `test_duplicate_marks_header_is_rejected`, `test_duplicate_json_config_key_is_rejected`, `test_unknown_config_key_is_rejected`, `test_unconfigured_emon_event_is_rejected`.

1. `test_one_percent_coverage_and_one_second_gap_are_not_valid`
2. `test_tsc_qpc_disagreement_two_percent_invalidates`
3. `test_qpf_missing_is_an_input_error`
4. `test_window_crossing_another_condition_is_excluded`
5. `test_fewer_than_twenty_windows_invalid`
6. `test_window_over_one_hundred_ms_invalid`
7. `test_current_and_historical_prochot_are_separate_invalid_flags`
8. `test_19c_invalid_reading_bit31_is_rejected`
9. `test_611_uint32_rollover_uses_modular_delta`
10. `test_smt_sibling_mixed_is_rejected`
11. `test_one_core_one_window_outside_three_percent_is_rejected`
12. `test_coverage_uses_seconds_not_milliseconds`
13. `test_tcc_margin_below_five_degrees_is_invalid`, `test_power_limit_flag_is_reported_without_becoming_a_thermal_failure`
14. `test_1b1_bit31_is_not_transferred_as_a_validity_bit`
15. `test_full_mode_core_dispersion_control_uses_frequency_fields`, `test_full_mode_replica_drift_control_uses_frequency_fields`
16. `test_l2_llc_denominator_zero_is_not_estimable`
17. `test_r1_native_dummy_report_confirms_all_eight_workers`
18. `test_real_conditioning_joint_capture_temporal_channels`
19. `test_real_evict_capture_all_forty_blocks_conservative_r2`
20. `test_lp_core_map_must_match_effective_emon_topology`
21. `test_overwriting_a_raw_file_is_rejected_and_bytes_stay_identical`, `test_reusing_an_existing_attempt_directory_is_rejected`, `test_derived_output_links_source_raw_hash_without_changing_it`, `test_ingesting_external_raw_uses_hash_and_write_once_creation`

Fórmulas y adaptador adicionales: `test_each_window_is_contracted_before_union`, `test_active_frequency_uses_ratio_of_sums`, `test_api_adapter_snapshots_edp_and_hash_links_result`.

## Integración pendiente

La implementación quedó en `T0P/validator/`; no se editó harness ni collector. Llamadas a añadir por la tarea de integración:

- `T0P/harness/telemetry_r11.py`: `_fit_clock_map()`, `_read_emon()`, `_derive_block()`, `_check()`.
- `T0P/collector/qualify_r11.py`: `telemetry_valid()`.

La API y ejemplo D13 están en `T0P/validator/adapter_example.py`; la CLI y reglas exactas, en `T0P/validator/VALIDATOR_SPEC.md`.

## Manifiestos SHA256

`T0P/telemetry/SHA256SUMS_telemetry_r6.txt` y `T0P/validator/SHA256SUMS_validator.txt` cubren todos los archivos de cada árbol, salvo el propio manifiesto por autorreferencia. No hay `__pycache__`, `.pyc` ni temporales.
