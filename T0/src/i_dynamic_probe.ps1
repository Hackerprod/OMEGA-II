param(
    [Parameter(Mandatory=$true)][ValidateSet('P','E','MIXED')][string]$WorkerSet,
    [Parameter(Mandatory=$true)][ValidateRange(1,16)][int]$WorkerCount,
    [Parameter(Mandatory=$true)][ValidateRange(1,3600)][int]$DurationSeconds,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)

# Stage-2 controlled load capture; called serially by capture_stage2.py after the judge's authorization.
# It runs a controlled CPU Q4 load and queries Windows PDH Processor Performance counters.
# The helper's dynamic-load mode exits itself after DurationSeconds; this script never stops a process.

$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'build\out\t0_measure_harness.exe'
$topology = Join-Path $root 'results\T0.0\b_topologia.csv'
if (-not (Test-Path -LiteralPath $OutputDirectory)) { throw "OutputDirectory must already exist: $OutputDirectory" }
if (-not (Test-Path -LiteralPath $exe)) { throw "Measurement harness binary is not built: $exe" }
if (-not (Test-Path -LiteralPath $topology)) { throw "Topology CSV is missing: $topology" }
$topologyRows = Import-Csv -LiteralPath $topology
$availableWorkers = @($topologyRows | Where-Object { $WorkerSet -eq 'MIXED' -or $_.core_type -eq "$WorkerSet-core" } | Select-Object -ExpandProperty physical_core_api_key -Unique).Count
if ($WorkerCount -gt $availableWorkers) { throw "WorkerCount $WorkerCount exceeds $WorkerSet topology count $availableWorkers" }

$stdoutPath = Join-Path $OutputDirectory 'controlled_load.stdout.txt'
$stderrPath = Join-Path $OutputDirectory 'controlled_load.stderr.txt'
$process = Start-Process -FilePath $exe -ArgumentList @('--dynamic-load', $topology, $WorkerSet, [string]$WorkerCount, [string]$DurationSeconds) `
    -PassThru -NoNewWindow -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath

$counterPath = '\Processor Performance(*)\Processor Frequency'
$counterStatus = 'AVAILABLE'
$counterError = $null
$samples = @()
try {
    $samples = Get-Counter -Counter $counterPath -SampleInterval 1 -MaxSamples $DurationSeconds
} catch {
    $counterStatus = 'UNAVAILABLE'
    $counterError = $_.Exception.Message
}
$process.WaitForExit()
$process.Refresh()

# The queried allowed interfaces did not expose PL1/PL2, package temperature or power.
$counterSamples = @($samples | ForEach-Object { $_.CounterSamples })
$counterSamples | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'processor_frequency_pdh_raw.csv') -NoTypeInformation -Encoding UTF8
$logicalType = @{}
foreach ($row in $topologyRows) { $logicalType[[string]$row.logical_processor] = $row.core_type }
$frequencySummary = @()
foreach ($group in ($counterSamples | Group-Object InstanceName)) {
    $values = @($group.Group | ForEach-Object { [double]$_.CookedValue } | Sort-Object)
    if ($values.Count -eq 0) { continue }
    $logicalMatch = [regex]::Match([string]$group.Name, '(\d+)$')
    $coreType = if ($logicalMatch.Success -and $logicalType.ContainsKey($logicalMatch.Value)) { $logicalType[$logicalMatch.Value] } else { 'UNMAPPED' }
    $middle = [int][Math]::Floor($values.Count / 2)
    if (($values.Count % 2) -eq 1) { $median = $values[$middle] }
    else { $median = ($values[$middle - 1] + $values[$middle]) / 2.0 }
    $frequencySummary += [pscustomobject]@{
        InstanceName = $group.Name
        CoreType = $coreType
        Samples = $values.Count
        FrequencyEffectiveSustainedMedianMHz = $median
        MinimumMHz = $values[0]
        MaximumMHz = $values[$values.Count - 1]
    }
}
$frequencySummary | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'processor_frequency_summary.csv') -NoTypeInformation -Encoding UTF8
$typeSummary = @()
foreach ($group in ($counterSamples | Group-Object { $logicalMatch = [regex]::Match([string]$_.InstanceName, '(\d+)$'); if ($logicalMatch.Success -and $logicalType.ContainsKey($logicalMatch.Groups[1].Value)) { $logicalType[$logicalMatch.Groups[1].Value] } else { 'UNMAPPED' } })) {
    $values = @($group.Group | ForEach-Object { [double]$_.CookedValue } | Sort-Object)
    if ($values.Count -eq 0) { continue }
    $middle = [int][Math]::Floor($values.Count / 2)
    if (($values.Count % 2) -eq 1) { $median = $values[$middle] }
    else { $median = ($values[$middle - 1] + $values[$middle]) / 2.0 }
    $typeSummary += [pscustomobject]@{ CoreType = $group.Name; Samples = $values.Count; FrequencyEffectiveSustainedMedianMHz = $median; MinimumMHz = $values[0]; MaximumMHz = $values[$values.Count - 1] }
}
$typeSummary | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'processor_frequency_by_type.csv') -NoTypeInformation -Encoding UTF8
$record = [ordered]@{
    command = "`"$exe`" --dynamic-load `"$topology`" $WorkerSet $WorkerCount $DurationSeconds"
    duration_seconds_requested = $DurationSeconds
    worker_set = $WorkerSet
    worker_count = $WorkerCount
    process_id = $process.Id
    process_exit_code = $process.ExitCode
    frequency_counter = $counterPath
    frequency_counter_status = $counterStatus
    frequency_counter_error = $counterError
    frequency_summary_statistic = 'median of sampled CookedValue per PDH instance; raw samples are preserved separately'
    temperature_telemetry = 'UNAVAILABLE'
    power_telemetry = 'UNAVAILABLE'
    PL1 = 'UNAVAILABLE_WITH_ALLOWED_INTERFACES'
    PL2 = 'UNAVAILABLE_WITH_ALLOWED_INTERFACES'
}
$record | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'dynamic_condition_record.json') -Encoding UTF8
if ($process.ExitCode -ne 0) { exit $process.ExitCode }
