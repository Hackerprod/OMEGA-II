# Frecuencia efectiva — decisión MD/25

Sol rechaza usar cadenas de sumas dependientes como medida absoluta de frecuencia (MD/25 §12). No se invocará ni reutilizará ese método. Sus fuentes, script de build y binarios locales fueron retirados; los logs de compilación permanecen como historial de que el prototipo no se ejecutó.

Estado para la campaña de optimización:

`EFFECTIVE_FREQUENCY = UNAVAILABLE`

Los catálogos históricos de VTune/SEP enumeran `CPU_CLK_UNHALTED.THREAD` y `CPU_CLK_UNHALTED.REF_TSC`, pero no se han recogido conteos bajo carga en esta fase de correctness. APERF/MPERF no aparece en los inventarios conservados. Solo se considerará una fuente confiable si VTune/SEP permite documentar y capturar los contadores como conteos; si no, se reporta throughput en MAC/s y frecuencia UNAVAILABLE. No es necesario para el gate D_read.
