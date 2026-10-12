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
    'MEM_LOAD_RETIRED.L2_MISS',
    'MEM_LOAD_RETIRED.L2_HIT',
    'MEM_LOAD_RETIRED.L3_MISS',
    'MEM_LOAD_RETIRED.L3_HIT'
)
$expected = [int](1000 * $CaptureSeconds / $SampleMilliseconds)
$interval = ('{0:0.000}' -f ($SampleMilliseconds / 1000.0))
$rawPath = Join-Path $logs 'emon_idle_3s.txt'
$csvPath = Join-Path $logs 'emon_effective_samples.csv'
$jsonPath = Join-Path $logs 'emon_reader_report.json'
$sleepSeconds = $CaptureSeconds
$args = @(
    '-C', ($events -join ','), '-t', $interval, '-l', "$expected",
    "$env:WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe",
    '-NoProfile', '-Command', "Start-Sleep -Seconds $sleepSeconds"
)

$raw = & $emon @args 2>&1
$exitCode = $LASTEXITCODE
$raw | Set-Content -LiteralPath $rawPath -Encoding utf8
$emonInfo = & $emon -v 2>&1 | Out-String
$referenceMHz = 0.0
if ($emonInfo -match '(?im)^TSC Freq\s+\.+\s+([\d.]+)\s+MHz') {
    $referenceMHz = [double]::Parse($Matches[1],[Globalization.CultureInfo]::InvariantCulture)
}
if ($referenceMHz -le 0) { throw 'EMON -v no informa una frecuencia TSC de referencia.' }
$content = Get-Content -LiteralPath $rawPath -Raw
$discarded = 0
if ($content -match '(?im)^Total\s+(\d+) data groups discarded due to time discrepancy') {
    $discarded = [int]$Matches[1]
}

$records = [System.Collections.Generic.List[object]]::new()
$groups = [regex]::Split($content, '(?m)^==========\s*$')
$index = 0
foreach ($group in $groups) {
    $threadMatch = [regex]::Match($group, '(?m)^CPU_CLK_UNHALTED\.THREAD\t([^\r\n]+)')
    $refMatch = [regex]::Match($group, '(?m)^CPU_CLK_UNHALTED\.REF_TSC\t([^\r\n]+)')
    if (-not ($threadMatch.Success -and $refMatch.Success)) { continue }
    $index++
    $threadValues = @($threadMatch.Groups[1].Value -split "`t" | ForEach-Object { $_.Trim() })
    $refValues = @($refMatch.Groups[1].Value -split "`t" | ForEach-Object { $_.Trim() })
    # EMON puts a sample-time field first; the following 24 values map to OS LP 0..23.
    if ($threadValues.Count -lt 25 -or $refValues.Count -lt 25) { continue }
    $durationTicks = [double]($threadValues[0] -replace ',', '')
    $durationMs = 1000.0 * $durationTicks / ($referenceMHz * 1000000.0)
    for ($lp = 0; $lp -lt 16; $lp++) {
        $cyclesText = $threadValues[$lp + 1]
        $refText = $refValues[$lp + 1]
        if ($cyclesText -notmatch '^\d[\d,]*$' -or $refText -notmatch '^\d[\d,]*$') { continue }
        $cycles = [double]($cyclesText -replace ',', '')
        $reference = [double]($refText -replace ',', '')
        if ($reference -le 0) { continue }
        $records.Add([pscustomobject]@{
            SampleIndex = $index
            LogicalProcessor = $lp
            PcoreIndex = [int][Math]::Floor($lp / 2)
            ReportedSampleDurationMs = [Math]::Round($durationMs,3)
            CyclesEffectiveDelta = [long]$cycles
            ReferenceCyclesDelta = [long]$reference
            ReferenceFrequencyMHz = $referenceMHz
            FActiveMHz = [Math]::Round($referenceMHz * $cycles / $reference, 3)
        })
    }
}
$records | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding utf8

$perLp = for ($lp = 0; $lp -lt 16; $lp++) {
    $r = @($records | Where-Object LogicalProcessor -eq $lp)
    $values = @($r | ForEach-Object FActiveMHz)
    $valid = $values.Count
    [pscustomobject]@{
        LogicalProcessor = $lp
        PcoreIndex = [int][Math]::Floor($lp / 2)
        ExpectedSamples = $expected
        ValidSamples = $valid
        SampleCoveragePct = [Math]::Round(100.0 * $valid / $expected, 2)
        FActiveMeanMHz = if ($valid) { [Math]::Round(($values | Measure-Object -Average).Average, 2) } else { $null }
        FActiveMinMHz = if ($valid) { [Math]::Round(($values | Measure-Object -Minimum).Minimum, 2) } else { $null }
        FActiveMaxMHz = if ($valid) { [Math]::Round(($values | Measure-Object -Maximum).Maximum, 2) } else { $null }
        MaxGap = 'NO_CALCULABLE: faltan timestamps; los grupos descartados no llevan marca de posición'
        Monotonicity = 'NO_APLICA: cada fila es un delta de intervalo, no snapshot acumulativo'
        Rollover = 'NO_OBSERVABLE: no expone anchura ni snapshots crudos del contador'
    }
}
$summary = [pscustomobject]@{
    Tool = 'Intel EMON V11.61 / VTune 2026.4.0'
    CaptureSecondsRequested = $CaptureSeconds
    SamplePeriodMillisecondsRequested = $SampleMilliseconds
    ExpectedGroups = $expected
    GroupsParsed = $index
    GroupsDiscardedByEmon = $discarded
    SampleCountCoveragePct = [Math]::Round(100.0 * $index / $expected,2)
    EmOnExitCode = $exitCode
    ReferenceFrequencyMHz = $referenceMHz
    FormulaMHz = "$referenceMHz * delta(CPU_CLK_UNHALTED.THREAD) / delta(CPU_CLK_UNHALTED.REF_TSC)"
    AcceptedWindowMeanMs = if ($records.Count) { [Math]::Round((($records | Where-Object LogicalProcessor -eq 0 | Measure-Object ReportedSampleDurationMs -Average).Average),3) } else { $null }
    AcceptedWindowMinMs = if ($records.Count) { [Math]::Round((($records | Where-Object LogicalProcessor -eq 0 | Measure-Object ReportedSampleDurationMs -Minimum).Minimum),3) } else { $null }
    AcceptedWindowMaxMs = if ($records.Count) { [Math]::Round((($records | Where-Object LogicalProcessor -eq 0 | Measure-Object ReportedSampleDurationMs -Maximum).Maximum),3) } else { $null }
    TemporalCoverage = 'NO_CUALIFICABLE: sin timestamp por grupo'
    MaximumGap = 'NO_CUALIFICABLE: sin timestamp por grupo'
    Monotonicity = 'NO_CUALIFICABLE: EMON emite deltas por intervalo, no valores acumulativos'
    Rollover = 'NO_CUALIFICABLE: anchura y snapshots de contador no expuestos'
    Qualification = 'FAIL: proxy de grupos inferior al 95%; EMON descarta grupos; cobertura temporal y huecos maximos no cualificables sin marcas temporales'
    PerLogicalProcessor = @($perLp)
}
$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $jsonPath -Encoding utf8
$summary | Select-Object Tool,CaptureSecondsRequested,SamplePeriodMillisecondsRequested,ExpectedGroups,GroupsParsed,GroupsDiscardedByEmon,ReferenceFrequencyMHz,AcceptedWindowMeanMs,AcceptedWindowMinMs,AcceptedWindowMaxMs,TemporalCoverage,MaximumGap,Monotonicity,Rollover,Qualification | Format-List
