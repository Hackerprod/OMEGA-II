# Collector S-19 R11

Plan, A6 estricto, derivador, orquestación sintética y smoke real están en `official_run_r11.py`.

```powershell
python T0P\collector\official_run_r11.py --plan-check
python T0P\collector\official_run_r11.py --test-orchestrator
python T0P\collector\official_run_r11.py --auth-self-test
python T0P\collector\official_run_r11.py --dry-accept-fixture T0P\collector\test_r11\authorized_fixture_valid.json
python T0P\collector\official_run_r11.py --smoke-real
python T0P\collector\official_run_r11.py --official
```

La ruta oficial permanece cerrada sin autorización A6 real y certificado lector S-20. Las capturas SMOKE están en `logs_r11`; la captura espejo del harness está en `T0P\harness\logs_r11`. No se actualizan plan ni manifiestos base.
