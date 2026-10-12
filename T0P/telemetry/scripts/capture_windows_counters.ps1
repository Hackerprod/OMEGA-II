param([ValidateRange(2,5)][int]$CaptureSeconds = 3)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$logs = Join-Path $root 'logs'
New-Item -ItemType Directory -Force -Path $logs | Out-Null
$rawPath = Join-Path $logs 'windows_counters_idle_3s.csv'
$infoObject = 'Informaci' + [char]0x00F3 + 'n del procesador'
$infoPerformance = '\' + $infoObject + '(*)\% de rendimiento del procesador'
$infoPrivileged = '\' + $infoObject + '(*)\% de utilidad privilegiada'
$paths = @(
    $infoPerformance,
    $infoPrivileged,
    '\Processor Performance(*)\Processor Frequency',
    '\Processor Performance(*)\% of Maximum Frequency',
    '\Processor Performance(*)\Processor State Flags'
)
$samples = Get-Counter -Counter $paths -SampleInterval 1 -MaxSamples ($CaptureSeconds + 1)
$samples.CounterSamples |
    Select-Object Timestamp,Path,InstanceName,CookedValue,Status,RawValue,SecondValue |
    Export-Csv -LiteralPath $rawPath -NoTypeInformation -Encoding utf8
$metaPath = Join-Path $logs 'windows_counters_idle_3s.txt'
@(
    'Origen: Get-Counter / PDH, Windows 11 Enterprise LTSC 10.0.26100.'
    'Capture: 4 snapshots en 3 s (Get-Counter limita SampleInterval a >=1 s).'
    'Processor Performance\Processor Frequency y % of Maximum Frequency: contadores PPM/Perflib; no acreditados como lectura directa APERF/MPERF.'
    'Processor Information\% de rendimiento del procesador: métrica de rendimiento/utilidad normalizada del SO; no asumir identidad directa con APERF/MPERF.'
    'Processor State Flags: mapa de flags, no incluye garantías de PL1/PL2 ni de cada flag térmico solicitado.'
) | Set-Content -LiteralPath $metaPath -Encoding utf8
Get-Content -LiteralPath $metaPath
