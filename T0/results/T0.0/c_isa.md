# T0.0-c — Detección CPUID/ISA

Base: Roadmap §5.1 y AMB-05 cerrada en Roadmap §7; `T0.md` §3, T0.0-c. Se consultó CPUID/XGETBV en un logical processor por cada uno de los 16 núcleos API, con afinidad temporal. No se ejecutó ni comparó ningún kernel ISA.

| Bandera/estado | Observación en núcleos consultados |
|---|---|
| AVX2 | CPUID=true; XCR0 YMM habilitado=true |
| AVX-VNNI 256 | CPUID leaf 7 subleaf 1 EAX bit 4=true; YMM habilitado=true |
| AVX-512F | CPUID=false; XCR0 ZMM state=false |
| FMA | true |
| BMI1 | true |
| BMI2 | true |
| OSXSAVE / AVX | true / true |
| XGETBV(0) | `0x7` |
| RDTSCP | true |

Los 16 registros comparten vendor/family/model/stepping `GenuineIntel/6/183/1` y las mismas banderas mostradas. Resolución MD/22/AMB-05: `ISA_REF=escalar/correctness` (no compite), `ISA_BASE=AVX2`, `ISA_OPT=AVX-VNNI de 256 bits` disponible, `AVX-512=NOT_AVAILABLE` (no se emula). La comparación de ISA se reserva a T0.1. Comando exacto: `C:\Projects\OMEGA\Ejercise\T0\build\out\t0_probe.exe --isa` (`T0\logs\isa_cpuid_xgetbv.command.txt`); salida cruda íntegra: `T0\logs\isa_cpuid_xgetbv.stdout.txt`.
