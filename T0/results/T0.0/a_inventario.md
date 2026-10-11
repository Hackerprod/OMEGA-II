# T0.0-a — Inventario de máquina

**Registro RAM conforme a decisión de Sol (MD/23):** `POPULATED_DIMM_COUNT=2`; `POPULATED_CHANNEL_COUNT=2`; `status=FIRMWARE_REPORTED`; gobierna SMBIOS/WMI firmware topology. `DIMM_0=Controller0-DIMM1, 16 GiB, DDR5-5200, DataWidth=64`; `DIMM_1=Controller1-DIMM1, 16 GiB, DDR5-5200, DataWidth=64`. Se conserva `SEP_MEMORY_TOPOLOGY=CONFLICTING_OBSERVATION`; no se reinterpreta SEP como autoridad física superior.

Base normativa: Roadmap §§2.2, 4.2, 4.3 y 5.1; decisiones vigentes MD/22–28; `T0.md` §§2–3, T0.0-a. El Roadmap manda. Cada observación cita su comando y salida cruda bajo `T0\logs\`; `COMMANDS.json` registra comando, exit code y hashes de stdout/stderr.

## Roadmap §2.2 — estado final de inventario

| Fila | Dato observado | Comando exacto (`.command.txt`) / salida cruda (`.stdout.txt`) | Resultado |
|---|---|---|---|
| CPU / identidad | WMI: `13th Gen Intel(R) Core(TM) i7-13700F`, GenuineIntel. CPUID por núcleo: GenuineIntel, family 6, model 183 (`0xB7`), stepping 1. MD/22–28 identifica la microarquitectura como Raptor Lake. | `powershell_platform_static_inventory.command.txt` / `.stdout.txt`; `isa_cpuid_xgetbv.command.txt` / `.stdout.txt`; `systeminfo_bare_metal_readonly.command.txt` / `.stdout.txt` | Identidad observada; coincide con el hardware gobernante del Roadmap y MD/22–28 |
| `N_P`, `N_E` | 8 P-cores físicos + 8 E-cores físicos = 16 núcleos físicos; 24 logical processors. | `topology_api.command.txt` (helper usa `GetLogicalProcessorInformationEx`) / `.stdout.txt`; `b_topologia.csv` | Observado, no se usó el orden numérico como premisa |
| `L2_P` | 8 relaciones L2 privadas de 2,097,152 B (2 MiB) cada una; cada relación cubre los dos LP SMT del P-core. | `topology_api.command.txt` / `.stdout.txt`; `sep_platform_info.command.txt` / `.stdout.txt` como comprobación descriptiva | 2 MiB por P-core observado |
| `L2_E` | 2 relaciones L2 de 4,194,304 B (4 MiB) cada una; cada una cubre cuatro E-cores/LP. | `topology_api.command.txt` / `.stdout.txt`; `sep_platform_info.command.txt` / `.stdout.txt` | 4 MiB por cluster de 4 E observados |
| `LLC` | 31,457,280 B (30 MiB) unificada; una relación compartida por los 24 LP. SEP la reporta como 30 MB. | `topology_api.command.txt` / `.stdout.txt`; `powershell_platform_static_inventory.command.txt` / `.stdout.txt` (`L3CacheSize=30720 KB`); `sep_platform_info.command.txt` / `.stdout.txt` | 30 MiB observados; la capacidad LLC útil no es salida de T0.0 |
| RAM | `POPULATED_DIMM_COUNT=2`; `POPULATED_CHANNEL_COUNT=2` (`FIRMWARE_REPORTED`). Dos módulos de 17,179,869,184 B cada uno, DDR5-5200, DataWidth=64; suma 32 GiB. | `powershell_win32_physical_memory_full.command.txt` / `.stdout.txt`; `powershell_win32_physical_memory_array_full.*`; `sep_platform_info_dimms.*` | Firmware WMI rige según MD/23; SEP queda `CONFLICTING_OBSERVATION` |
| ISA | AVX2=true, AVX-VNNI-256=true, AVX512F CPUID=false; detalles y demás bits en `c_isa.md`. | `isa_cpuid_xgetbv.command.txt` / `.stdout.txt` | Detectado en los 16 núcleos físicos |
| Bare metal / VM | Win32_ComputerSystem `HypervisorPresent=false`; CPUID hypervisor bit=false en las consultas por núcleo; `systeminfo` indica estación de trabajo independiente, ASUS. | `powershell_platform_static_inventory.command.txt` / `.stdout.txt`; `isa_cpuid_xgetbv.command.txt` / `.stdout.txt`; `systeminfo_bare_metal_readonly.command.txt` / `.stdout.txt` | Observaciones consistentes con host nativo |
| Contadores | Intel SEP user mode 5.61 / driver 5.61.22 / PAX 1.0.2 y VTune 2026.4. Se enumeraron candidatos IMC, L2 y LLC; la única tentativa acotada efectiva importó los perfiles SEP, pero no expuso APERF/MPERF ni THREAD/REF_TSC utilizables para conversión. PCM no está disponible en las rutas/registro consultados. | `g_contadores.md`; `phase1_raw/effective_frequency_attempt.json`; comandos y salidas `sep_*`, `vtune_*`, `powershell_counter_tools_readonly_availability.*` | `EFFECTIVE_FREQUENCY=UNAVAILABLE_WITH_QUALIFIED_TOOLS`; eventos generales enumerados, no validados para contabilidad fina |
| Relojes / Potencia | `EFFECTIVE_FREQUENCY=UNAVAILABLE_WITH_QUALIFIED_TOOLS` según el único intento SEP/VTune MD/28. `PL1/PL2=UNAVAILABLE_WITH_ALLOWED_INTERFACES`; `TEMPERATURE_TELEMETRY=UNAVAILABLE`; `POWER_TELEMETRY=UNAVAILABLE`. El plan activo observado en preflight fue High Performance, GUID `8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c`. | `phase1_raw/effective_frequency_attempt.json`; `i_condiciones.md`; `phase1_execution_status.json` y preflight de segmento 3; inventario de interfaces de potencia | Cerrado con estados explícitos de indisponibilidad; 2100 MHz PDH es nominal constante, no frecuencia efectiva |

## Esperado vs observado

| Roadmap §2.2 (solo esperado) | Observación medida | Comparación |
|---|---|---|
| 8 P + 8 E, 24 hilos | 8 P + 8 E físicos, 24 LP | coincide |
| `L2_P` 2 MiB | 2,097,152 B privado por P-core | coincide |
| `L2_E` 4 MiB compartida por cluster de 4 E | 4,194,304 B por grupo de cuatro E; dos grupos | coincide |
| `LLC` 30 MB | API 31,457,280 B (30 MiB); SEP lo etiqueta “30MB” | capacidad coincide; unidad SEP/API queda explícita |
| RAM DDR4 o DDR5; módulos/canales a verificar | 2 × 16 GiB DDR5-5200; 2 canales `FIRMWARE_REPORTED` por SMBIOS/WMI | inventario de firmware cerrado por MD/23; SEP discordante preservado |
| ISA AVX2/AVX-VNNI/AVX-512; esperado sin AVX-512 | AVX2 y VNNI-256 presentes; AVX512F ausente | AVX-512 coincide con esperado; VNNI se registra sin comparar rutas |
| Bare metal | HypervisorPresent=false, CPUID hypervisor=false, systeminfo workstation independiente | consistente con host nativo |

## Estado de cierre del inventario RAM

`Win32_PhysicalMemoryArray.MemoryDevices=4` se conserva como valor de slots y no se usa para contar canales. La discrepancia literal SEP vs SMBIOS/WMI queda preservada; según MD/23 gobierna el inventario firmware (`FIRMWARE_REPORTED`) sin reescribir el raw SEP. El número de canales queda registrado conforme a MD/23 (2 FIRMWARE_REPORTED). No se hizo medición de rendimiento, no se compararon rutas ISA y no se ejecutó carga en GPU.

## Anexos crudos de memoria (transcripción literal)

Los bytes crudos íntegros están en `T0\logs\powershell_win32_physical_memory_full.stdout.txt`, `powershell_win32_physical_memory_array_full.stdout.txt` y `sep_platform_info_dimms.stdout.txt`, con comandos exactos/exit codes en `T0\logs\COMMANDS.json` y los `.command.txt` correspondientes. Los bloques siguientes son una transcripción sin reinterpretación; la regla de registro indicada arriba es la instrucción explícita MD/23, no una adjudicación nueva a partir de estos bloques.

### Win32_PhysicalMemory — `Format-List *`

```text
Caption               : Memoria física
Description           : Memoria física
InstallDate           :
Name                  : Memoria física
Status                :
CreationClassName     : Win32_PhysicalMemory
Manufacturer          : Kingston
Model                 :
OtherIdentifyingInfo  :
PartNumber            : HP52U40S8MA-16FRAE
PoweredOn             :
SerialNumber          : 00080DD6
SKU                   :
Tag                   : Physical Memory 1
Version               :
HotSwappable          :
Removable             :
Replaceable           :
FormFactor            : 8
BankLabel             : BANK 0
Capacity              : 17179869184
DataWidth             : 64
InterleavePosition    : 1
MemoryType            : 0
PositionInRow         :
Speed                 : 5200
TotalWidth            : 64
Attributes            : 1
ConfiguredClockSpeed  : 5200
ConfiguredVoltage     : 1100
DeviceLocator         : Controller0-DIMM1
InterleaveDataDepth   : 2
MaxVoltage            : 1999
MinVoltage            : 1100
SMBIOSMemoryType      : 34
TypeDetail            : 128
PSComputerName        :
CimClass              : root/cimv2:Win32_PhysicalMemory
CimInstanceProperties : {Caption, Description, InstallDate, Name...}
CimSystemProperties   : Microsoft.Management.Infrastructure.CimSystemProperties

