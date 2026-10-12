$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$logs = Join-Path $root 'logs'
New-Item -ItemType Directory -Force -Path $logs | Out-Null
$emon = 'C:\Program Files (x86)\Intel\oneAPI\vtune\2026.4\bin64\emon.exe'
$utf8 = [System.Text.UTF8Encoding]::new($false)

(& "$env:WINDIR\System32\whoami.exe" /groups 2>&1 | Out-String) |
    Set-Content -LiteralPath (Join-Path $logs 'whoami_groups.txt') -Encoding utf8
(& "$env:WINDIR\System32\sc.exe" query sepdrv5 2>&1 | Out-String) |
    Set-Content -LiteralPath (Join-Path $logs 'sc_sepdrv5.txt') -Encoding utf8
(& "$env:WINDIR\System32\sc.exe" query sepdal 2>&1 | Out-String) |
    Set-Content -LiteralPath (Join-Path $logs 'sc_sepdal.txt') -Encoding utf8
(& "$env:WINDIR\System32\driverquery.exe" /v /fo csv 2>&1 |
    Select-String -Pattern 'sepdrv5|sepdal' | Out-String) |
    Set-Content -LiteralPath (Join-Path $logs 'driverquery_sep.txt') -Encoding utf8
(& $emon -v 2>&1 | Out-String) |
    Set-Content -LiteralPath (Join-Path $logs 'emon_version_topology.txt') -Encoding utf8
(& "$env:WINDIR\System32\wpr.exe" -profiles 2>&1 | Out-String) |
    Set-Content -LiteralPath (Join-Path $logs 'wpr_profiles.txt') -Encoding utf8
(& "$env:WINDIR\System32\wpr.exe" -pmcsources 2>&1 | Out-String) |
    Set-Content -LiteralPath (Join-Path $logs 'wpr_pmcsources.txt') -Encoding utf8
$xperf = Get-Command xperf.exe -ErrorAction SilentlyContinue | Select-Object Name,Source
$pmcsources = Get-Command pmcsources.exe -ErrorAction SilentlyContinue | Select-Object Name,Source
@($xperf) | ConvertTo-Json -Depth 2 | Set-Content -LiteralPath (Join-Path $logs 'xperf_where.json') -Encoding utf8
@($pmcsources) | ConvertTo-Json -Depth 2 | Set-Content -LiteralPath (Join-Path $logs 'pmcsources_where.json') -Encoding utf8
if (-not $xperf) { 'No encontrado en PATH: xperf.exe (Windows Performance Toolkit).' | Set-Content -LiteralPath (Join-Path $logs 'xperf_where.json') -Encoding utf8 }
if (-not $pmcsources) { 'No encontrado en PATH: pmcsources.exe.' | Set-Content -LiteralPath (Join-Path $logs 'pmcsources_where.json') -Encoding utf8 }
(& "$env:WINDIR\System32\powercfg.exe" /getactivescheme 2>&1 | Out-String) |
    Set-Content -LiteralPath (Join-Path $logs 'powercfg_active.txt') -Encoding utf8
(& "$env:WINDIR\System32\powercfg.exe" /q SCHEME_CURRENT SUB_PROCESSOR 2>&1 | Out-String) |
    Set-Content -LiteralPath (Join-Path $logs 'powercfg_processor_settings.txt') -Encoding utf8

$cpu = Get-CimInstance Win32_Processor | Select-Object Name,Manufacturer,ProcessorId,Architecture,NumberOfCores,NumberOfLogicalProcessors,MaxClockSpeed
$os = Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber
$thermal = @(Get-CimInstance -Namespace root\wmi -ClassName MSAcpi_ThermalZoneTemperature -ErrorAction SilentlyContinue |
    Select-Object InstanceName,CurrentTemperature,Active,Passive)
$result = [pscustomobject]@{ CPU=$cpu; OS=$os; ACPIThermalZones=$thermal; ActivePlan=(Get-Content (Join-Path $logs 'powercfg_active.txt') -Raw).Trim() }
$result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $logs 'wmi_cim_snapshot.json') -Encoding utf8

$powerNamespace = 'root\cimv2\power'
$activeWmiPlan = Get-CimInstance -Namespace $powerNamespace -ClassName Win32_PowerPlan |
    Where-Object IsActive | Select-Object ElementName,IsActive,InstanceID,Description
$activeGuid = ($activeWmiPlan.InstanceID -split '\\')[-1]
$processorSettingGuids = @{
    '893dee8e-2bef-41e0-89c6-b55d0929964c' = 'MinProcessorState'
    'bc5038f7-23e0-4960-96da-33abaf5935ec' = 'MaxProcessorState'
    '943c8cb6-6f93-4227-ad87-e9a3feec08d1' = 'CPOverUtil'
}
$wmiPowerIndices = Get-CimInstance -Namespace $powerNamespace -ClassName Win32_PowerSettingDataIndex |
    Where-Object {$_.InstanceID -like "*$activeGuid*"} |
    ForEach-Object {
        if ($_.InstanceID -match '\\(AC|DC)\\\{([0-9a-f-]+)\}$' -and $processorSettingGuids.ContainsKey($Matches[2].ToLowerInvariant())) {
            [pscustomobject]@{Setting=$processorSettingGuids[$Matches[2].ToLowerInvariant()]; PowerSource=$Matches[1]; Index=$_.SettingIndexValue}
        }
    }
[pscustomobject]@{ActivePlan=$activeWmiPlan; ProcessorSettings=@($wmiPowerIndices)} |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $logs 'wmi_power_plan_settings.json') -Encoding utf8

$infoObject = 'Informaci' + [char]0x00F3 + 'n del procesador'
$counterSets = @($infoObject,'Processor Performance','Procesador')
$counterInfo = foreach ($setName in $counterSets) {
    $set = Get-Counter -ListSet $setName -ErrorAction SilentlyContinue
    if ($set) { [pscustomobject]@{Name=$set.CounterSetName; Description=$set.Description; Counters=@($set.Counter)} }
}
$counterInfo | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $logs 'windows_counter_inventory.json') -Encoding utf8

$installed = Get-ItemProperty 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*','HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*' -ErrorAction SilentlyContinue |
    Where-Object {$_.DisplayName -match 'VTune|EMON|Intel.*XTU|Dynamic Tuning|PCM'} |
    Select-Object DisplayName,DisplayVersion,InstallLocation
$installed | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $logs 'intel_tools_inventory.json') -Encoding utf8

@(
    'Hora local de recolección: ' + (Get-Date -Format o)
    'whoami: see whoami_groups.txt; Windows token is elevated when Mandatory Label is High and Administrators is enabled.'
    'VTune/EMON: see emon_version_topology.txt; EMON -v reports public version, CPUID and P/E core mapping.'
    'Drivers: see sc_sepdrv5.txt, sc_sepdal.txt and driverquery_sep.txt.'
    'ETW/PMC: see wpr_profiles.txt, wpr_pmcsources.txt, xperf_where.json and pmcsources_where.json.'
    'Power plan: see powercfg_active.txt and powercfg_processor_settings.txt; commands are query-only.'
    'WMI power-plan/settings: see wmi_power_plan_settings.json.'
    'Thermal WMI: see wmi_cim_snapshot.json; ACPI thermal-zone readings are not CPU package/core sensors.'
    'Tools inventory: see intel_tools_inventory.json.'
) | Set-Content -LiteralPath (Join-Path $logs 'evidence_index.txt') -Encoding utf8
