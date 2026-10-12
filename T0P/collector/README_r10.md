# Cualificador T0P R10

`qualify_r10.py` es la interfaz única. Acepta los JSONL y el plan verificado del harness, rechaza JSON no estricto y emite el estado de cualificación con la residencia siempre en `NOT_STARTED`. El modo SMOKE conserva el conteo de validez y nunca aplica umbrales.

```powershell
python T0P\collector\qualify_r10.py --self-test --self-test-output T0P\collector\logs_r10\synthetic_qa_r10.txt
python T0P\collector\qualify_r10.py --raw <captura>\raw.jsonl --summary <captura>\summary.jsonl --plan T0P\collector\plan.json --output <captura>\qualification.json
```

La primera orden cubre decisiones sintéticas y negativas JSON estrictas; la segunda es la integración SMOKE del runner. Ninguna crea `AUTHORIZED.json` ni habilita sesión oficial.
