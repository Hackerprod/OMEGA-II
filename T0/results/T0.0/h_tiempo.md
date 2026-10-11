# T0.0-h — Temporizadores

Base: Roadmap §5.1; `T0.md` §3, T0.0-h. Solo microbenchmarks del temporizador, sin kernel/workload científico. Se fijó el hilo temporalmente a un LP representativo de cada núcleo según el mapa API; se consultaron los 16 núcleos físicos.

- QPC frequency: 10,000,000 Hz; resolución nominal `1/frequency = 100 ns`.
- 100,000 parejas de lecturas consecutivas: delta mínima y mediana observada 0 ticks; una pareja cabe dentro de un tick y no resuelve overhead por resta individual.
- Overhead amortizado en 21 repeticiones de 100,000 llamadas QPC por núcleo (elapsed QPC / número de llamadas; **incluye bucle/harness**, por lo que es una cota amortizada, no coste aislado de la API):

| Tipo de núcleo | Repeticiones/núcleos | Mediana ns/llamada amortizada | Rango entre núcleos |
|---|---:|---:|---:|
| P-core | 8 | 9.4695 ns | 9.468–9.471 ns |
| E-core | 8 | 14.8855 ns | 14.785–16.062 ns |

- RDTSCP está anunciado. 100,000 parejas con LFENCE alrededor, fijadas por núcleo: P-core mediana 21 ciclos (rango 21–21); E-core mediana 36 ciclos (rango 36–36). `TSC_AUX` fue consistente con el LP fijado. No se convierten ciclos a tiempo ni se infiere frecuencia sostenida.

Cada comando exacto está en su `T0\logs\timer_CORE_API_*_{Pcore,Ecore}.command.txt` (forma `C:\Projects\OMEGA\Ejercise\T0\build\out\t0_probe.exe --timer <group> <logical_processor>`); stdout/stderr crudos en archivos homónimos. Código y flags: `T0\src\t0_probe.cpp`, `T0\build\build_probe.cmd`.
