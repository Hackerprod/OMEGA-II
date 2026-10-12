# Reader telemetry r6 — helper R1

Este reader añade una carga nativa acotada para D2-INST_RETIRED. No ejecuta OMEGA, ggml, Torch ni código de aplicación de usuario: solo `known_retired_loop` y workers nativos. El caller valida en el helper el retorno de `N_loop=200.000.000.000` exactamente una vez en cada LP P seleccionado `0,2,…,14`, concurrentemente. El mapa y el hilo controlador van fijados a LP16, fuera de los ocho workers.

## Bucle y afinidad

La correspondencia guardada por EMON `-M` es P0 Core0 LP0/1, P1 Core4 LP2/3, P2 Core8 LP4/5, P3 Core12 LP6/7, P4 Core16 LP8/9, P5 Core20 LP10/11, P6 Core24 LP12/13 y P7 Core28 LP14/15.

`known_loop_r6.asm` conserva el método gobernante. `known_loop_r6_disassembly.txt` confirma:

```text
mov rax,rcx        ; setup
xor rdx,rdx        ; setup
loop_start:
add rdx,1          ; 1 instrucción por iteración
dec rax            ; 1
jne loop_start     ; 1
mov rax,rdx        ; cierre
ret                ; cierre
```

Por tanto `I0=3N+4=600000000004`. Cada worker fija una máscara singleton, verifica grupo/LP efectivo antes de abrir la barrera, registra LP y QPC al entrar/salir, exige retorno `N`, misma afinidad/LP y duración real ≥20 s. N nunca se adapta. El reporte y mapa son archivos `CREATE_NEW`; mapa QPC/UTC se muestrea durante toda la carga. La medición DUMMY r6 duró 39,3457–40,0100 s por worker, cumplió afinidad y devolvió N en los ocho LP.

## Compilación y smoke nativo

```powershell
cmd /c T0P\telemetry\reader\build_inst_cli_helper_r6.cmd
```

El comando terminó con código 0. `vcvars64.bat` mostró el aviso no fatal `"vswhere.exe" no se reconoce`; MASM, `cl /O2` y `dumpbin /DISASM` completaron. El ejecutable se corrió en modo DUMMY, sin EMON; los resultados inmutables y hashes están en `telemetry/logs/inst_cli_r6_dummy_session/attempt-002/`.

El runner acepta dos rutas nuevas: `inst_cli_helper_r6.exe <helper_report.csv> <helper_clock_map.csv>`. No inicia EMON ni decide/reintenta capturas. Cada intento R1 futuro debe tener carpeta D13 nueva. Una captura real requeriría EMON lanzando este helper como aplicación para que `-t 0` termine a la salida:

```text
emon -t 0 -C "INST_RETIRED.ANY" -m -c -u -f <emon_raw_nuevo> <inst_cli_helper_r6.exe> <helper_report_nuevo.csv> <helper_clock_map_nuevo.csv>
```

`telemetry/logs/emon_help.txt` documenta el formato `emon [opciones] -C "event-list" [application-command-line]`; con `-t 0` y aplicación la colección dura hasta que sale la aplicación. `-f` crea un archivo, `-F` añade. El preflight local `emon --dry-run -m -C "INST_RETIRED.ANY"` devolvió únicamente `Event Set 0 / INST_RETIRED.ANY` (Fixed Counter 0, `0x309`), por tanto un evento y un solo grupo, sin multiplexación configurada.

La llamada real no se ejecutó: EMON habilita/lee el PMU mediante registros de control de hardware y la instrucción de esta tarea prohíbe escribir MSR. Las cinco capturas, sus ocho conteos EMON por captura y el margen del 1 % quedan pendientes con estado `HOLD_INSTRUMENTATION`; no se sustituyen por el resultado de la carga DUMMY.
