# QA de referencia ggml (sin cronometraje)

20 celdas canónicas (5 fixtures × M={1,4,8,16}); cada archivo fue contrastado con el generador antes de uso. Cada celda se ejecutó por flat y repack. `finite=1` en las 40 filas. `tolerance_pass` aplica MD/35 `1e-5 + 1e-5*|y_ref|`; no adjudica la tolerancia gobernante. Error normalizado = max |y−y_ref| / Σ_f |x_f·w_f|.

| Fixture | M | Ruta / función | Abs. máx | Rel. máx | Normalizado máx | Finito | Pasa MD/35 | Fallos |
|---|---:|---|---:|---:|---:|:---:|:---:|---:|
| uniform | 1 | flat / ggml_mul_mat | 0.00244140625 | 0.000340308592393 | 2.62501842577e-08 | 1 | 0 | 5 |
| uniform | 1 | repack / ggml_gemv_q4_0_8x8_q8_0 | 0.001953125 | 3.72565042021e-05 | 3.13231326981e-08 | 1 | 0 | 2 |
| uniform | 4 | flat / ggml_mul_mat | 0.001953125 | 0.000390887497791 | 3.20566692062e-08 | 1 | 0 | 26 |
| uniform | 4 | repack / ggml_gemm_q4_0_8x8_q8_0 | 0.0029296875 | 0.000253396471459 | 4.53676264532e-08 | 1 | 0 | 14 |
| uniform | 8 | flat / ggml_mul_mat | 0.001953125 | 0.000443618448997 | 2.94498647105e-08 | 1 | 0 | 42 |
| uniform | 8 | repack / ggml_gemm_q4_0_8x8_q8_0 | 0.0029296875 | 0.000248607906045 | 3.44946555579e-08 | 1 | 0 | 13 |
| uniform | 16 | flat / ggml_mul_mat | 0.0029296875 | 0.00171745451764 | 3.47285866768e-08 | 1 | 0 | 86 |
| uniform | 16 | repack / ggml_gemm_q4_0_8x8_q8_0 | 0.00390625 | 0.000798016259318 | 4.19120899474e-08 | 1 | 0 | 41 |
| mixed_cancellation | 1 | flat / ggml_mul_mat | 0 | 0 | 0 | 1 | 1 | 0 |
| mixed_cancellation | 1 | repack / ggml_gemv_q4_0_8x8_q8_0 | 0 | 0 | 0 | 1 | 1 | 0 |
| mixed_cancellation | 4 | flat / ggml_mul_mat | 0 | 0 | 0 | 1 | 1 | 0 |
| mixed_cancellation | 4 | repack / ggml_gemm_q4_0_8x8_q8_0 | 0 | 0 | 0 | 1 | 1 | 0 |
| mixed_cancellation | 8 | flat / ggml_mul_mat | 0 | 0 | 0 | 1 | 1 | 0 |
| mixed_cancellation | 8 | repack / ggml_gemm_q4_0_8x8_q8_0 | 0 | 0 | 0 | 1 | 1 | 0 |
| mixed_cancellation | 16 | flat / ggml_mul_mat | 0 | 0 | 0 | 1 | 1 | 0 |
| mixed_cancellation | 16 | repack / ggml_gemm_q4_0_8x8_q8_0 | 0 | 0 | 0 | 1 | 1 | 0 |
| varied_scales | 1 | flat / ggml_mul_mat | 0.0029296875 | 9.36908673724e-05 | 2.89373700316e-08 | 1 | 0 | 3 |
| varied_scales | 1 | repack / ggml_gemv_q4_0_8x8_q8_0 | 0.001953125 | 1.11598540083e-05 | 2.16783228469e-08 | 1 | 0 | 1 |
| varied_scales | 4 | flat / ggml_mul_mat | 0.0029296875 | 0.000210406125708 | 2.86142686797e-08 | 1 | 0 | 21 |
| varied_scales | 4 | repack / ggml_gemm_q4_0_8x8_q8_0 | 0.00390625 | 7.89020408832e-05 | 4.16780029377e-08 | 1 | 0 | 9 |
| varied_scales | 8 | flat / ggml_mul_mat | 0.00390625 | 0.00163596784705 | 3.34579148944e-08 | 1 | 0 | 27 |
| varied_scales | 8 | repack / ggml_gemm_q4_0_8x8_q8_0 | 0.005859375 | 0.00012707194062 | 5.4346809851e-08 | 1 | 0 | 8 |
| varied_scales | 16 | flat / ggml_mul_mat | 0.00390625 | 0.0003298468167 | 3.92348094805e-08 | 1 | 0 | 74 |
| varied_scales | 16 | repack / ggml_gemm_q4_0_8x8_q8_0 | 0.005859375 | 0.000442690900585 | 5.11635788665e-08 | 1 | 0 | 27 |
| sub127 | 1 | flat / ggml_mul_mat | 0.00048828125 | 0.000292499307037 | 2.52999560809e-08 | 1 | 0 | 3 |
| sub127 | 1 | repack / ggml_gemv_q4_0_8x8_q8_0 | 0.00048828125 | 0.000117918568805 | 2.20630603931e-08 | 1 | 0 | 2 |
| sub127 | 4 | flat / ggml_mul_mat | 0.0009765625 | 0.000256879932976 | 3.49399894479e-08 | 1 | 0 | 15 |
| sub127 | 4 | repack / ggml_gemm_q4_0_8x8_q8_0 | 0.0009765625 | 6.80781848254e-05 | 3.76957641658e-08 | 1 | 0 | 6 |
| sub127 | 8 | flat / ggml_mul_mat | 0.0009765625 | 0.00101565840427 | 3.74899752065e-08 | 1 | 0 | 36 |
| sub127 | 8 | repack / ggml_gemm_q4_0_8x8_q8_0 | 0.000732421875 | 0.000538564470834 | 3.32988257894e-08 | 1 | 0 | 16 |
| sub127 | 16 | flat / ggml_mul_mat | 0.000732421875 | 0.000377981635554 | 3.11049755583e-08 | 1 | 0 | 65 |
| sub127 | 16 | repack / ggml_gemm_q4_0_8x8_q8_0 | 0.00146484375 | 0.000204775293152 | 4.97662541739e-08 | 1 | 0 | 22 |
| extremes | 1 | flat / ggml_mul_mat | 0.0625 | 2.18701947082e-07 | 2.18701941672e-07 | 1 | 1 | 0 |
| extremes | 1 | repack / ggml_gemv_q4_0_8x8_q8_0 | 0.0625 | 2.18701947082e-07 | 2.18701941672e-07 | 1 | 1 | 0 |
| extremes | 4 | flat / ggml_mul_mat | 0.0625 | 2.24345578339e-07 | 2.24345590027e-07 | 1 | 1 | 0 |
| extremes | 4 | repack / ggml_gemm_q4_0_8x8_q8_0 | 0.0625 | 2.24345578339e-07 | 2.24345590027e-07 | 1 | 1 | 0 |
| extremes | 8 | flat / ggml_mul_mat | 0.0625 | 2.34884423945e-07 | 2.34884434609e-07 | 1 | 1 | 0 |
| extremes | 8 | repack / ggml_gemm_q4_0_8x8_q8_0 | 0.0625 | 2.34884423945e-07 | 2.34884434609e-07 | 1 | 1 | 0 |
| extremes | 16 | flat / ggml_mul_mat | 0.0625 | 2.29521806236e-07 | 2.29521817129e-07 | 1 | 1 | 0 |
| extremes | 16 | repack / ggml_gemm_q4_0_8x8_q8_0 | 0.0625 | 2.29521806236e-07 | 2.29521817129e-07 | 1 | 1 | 0 |

