param([string]$Executable=(Join-Path $PSScriptRoot 'harness_r9.exe'),[string]$Plan=(Join-Path $PSScriptRoot '..\collector\plan.json'),[string]$Collector=(Join-Path $PSScriptRoot '..\collector\collector.exe'))
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$root=Split-Path (Split-Path $here -Parent) -Parent
if(!(Test-Path -LiteralPath $Executable)){throw "harness missing: $Executable"}
$id='geometry_'+(Get-Date -Format 'yyyyMMdd_HHmmss')
$cap=Join-Path $here ('logs_r9\'+$id)
New-Item -ItemType Directory -Force -Path $cap|Out-Null
& $Collector --plan-check $Plan *> (Join-Path $cap 'plan_check_before_calibration.txt')
if($LASTEXITCODE -ne 0){throw "plan rejected before geometry calibration; see $cap\plan_check_before_calibration.txt"}
$planObj=Get-Content -LiteralPath $Plan -Raw|ConvertFrom-Json
$planSha=[string]$planObj.sha256
if($planSha -notmatch '^[0-9a-f]{64}$'){throw 'plan sha256 invalid'}
$env:PATH=(Join-Path $root 'T0P\ref_ggml\build_llama\bin')+';'+(Join-Path (Split-Path $root -Parent) '.venv_cuda\Lib\site-packages\torch\lib')+';'+$env:PATH
$launcher=Join-Path $here 'launch_geometry_smoke_r9.cmd'
$args='/d /c ""'+$launcher+'" "'+$Executable+'" "'+$cap+'" "'+$Plan+'" "'+$planSha+'""'
$hp=Start-Process -FilePath $env:ComSpec -ArgumentList $args -WorkingDirectory $root -PassThru -Wait -WindowStyle Hidden
$exitFile=Join-Path $cap 'geometry.exit.txt'
if(!(Test-Path -LiteralPath $exitFile)){throw "geometry launcher exited without code file; see $cap"}
$rc=[int](Get-Content -LiteralPath $exitFile -Raw)
if($rc -ne 0){throw "geometry smoke exited $rc; see $cap\geometry.stdout.txt and $cap\geometry.stderr.txt"}
Write-Output "GEOMETRY_CAPTURE=$cap"
Get-Content -LiteralPath (Join-Path $cap 'geometry.stdout.txt')
& python (Join-Path $here 'validate_r9.py') --raw (Join-Path $cap 'geometry_raw.jsonl') *> (Join-Path $cap 'schema_raw.txt')
if($LASTEXITCODE -ne 0){throw "geometry raw JSONL schema rejected; see $cap\schema_raw.txt"}
& python (Join-Path $here 'validate_r9.py') --summary (Join-Path $cap 'geometry_summary.jsonl') *> (Join-Path $cap 'schema_summary.txt')
if($LASTEXITCODE -ne 0){throw "geometry summary schema rejected; see $cap\schema_summary.txt"}
Get-Content -LiteralPath (Join-Path $cap 'schema_raw.txt')
Get-Content -LiteralPath (Join-Path $cap 'schema_summary.txt')
