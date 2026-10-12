param([string]$Collector=(Join-Path $PSScriptRoot '..\collector\collector.exe'),[string]$Plan=(Join-Path $PSScriptRoot '..\collector\plan.json'))
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$logs=Join-Path $root 'logs_r11'
New-Item -ItemType Directory -Force -Path $logs|Out-Null
$check=Join-Path $logs 'plan_check_r11.txt'
& $Collector --plan-check $Plan *> $check
if($LASTEXITCODE -ne 0){throw "plan hash/schema validation failed; see $check"}
$j=Get-Content -LiteralPath $Plan -Raw|ConvertFrom-Json
"plan_sha256=$($j.sha256) events=$($j.events.Count) validated_before_calibration=true"|Set-Content -LiteralPath (Join-Path $logs 'plan_preflight_r11.txt')
$bad=Join-Path $logs 'plan_one_byte_altered_r11.json'
$b=[IO.File]::ReadAllBytes($Plan);$b[0]=[byte][char]'[';[IO.File]::WriteAllBytes($bad,$b)
& $Collector --plan-check $bad *> (Join-Path $logs 'plan_mutation_reject_r11.txt')
if($LASTEXITCODE -eq 0){throw 'one-byte-mutated plan was accepted'}
Remove-Item -LiteralPath $bad -Force
$official=Join-Path $logs 'official_fail_closed_r11.txt'
$officialErr=Join-Path $logs 'official_fail_closed_r11.stderr.txt'
$op=Start-Process -FilePath $Collector -ArgumentList @('--official') -PassThru -Wait -NoNewWindow -RedirectStandardOutput $official -RedirectStandardError $officialErr
if($op.ExitCode -eq 0){throw 'collector --official unexpectedly accepted without authorization'}
if(!((Get-Content -Raw $official)+ (Get-Content -Raw $officialErr)).Contains('AUTHORIZED.json')){throw "official rejection reason missing; see $official"}
Write-Output 'PREFLIGHT plan=PASS mutation=REJECT official_without_AUTHORIZED=REJECT'