## Evidencia y límites

A: `ggml_mul_mat`, n_threads=1. Se leyó el callback de `ggml_get_type_traits_cpu(Q4_0)` en ejecución y se registraron dirección/RVA dentro de `ggml-cpu.dll`. El DLL no exporta el símbolo y no hay PDB; identidad runtime limitada, asignación nominal basada en `ggml-cpu.c`.
B: llamada directa a funciones upstream enlazadas desde `arch/x86/repack.cpp`: GEMV para M=1 y GEMM para M=4,8,16. Direcciones/ejecución por celda en stdout. Aritmética intacta.
Q4 reconstruye exactamente códigos y escalas FP16 tras interleave/XOR 0x8888888888888888; Q8 reconstruye exactamente INT8 y escalas FP16 en block_q8_0x4. Sin recuantización ni Q8→F32→Q8. La referencia calcula dot INT32 por grupo; los productos internos del kernel no se capturan individualmente.

## Módulos enumerados por EnumProcessModules

- C:\Projects\OMEGA\Ejercise\T0P\ref_ggml\adapter\adapter.exe size=385024 sha256=d95fa549b90152406a69e1fbcd6e60c179ab5f06cb8791fef9e2b770a7c857f9
- C:\WINDOWS\SYSTEM32\ntdll.dll size=2519040 sha256=9e35cbb412da1af9c01180a942989e0b45d6a40375052535e3631a473a53c3e4
- C:\WINDOWS\System32\KERNEL32.DLL size=823296 sha256=4f84d975b80158f842426d6c609bf635294e490c0128416a1b5172e6f0a3e45b
- C:\WINDOWS\System32\KERNELBASE.dll size=4124672 sha256=c7a7123a06c728989606c8caba989e68a59180aa8eb8bc712eebb3d59db6e3a1
- C:\WINDOWS\System32\ucrtbase.dll size=1355776 sha256=c2ed69b4a1bb9432c75134e813c70539e27c8c1fe96a784d9be78c874e93d9d5
- C:\Projects\OMEGA\Ejercise\T0P\ref_ggml\build_llama\bin\ggml-cpu.dll size=1658880 sha256=cfce3924a4de5ef82317089937edc1a4ae494515af1f1d9ee8ef05f6e921ba23
- C:\WINDOWS\System32\ADVAPI32.dll size=737280 sha256=39a735df6720821ad42ef5e2b4753b3f18c8b35297064062193399bbee97db47
- C:\WINDOWS\System32\msvcrt.dll size=692224 sha256=e87671c629f8ba6483336a5fe3814ddc642c5bbd3ca6ab9097dc1369fb34db7f
- C:\WINDOWS\System32\sechost.dll size=679936 sha256=32881e3ea52060a84c1e9cd8faa728acf96d1d1229e518a267fbe0bf6b7e8469
- C:\WINDOWS\System32\RPCRT4.dll size=1146880 sha256=478667ffc376c9725af56ede8fc56f68d4a4804b92afc2977187e185525fb393
- C:\Projects\OMEGA\Ejercise\T0P\ref_ggml\build_llama\bin\ggml-base.dll size=696320 sha256=8ac75aca8cd5a716bbbec7a171fea55f0a115315a8afb0dc499c37338a29bbc4
- C:\WINDOWS\SYSTEM32\bcrypt.dll size=172032 sha256=9ee5d54e9fbd795611322dd040e9978508c09091a6fdda40ea6f563e066da3ce
- C:\WINDOWS\SYSTEM32\MSVCP140.dll size=561152 sha256=0f885b509a685d2bbfa652fed26b5fb31d88fbdab0a978c641d1c7b8aa460aa9
- C:\WINDOWS\SYSTEM32\VCRUNTIME140.dll size=122880 sha256=d5e4d9a3e835fa679450145d6a7d94e36573a509317111904d9b3712c30d9066
- C:\WINDOWS\SYSTEM32\VCRUNTIME140_1.dll size=49152 sha256=1f2d41c4aa5db0bc33ebf7b66d72943a817d7ce6cbe880502a9403823633093f
- C:\WINDOWS\SYSTEM32\VCOMP140.DLL size=196608 sha256=55aba23cdcd6484fbb06f4155b8ca75adfce7a881f10afd0c49457165e677164
- C:\WINDOWS\System32\bcryptprimitives.dll size=675840 sha256=a2169ce3ad4b7cb060d75519b1b078f52ed79a8aa20d416aaec2762875be55f8
