# Harness R11

La captura de residencia reducida se ejecuta por collector; el harness emite A/B/C, ocho shards, t_app por aplicación, fases fuera del kernel y comparación de salida bit a bit.

```powershell
cmd /c call T0P\harness\build_r11.cmd
python T0P\collector\official_run_r11.py --smoke-real
```

El smoke cubre cinco condiciones y N=3. Los crudos se guardan en `logs_r11` y se etiquetan `SMOKE_ONLY`; nunca son resultado científico ni autorización oficial. Consulte `HARNESS_SPEC_r11.md` y `T0P\collector\OFFICIAL_RUN_r11.md`.