Caption               : Memoria física
Description           : Memoria física
InstallDate           :
Name                  : Memoria física
Status                :
CreationClassName     : Win32_PhysicalMemory
Manufacturer          : Kingston
Model                 :
OtherIdentifyingInfo  :
PartNumber            : HP52U40S8MA-16FRAE
PoweredOn             :
SerialNumber          : 00080E06
SKU                   :
Tag                   : Physical Memory 3
Version               :
HotSwappable          :
Removable             :
Replaceable           :
FormFactor            : 8
BankLabel             : BANK 0
Capacity              : 17179869184
DataWidth             : 64
InterleavePosition    : 2
MemoryType            : 0
PositionInRow         :
Speed                 : 5200
TotalWidth            : 64
Attributes            : 1
ConfiguredClockSpeed  : 5200
ConfiguredVoltage     : 1100
DeviceLocator         : Controller1-DIMM1
InterleaveDataDepth   : 2
MaxVoltage            : 1999
MinVoltage            : 1100
SMBIOSMemoryType      : 34
TypeDetail            : 128
PSComputerName        :
CimClass              : root/cimv2:Win32_PhysicalMemory
CimInstanceProperties : {Caption, Description, InstallDate, Name...}
CimSystemProperties   : Microsoft.Management.Infrastructure.CimSystemProperties
```

### Win32_PhysicalMemoryArray — `Format-List *`

```text
Status                :
Name                  : Matriz de memoria física
Replaceable           :
Location              : 3
Caption               : Matriz de memoria física
Description           : Matriz de memoria física
InstallDate           :
CreationClassName     : Win32_PhysicalMemoryArray
Manufacturer          :
Model                 :
OtherIdentifyingInfo  :
PartNumber             :
PoweredOn              :
SerialNumber            :
SKU                    :
Tag                   : Physical Memory Array 0
Version               :
Depth                 :
Height                :
HotSwappable          :
Removable             :
Weight                :
Width                 :
MaxCapacity           : 268435456
MaxCapacityEx         : 268435456
MemoryDevices         : 4
MemoryErrorCorrection : 3
Use                   : 3
PSComputerName        :
CimClass              : root/cimv2:Win32_PhysicalMemoryArray
CimInstanceProperties : {Caption, Description, InstallDate, Name...}
CimSystemProperties   : Microsoft.Management.Infrastructure.CimSystemProperties
```

### SEP — bloque `RAM Features` literal de `sep -platform-info`

```text
RAM Features:
    (Package/Memory Controller/Channel)
	(0/0/0) (Total Number of Ranks on this Channel: 2)
		 (Dimm0 Info: Capacity = 16 GB, Data Width = 64, Form Factor = DIMM, Memory Type = DDR5, Memory Type Detail = Synchronous, Speed = 5200MHz, Configured Speed = 5200MHz)
		 (Dimm1 Info: Capacity = 16 GB, Data Width = 64, Form Factor = DIMM, Memory Type = DDR5, Memory Type Detail = Synchronous, Speed = 5200MHz, Configured Speed = 5200MHz)
	(0/0/1) (Total Number of Ranks on this Channel: 2)
		 (Dimm0 Info: Empty)
		 (Dimm1 Info: Empty)
	(0/0/2) (Total Number of Ranks on this Channel: 2)
		 (Dimm0 Info: Empty)
		 (Dimm1 Info: Empty)
	(0/0/3) (Total Number of Ranks on this Channel: 2)
		 (Dimm0 Info: Empty)
		 (Dimm1 Info: Empty)
	(0/1/0) (Total Number of Ranks on this Channel: 2)
		 (Dimm0 Info: Empty)
		 (Dimm1 Info: Empty)
	(0/1/1) (Total Number of Ranks on this Channel: 2)
		 (Dimm0 Info: Empty)
		 (Dimm1 Info: Empty)
	(0/1/2) (Total Number of Ranks on this Channel: 2)
		 (Dimm0 Info: Empty)
		 (Dimm1 Info: Empty)
	(0/1/3) (Total Number of Ranks on this Channel: 2)
		 (Dimm0 Info: Empty)
		 (Dimm1 Info: Empty)

```
