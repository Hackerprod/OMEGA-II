$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$workspace = (Resolve-Path (Join-Path $root '..\..')).Path
$id = 'smoke_' + (Get-Date -Format 'yyyyMMdd_HHmmss')
$logs = Join-Path $root 'logs'
New-Item -ItemType Directory -Force -Path $logs | Out-Null
$summary = Join-Path $logs ($id + '_summary.md')
$collector = Join-Path $root 'collector.exe'
$plan = Join-Path $root 'plan.json'
$qualifyDir = Join-Path $workspace 'T0P\qualify'
$qualify = Join-Path $qualifyDir 'qualify.exe'
$reader = Join-Path $workspace 'T0P\telemetry\reader\telemetry_reader_r3.exe'
$emon = 'C:\Program Files (x86)\Intel\oneAPI\vtune\2026.4\bin64\emon.exe'
$env:PATH = (Split-Path $emon -Parent) + ';' + $env:PATH
$edp = Join-Path $workspace 'T0P\telemetry\reader\emon_joint_qualification.edp'
$telemetry = Join-Path $logs ($id + '_emon_joint_raw.txt')
$telemetryErr = Join-Path $logs ($id + '_emon_joint_stderr.txt')
$clockMap = Join-Path $logs ($id + '_clock_map.csv')
$dummyOut = Join-Path $logs ($id + '_conditioning.txt')
$dummyErr = Join-Path $logs ($id + '_conditioning.stderr.txt')
$providerOut = Join-Path $logs ($id + '_providers.txt')
$providerErr = Join-Path $logs ($id + '_providers.stderr.txt')
$evictExe = Join-Path $workspace 'T0P\evict\evict_qa.exe'
$evictOut = Join-Path $logs ($id + '_eviction.txt')
$negativeOut = Join-Path $logs ($id + '_telemetry_negative.txt')
$pageOut = Join-Path $logs ($id + '_page_gate.txt')
$status = [ordered]@{ label='SMOKE'; official_measurement=$false; metrics_used=$false; phases=[ordered]@{} }
$emonProc = $null
function Test-PageRecord([string]$line, [UInt64]$systemPageSize) {
    if ($line -notmatch 'address=0x([0-9a-fA-F]+) bytes=(\d+) queried_pages=(\d+) valid_pages=(\d+) large_pages=(\d+) QueryWorkingSetEx=PASS') { return $false }
    $addr=[Convert]::ToUInt64($Matches[1],16); $bytes=[UInt64]$Matches[2]; $queried=[UInt64]$Matches[3]; $valid=[UInt64]$Matches[4]; $large=[UInt64]$Matches[5]
    $expected=[UInt64][Math]::Ceiling((($addr % $systemPageSize) + $bytes) / [double]$systemPageSize)
    return ($bytes -gt 0 -and $queried -eq $expected -and $valid -eq $queried -and $large -eq $queried)
}
try {
    & $collector --self-test *> (Join-Path $logs ($id + '_decision_gate.txt'))
    if ($LASTEXITCODE -ne 0) { throw "decision gate exited $LASTEXITCODE" }
    & $collector --plan-check $plan *> (Join-Path $logs ($id + '_plan_check.txt'))
    if ($LASTEXITCODE -ne 0) { throw "plan check exited $LASTEXITCODE" }
    $status.phases['plan_and_four_numeric_gate_cases'] = 'PASS_SMOKE_ONLY'

    $env:PATH = (Join-Path $workspace 'T0P\ref_ggml\build_llama\bin') + ';' + (Join-Path $workspace '..\.venv_cuda\Lib\site-packages\torch\lib') + ';' + $env:PATH
    $emonProc = Start-Process -FilePath $emon -ArgumentList @('-collect-edp', "edp_file=$edp") -WorkingDirectory (Split-Path $edp -Parent) -RedirectStandardOutput $telemetry -RedirectStandardError $telemetryErr -PassThru -WindowStyle Hidden
    Start-Sleep -Seconds 3
    $emonProc.Refresh()
    if ($emonProc.HasExited) { throw "EMON CLI joint exited early: $($emonProc.ExitCode); see $telemetryErr and $telemetry" }

    $provider = Start-Process -FilePath $qualify -ArgumentList '--dry-run' -WorkingDirectory $qualifyDir -RedirectStandardOutput $providerOut -RedirectStandardError $providerErr -PassThru -Wait -WindowStyle Hidden
    $status.phases['real_kernel_ggml_torch_providers'] = if ($provider.ExitCode -eq 0) { 'PASS_SMOKE_QA_NO_TIMING' } else { "FAIL_EXIT_$($provider.ExitCode)" }
    if ($provider.ExitCode -ne 0) { throw "qualify.exe --dry-run exited $($provider.ExitCode); see $providerOut and $providerErr" }
    $providerManifest = Join-Path $qualifyDir 'SHA256SUMS_qualify.txt'
    $providerHashes = @{}
    foreach ($entry in Get-Content $providerManifest) { if ($entry -match '^([0-9a-f]{64})\s+T0P/qualify/(qualify(?:_ggml_qa|_torch_qa|_kernel_qa|_kernel_controls)?\.exe)$') { $providerHashes[$Matches[2]]=$Matches[1] } }
    foreach ($name in @('qualify.exe','qualify_ggml_qa.exe','qualify_torch_qa.exe','qualify_kernel_qa.exe','qualify_kernel_controls.exe')) {
        $path=Join-Path $qualifyDir $name
        $actual=(Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant()
        if (-not $providerHashes.ContainsKey($name) -or $actual -ne $providerHashes[$name]) { throw "frozen provider SHA-256 mismatch: $name actual=$actual expected=$($providerHashes[$name])" }
    }
    $contractPath = Join-Path $qualifyDir 'logs\dryrun_summary.json'
    $contract = Get-Content $contractPath -Raw | ConvertFrom-Json
    if ($contract.status -ne 'PASS' -or $contract.routine_records.Count -ne 61 -or $contract.timed -ne $false -or $null -ne $contract.g_ggml -or $null -ne $contract.g_torch -or $null -ne $contract.d_prime_hot -or $null -ne $contract.p_instr) { throw 'provider JSON contract failed schema/count/timing-null checks' }
    $contractHash=(Get-FileHash -Algorithm SHA256 -LiteralPath $contractPath).Hash.ToLowerInvariant()
    @("provider_exe_hashes=PASS","dryrun_contract_schema=$($contract.mode)","records=$($contract.routine_records.Count)","timed=$($contract.timed)","numeric_metrics=null","dryrun_summary_sha256=$contractHash") | Set-Content (Join-Path $logs ($id + '_provider_contract.txt'))

    $evict = Start-Process -FilePath $evictExe -WorkingDirectory (Split-Path $evictExe -Parent) -RedirectStandardOutput $evictOut -RedirectStandardError (Join-Path $logs ($id + '_eviction.stderr.txt')) -PassThru -Wait -WindowStyle Hidden
    if ($evict.ExitCode -ne 0) { throw "evict_qa.exe exited $($evict.ExitCode); see $evictOut" }
    $evictText = Get-Content $evictOut -Raw
    if ($evictText -notmatch 'TOPOLOGY=PASS' -or $evictText -notmatch 'LARGE_PAGE_PRIVILEGE=AVAILABLE' -or $evictText -notmatch 'N=31') { throw 'eviction physical preflight did not confirm topology, pages and N=31' }
    $status.phases['physical_topology_affinity_pages_and_eviction'] = 'PASS_SMOKE_QA; see eviction log; LP affinity / large-page checks from provider'

    & $reader --dummy 180 $clockMap *> $dummyOut 2> $dummyErr
    $dummyExit = $LASTEXITCODE
    if ($dummyExit -ne 0) { throw "telemetry_reader_r3.exe --dummy 180 exited $dummyExit; see $dummyOut and $dummyErr" }
    $status.phases['emon_cli_joint_and_180s_conditioning'] = 'PASS_SMOKE'

    & $reader --negative-tests *> $negativeOut
    if ($LASTEXITCODE -ne 0) { throw "telemetry negative tests exited $LASTEXITCODE" }
    $status.phases['telemetry_rules_negative_cases'] = 'PASS_SMOKE'

    $pageContract = Get-Content (Join-Path $root 'PAGE_EXPECTED_COLLECTOR.json') -Raw | ConvertFrom-Json
    $pageSize = [Environment]::SystemPageSize
    $rows = @()
    foreach ($log in @((Join-Path $qualifyDir 'logs\dryrun_ggml.log'), (Join-Path $qualifyDir 'logs\dryrun_torch.log'), (Join-Path $qualifyDir 'logs\dryrun_omega.log'))) {
        if (Test-Path $log) { $rows += Select-String -Path $log -Pattern '^PAGE ' | ForEach-Object { $_.Line } }
    }
    $checked = 0
    foreach ($line in $rows) {
        if (-not (Test-PageRecord $line ([UInt64]$pageSize))) { throw "PAGE_EXPECTED_COLLECTOR mismatch: $line; page_size=$pageSize" }
        $checked++
    }
    if ($checked -eq 0) { throw 'PAGE_EXPECTED_COLLECTOR found no real provider PAGE records' }
    $badValid = $rows[0] -replace 'valid_pages=\d+', 'valid_pages=0'
    $badLarge = $rows[0] -replace 'large_pages=\d+', 'large_pages=0'
    if ((Test-PageRecord $badValid ([UInt64]$pageSize)) -or (Test-PageRecord $badLarge ([UInt64]$pageSize))) { throw 'consumer page gate accepted a negative Valid/LargePage counterexample' }
    "schema=$($pageContract.schema) page_size=$pageSize provider_PAGE_rows=$checked negative_valid_pages=REJECT negative_large_pages=REJECT status=PASS_SMOKE" | Set-Content $pageOut
    $status.phases['consumer_page_gate_and_negative_cases'] = 'PASS_SMOKE'

    $raw = Get-Content $telemetry -Raw
    $groups = ([regex]::Matches($raw, '(?m)^==========\s*$')).Count
    $footer = $raw -match '(?m)^\s*\d+\.\d+s real\s+\d+\.\d+s user\s+\d+\.\d+s system\s*$'
    if ($raw -notmatch 'TSC Freq\s+\.\.\.\.+\s+2112\.00 MHz' -or $raw -notmatch 'MSR_EVENT:msr=0x606:type=STATIC:scope=PACKAGE\s+[\d,]+\s+658,947' -or $raw -notmatch 'MSR_EVENT:msr=0x611:type=FREERUN:scope=PACKAGE' -or $raw -notmatch 'MSR_EVENT:msr=0x19C:type=STATIC:scope=THREAD' -or $raw -notmatch 'MSR_EVENT:msr=0x1B1:type=STATIC:scope=PACKAGE') { throw 'EMON capture lacks a required joint channel or u_E/f_TSC evidence' }
    $tcc = if ($raw -match 'MSR_EVENT:msr=0x1A2') { 'CAPTURED' } else { 'PREQUALIFIED_100C_NOT_IN_STREAM' }
    @("QPC_frequency=$([Diagnostics.Stopwatch]::Frequency)","TSC_frequency=2112.00_MHz","energy_unit=2^-14_J_from_0x606_658947","TCC=$tcc","PL1=NOT_READABLE_WITH_AVAILABLE_INTERFACE","PL2=NOT_READABLE_WITH_AVAILABLE_INTERFACE","POWER_TIME_WINDOWS=NOT_READABLE_WITH_AVAILABLE_INTERFACE","PERF_LIMIT_REASONS_0x64F=NOT_READABLE_WITH_AVAILABLE_INTERFACE","EMON_groups=$groups","footer=$footer") | Set-Content (Join-Path $logs ($id + '_physical_telemetry.txt'))
    $status.phases['emon_cli_groups'] = "groups=$groups footer=$footer"
    $status.phases['uE_tcc_fTSC_qpc_and_unreadable_power_limits'] = "u_E=2^-14_J; TSC=2112_MHz; TCC=$tcc; PL1_PL2_0x64F=NOT_READABLE_WITH_AVAILABLE_INTERFACE"
    $official = Start-Process -FilePath $collector -ArgumentList '--official' -WorkingDirectory $root -RedirectStandardOutput (Join-Path $logs ($id + '_official.stdout.txt')) -RedirectStandardError (Join-Path $logs ($id + '_official.stderr.txt')) -PassThru -Wait -WindowStyle Hidden
    if ($official.ExitCode -ne 20) { throw "--official expected fail-closed exit 20, got $($official.ExitCode)" }
    $status.phases['official_fail_closed_without_authorized'] = 'PASS_EXIT_20'
    $status.phases['derivation_and_campaign_cut'] = 'PASS_SELF_TESTED; no performance values emitted'
    $status.phases['block_telemetry_with_margins'] = 'UNBOUND_IN_SMOKE; EMON capture and QPC conditioning recorded, provider-specific padded windows not attributed'
    $status.phases['eviction'] = 'QUALIFIED_BY_EXISTING_T0P_EVICT_EVIDENCE; standalone provider integration remains separate'
} catch {
    $status.phases['execution_error'] = $_.Exception.Message
    $status | ConvertTo-Json -Depth 6 | Set-Content $summary
    throw
} finally {
    if ($emonProc) {
        $emonProc.Refresh()
        if (-not $emonProc.HasExited) { Stop-Process -Id $emonProc.Id -Force; Start-Sleep -Milliseconds 500 }
        $emonProc.Refresh()
    }
}
$status | ConvertTo-Json -Depth 6 | Set-Content $summary
Write-Output "SMOKE_SUMMARY=$summary"
Write-Output "SMOKE phases=$($status.phases.Count) provider=real timed=false official_measurement=false"
