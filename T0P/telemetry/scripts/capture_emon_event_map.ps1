param(
    [ValidateRange(2,5)][int]$CaptureSeconds = 3,
    [ValidateRange(10,100)][int]$SampleMilliseconds = 10
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$logs = Join-Path $root 'logs'
New-Item -ItemType Directory -Force -Path $logs | Out-Null
$emon = 'C:\Program Files (x86)\Intel\oneAPI\vtune\2026.4\bin64\emon.exe'
if (-not (Test-Path -LiteralPath $emon)) { throw "EMON no encontrado: $emon" }
$events = @(
    'CPU_CLK_UNHALTED.THREAD',
    'CPU_CLK_UNHALTED.REF_TSC',
    'L2_RQSTS.DEMAND_DATA_RD_MISS',
    'L2_RQSTS.ALL_DEMAND_DATA_RD',
    'OFFCORE_REQUESTS.L3_MISS_DEMAND_DATA_RD',
    'OFFCORE_REQUESTS.DEMAND_DATA_RD'
)
$groups = [int](1000 * $CaptureSeconds / $SampleMilliseconds)
$interval = '{0:0.000}' -f ($SampleMilliseconds / 1000.0)
$rawPath = Join-Path $logs 'emon_event_map_idle_3s.txt'
$args = @(
    '-C',($events -join ','),'-t',$interval,'-l',"$groups",
    "$env:WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe",
    '-NoProfile','-Command',"Start-Sleep -Seconds $CaptureSeconds"
)
$raw = & $emon @args 2>&1
$exitCode = $LASTEXITCODE
$raw | Set-Content -LiteralPath $rawPath -Encoding utf8
$text = Get-Content -LiteralPath $rawPath -Raw
$actual = ([regex]::Matches($text,'(?m)^CPU_CLK_UNHALTED\.THREAD\t')).Count
$dropped = 0
if ($text -match '(?im)^Total\s+(\d+) data groups discarded due to time discrepancy') { $dropped = [int]$Matches[1] }
$summary = @(
    "Command: emon -C $($events -join ',') -t $interval -l $groups powershell.exe Start-Sleep -Seconds $CaptureSeconds"
    "Expected groups: $groups; groups in output: $actual; dropped by EMON: $dropped; exit code: $exitCode"
    'This file is a raw system-wide read-only capture. EMON reports per-OS-LP counts; P-core events are N/A on E-core LPs.'
    'Time-discrepancy drops lack position timestamps; sample-count ratio is not temporal coverage and maximum gaps are not inferable.'
    'Counters are event deltas. EMON does not expose initial/final cumulative values or counter widths, so rollover is not observable.'
)
$summary | Set-Content -LiteralPath (Join-Path $logs 'emon_event_map_capture_summary.txt') -Encoding utf8
$summary
