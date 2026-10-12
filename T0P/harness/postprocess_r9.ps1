param([Parameter(Mandatory=$true)][string]$Capture)
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$cap=(Resolve-Path -LiteralPath $Capture).Path
$schema=Join-Path $here 'validate_r9.py'
& python $schema --raw (Join-Path $cap 'raw.jsonl') *> (Join-Path $cap 'schema_raw_prederive.txt')
if($LASTEXITCODE -ne 0){throw "raw JSONL schema rejected before derivation; see $cap\schema_raw_prederive.txt"}
& python $schema --summary (Join-Path $cap 'summary.jsonl') *> (Join-Path $cap 'schema_summary_prederive.txt')
if($LASTEXITCODE -ne 0){throw "summary JSONL schema rejected before derivation; see $cap\schema_summary_prederive.txt"}
& python (Join-Path $here 'telemetry_r9.py') --derive --capture $cap *> (Join-Path $cap 'telemetry_runner.stdout.txt') 2> (Join-Path $cap 'telemetry_runner.stderr.txt')
$telemetryRc=$LASTEXITCODE
if($telemetryRc -notin @(0,4)){throw "telemetry_r9.py exited $telemetryRc; see $cap\telemetry_runner.stdout.txt and $cap\telemetry_runner.stderr.txt"}
"TELEMETRY_DERIVATION_EXIT=$telemetryRc invalid_samples_recorded=true"|Set-Content -LiteralPath (Join-Path $cap 'telemetry_runner.status.txt')
& python $schema --raw (Join-Path $cap 'raw.jsonl') *> (Join-Path $cap 'schema_raw_postderive.txt')
if($LASTEXITCODE -ne 0){throw "derived raw JSONL schema rejected; see $cap\schema_raw_postderive.txt"}
& python $schema --summary (Join-Path $cap 'summary.jsonl') *> (Join-Path $cap 'schema_summary_postderive.txt')
if($LASTEXITCODE -ne 0){throw "derived summary JSONL schema rejected; see $cap\schema_summary_postderive.txt"}
Write-Output "TELEMETRY_POSTPROCESS capture=$cap exit=$telemetryRc"
Get-Content (Join-Path $cap 'telemetry_runner.stdout.txt')
Get-Content (Join-Path $cap 'schema_raw_postderive.txt')
Get-Content (Join-Path $cap 'schema_summary_postderive.txt')
