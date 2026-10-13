param([string]$AttemptName=('smoke_r17_'+[DateTime]::Now.ToString('yyyyMMdd_HHmmssfff')))
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$root=Split-Path (Split-Path $here -Parent) -Parent
$collector=Join-Path $root 'T0P\collector'
$obj=Join-Path $collector 'logs_r17\obj_r17'
$hobj=Join-Path $here 'logs_r17\obj_r17'
$cap=Join-Path $obj $AttemptName
$exe=Join-Path $hobj 'harness_r17.exe'
$plan=Join-Path $obj 'residency_plan_r17.json'
$schedule=Join-Path $hobj 'residency_smoke_schedule_r17.tsv'
$emon='C:\Program Files (x86)\Intel\oneAPI\vtune\2026.4\bin64\emon.exe'
$sourceEdp=Join-Path $root 'T0P\telemetry\reader\emon_joint_qualification.edp'
$telemetry=Join-Path $collector 'telemetry_r17.py'
$official=Join-Path $collector 'official_run_r17.py'
$verify=Join-Path $collector 'verify_residency_smoke_r17.py'
$python='C:\Program Files\Python313\python.exe'
$gateProc=$null
function Get-FreeC { (Get-PSDrive C).Free }
function Get-AttemptBytes([string]$Path) {
  if(!(Test-Path -LiteralPath $Path)){return [long]0}
  $sum=Get-ChildItem -LiteralPath $Path -File -Recurse | Measure-Object -Property Length -Sum
  return [long]$sum.Sum
}
function Write-Status([string]$Reason,[long]$FreeBytes,[long]$AttemptBytes) {
  $status=Join-Path $collector 'STATUS_r17.md'
@("# STATUS R17","","- Estado: STOP por seguridad de recursos o plazo de captura.","- Motivo: $Reason",
    "- C: libre: $FreeBytes bytes","- Intento: $cap","- Intento: $AttemptBytes bytes",
    "- EMON: cierre solicitado con `emon -stop`.") | Set-Content -LiteralPath $status -Encoding UTF8
}
function Stop-GateTree([System.Diagnostics.Process]$Gate) {
  if(!$Gate){return}
  $children=Get-CimInstance Win32_Process -Filter "ParentProcessId=$($Gate.Id)" -ErrorAction SilentlyContinue
  foreach($child in $children){
    if($child.Name -eq 'harness_r17.exe' -and $child.ExecutablePath -eq $exe){
      Stop-Process -Id $child.ProcessId -Force -ErrorAction SilentlyContinue
    }
  }
  Stop-Process -Id $Gate.Id -Force -ErrorAction SilentlyContinue
}
foreach($path in @($exe,$plan,$schedule,$emon,$sourceEdp,$telemetry,$official,$python)){
  if(!(Test-Path -LiteralPath $path)){throw "required file missing: $path"}
}
if(Test-Path -LiteralPath $cap){throw "new D13 attempt directory required: $cap"}
$free=Get-FreeC
if($free -lt 4GB){throw "STOP BEFORE SMOKE: C free below 4GiB ($free bytes)"}
& $python -B $telemetry prepare --attempt $cap --plan $plan --schedule $schedule --edp-source $sourceEdp
if($LASTEXITCODE -ne 0){throw "capture budget / D13 prepare failed exit=$LASTEXITCODE"}
$budget=Get-Content -LiteralPath (Join-Path $cap 'capture_budget_r17.json') -Raw | ConvertFrom-Json
if([long]$budget.estimated_max_raw_bytes -ge 1GB){throw "attempt estimate is not below 1GiB: $($budget.estimated_max_raw_bytes)"}
$free=Get-FreeC
if($free -lt 4GB){Write-Status 'C: <4GiB after D13 preparation' $free (Get-AttemptBytes $cap); throw 'STOP BEFORE EMON: C: below 4GiB'}
$stdout=Join-Path $cap 'official_run_stdout_r17.txt'
$stderr=Join-Path $cap 'official_run_stderr_r17.txt'
$arguments=@('-B',$official,'--smoke','--integrated-executable',$exe,
  '--plan',(Join-Path $cap 'residency_plan_r17.json'),
  '--attempt-directory',$cap,'--emon',$emon)|ForEach-Object{'"'+$_+'"'}
try {
  $gateProc=Start-Process -FilePath $python -ArgumentList $arguments -WorkingDirectory $root `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru -WindowStyle Hidden
  $maximum=[double]$budget.maximum_capture_s
  $deadline=[DateTime]::UtcNow.AddSeconds($maximum+120.0)
  while($true){
    $gateProc.Refresh()
    if($gateProc.HasExited){$gateProc.WaitForExit();break}
    $free=Get-FreeC;$attemptBytes=Get-AttemptBytes $cap
    if($free -lt 4GB -or $attemptBytes -ge 1GB){
      $reason=if($free -lt 4GB){'C: free below 4GiB'}else{'attempt reached 1GiB'}
      Stop-GateTree $gateProc
      & $emon -stop *> $null
      Write-Status $reason $free $attemptBytes
      throw "STOP DURING SMOKE: $reason"
    }
    if([DateTime]::UtcNow -gt $deadline){
      Stop-GateTree $gateProc
      & $emon -stop *> $null
      $free=Get-FreeC;$attemptBytes=Get-AttemptBytes $cap
      Write-Status 'computed_capture_budget_deadline_exceeded' $free $attemptBytes
      throw 'STOP: acquisition exceeded computed capture budget'
    }
    Start-Sleep -Milliseconds 250
  }
} finally {
  if($gateProc){$gateProc.Dispose()}
}
$free=Get-FreeC;$attemptBytes=Get-AttemptBytes $cap
if($attemptBytes -ge 1GB){throw "attempt exceeded 1GiB: $attemptBytes"}
if((Test-Path -LiteralPath (Join-Path $cap 'emon_raw.txt')) -and
   (Test-Path -LiteralPath (Join-Path $cap 'preflight_r17.json'))){
  & $python -B $telemetry postprocess --attempt $cap --output (Join-Path $cap 'telemetry_status_r17.json')
  if($LASTEXITCODE -ne 0){throw "S20/D13 postprocess failed exit=$LASTEXITCODE"}
}
if((Test-Path -LiteralPath (Join-Path $cap 'residency_events_r17.jsonl')) -and
   (Test-Path -LiteralPath (Join-Path $cap 'residency_episodes_r17.jsonl')) -and
   (Test-Path -LiteralPath (Join-Path $cap 'phase_residency_r17.json')) -and
   (Test-Path -LiteralPath (Join-Path $cap 'capture_timing_r17.json')) -and
   (Test-Path -LiteralPath (Join-Path $cap 'preflight_r17.json'))){
  & $python -B $verify --attempt $cap
  if($LASTEXITCODE -ne 0){throw "residency smoke verification failed exit=$LASTEXITCODE"}
  "D11 NOT_RUN: derivador R17 pendiente de D11; smoke sin metricas adjudicables"
} else {
  "RESIDENCY_SMOKE_R17 NOT_REACHED gate_exit=$($gateProc.ExitCode) reason=early_gate_decision_or_real_blocker" 
}
"SMOKE_R17_PIPELINE COMPLETE attempt=$cap gate_exit=$($gateProc.ExitCode) attempt_bytes=$attemptBytes C_free_bytes=$free"
