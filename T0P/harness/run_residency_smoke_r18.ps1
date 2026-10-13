param([string]$AttemptName=('smoke_chain_n3_r18_'+[DateTime]::Now.ToString('yyyyMMdd_HHmmssfff')))
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$root=Split-Path (Split-Path $here -Parent) -Parent
$collector=Join-Path $root 'T0P\collector'
$obj=Join-Path $collector 'logs_r18\obj_r18'
$hobj=Join-Path $here 'logs_r18\obj_r18'
$cap=Join-Path $obj $AttemptName
$exe=Join-Path $hobj 'harness_r18.exe'
$plan=Join-Path $obj 'residency_plan_r18.json'
$schedule=Join-Path $hobj 'residency_smoke_schedule_r18.tsv'
$emon='C:\Program Files (x86)\Intel\oneAPI\vtune\2026.4\bin64\emon.exe'
$sourceEdp=Join-Path $root 'T0P\telemetry\reader\emon_joint_qualification.edp'
$telemetry=Join-Path $collector 'telemetry_r18.py'
$official=Join-Path $collector 'official_run_r18.py'
$verify=Join-Path $collector 'verify_residency_smoke_r18.py'
$python='C:\Program Files\Python313\python.exe'
$startedUtc=$null
$gateProc=$null
$gateExit=$null
function Get-FreeC { (Get-PSDrive C).Free }
function Get-AttemptBytes([string]$Path) {
  if(!(Test-Path -LiteralPath $Path)){return [long]0}
  $sum=Get-ChildItem -LiteralPath $Path -File -Recurse | Measure-Object -Property Length -Sum
  return [long]$sum.Sum
}
function Write-Status([string]$Reason,[long]$FreeBytes,[long]$AttemptBytes) {
  $status=Join-Path $collector 'STATUS_r18.md'
@("# STATUS R18","","- Estado: STOP por seguridad de recursos o plazo de captura.","- Motivo: $Reason",
    "- C: libre: $FreeBytes bytes","- Intento: $cap","- Intento: $AttemptBytes bytes",
    "- EMON: cierre solicitado con `emon -stop`.") | Set-Content -LiteralPath $status -Encoding UTF8
}
function Stop-GateTree([System.Diagnostics.Process]$Gate) {
  if(!$Gate){return}
  $children=Get-CimInstance Win32_Process -Filter "ParentProcessId=$($Gate.Id)" -ErrorAction SilentlyContinue
  foreach($child in $children){
    if($child.Name -eq 'harness_r18.exe' -and $child.ExecutablePath -eq $exe){
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
$startedUtc=[DateTime]::UtcNow
& $python -B $telemetry prepare --attempt $cap --plan $plan --schedule $schedule --edp-source $sourceEdp
if($LASTEXITCODE -ne 0){throw "capture budget / D13 prepare failed exit=$LASTEXITCODE"}
$budget=Get-Content -LiteralPath (Join-Path $cap 'capture_budget_r18.json') -Raw | ConvertFrom-Json
if([long]$budget.estimated_max_raw_bytes -ge 1GB){throw "attempt estimate is not below 1GiB: $($budget.estimated_max_raw_bytes)"}
$prepareFree=Get-FreeC
"P1_PRECHECK C_free_bytes=$prepareFree threshold_bytes=4294967296 estimated_attempt_raw_bytes=$($budget.estimated_max_raw_bytes) attempt_limit_bytes=1073741824"
$free=Get-FreeC
if($free -lt 4GB){Write-Status 'C: <4GiB after D13 preparation' $free (Get-AttemptBytes $cap); throw 'STOP BEFORE EMON: C: below 4GiB'}
$stdout=Join-Path $cap 'official_run_stdout_r18.txt'
$stderr=Join-Path $cap 'official_run_stderr_r18.txt'
$arguments=@('-B',$official,'--smoke','--integrated-executable',$exe,
  '--plan',(Join-Path $cap 'residency_plan_r18.json'),
  '--attempt-directory',$cap,'--emon',$emon)|ForEach-Object{'"'+$_+'"'}
try {
  $gateProc=Start-Process -FilePath $python -ArgumentList $arguments -WorkingDirectory $root `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru -WindowStyle Hidden
  $maximum=[double]$budget.maximum_capture_s
  $deadline=[DateTime]::UtcNow.AddSeconds($maximum+120.0)
  $captureTimingPath=Join-Path $cap 'capture_timing_r18.json'
  $captureDone=$false
  while($true){
    $gateProc.Refresh()
    if($gateProc.HasExited){$gateProc.WaitForExit();break}
    if(Test-Path -LiteralPath $captureTimingPath){
      try {
        $captureTiming=Get-Content -LiteralPath $captureTimingPath -Raw | ConvertFrom-Json
        $captureDone=($captureTiming.controlled_stop -eq $true -and
          $captureTiming.stop_exit -eq 0 -and $captureTiming.emon_exit -eq 0)
      } catch { $captureDone=$false }
    }
    $free=Get-FreeC;$attemptBytes=Get-AttemptBytes $cap
    if($free -lt 4GB -or $attemptBytes -ge 1GB){
      $reason=if($free -lt 4GB){'C: free below 4GiB'}else{'attempt reached 1GiB'}
      Stop-GateTree $gateProc
      if(-not $captureDone){& $emon -stop *> $null}
      Write-Status $reason $free $attemptBytes
      throw "STOP DURING SMOKE: $reason"
    }
    if(-not $captureDone -and [DateTime]::UtcNow -gt $deadline){
      Stop-GateTree $gateProc
      & $emon -stop *> $null
      $free=Get-FreeC;$attemptBytes=Get-AttemptBytes $cap
      Write-Status 'computed_capture_budget_deadline_exceeded' $free $attemptBytes
      throw 'STOP: acquisition exceeded computed capture budget'
    }
    Start-Sleep -Milliseconds 250
  }
} finally {
  if($gateProc){$gateProc.Refresh();$gateExit=$gateProc.ExitCode;$gateProc.Dispose()}
}
$free=Get-FreeC;$attemptBytes=Get-AttemptBytes $cap
if($attemptBytes -ge 1GB){throw "attempt exceeded 1GiB: $attemptBytes"}
if((Test-Path -LiteralPath (Join-Path $cap 'residency_events_r18.jsonl')) -and
   (Test-Path -LiteralPath (Join-Path $cap 'residency_episodes_r18.jsonl')) -and
   (Test-Path -LiteralPath (Join-Path $cap 'phase_residency_r18.json')) -and
   (Test-Path -LiteralPath (Join-Path $cap 'capture_timing_r18.json')) -and
   (Test-Path -LiteralPath (Join-Path $cap 'preflight_r18.json'))){
  & $python -B $verify --attempt $cap
  if($LASTEXITCODE -ne 0){throw "residency smoke verification failed exit=$LASTEXITCODE"}
  $summary=Get-Content -LiteralPath (Join-Path $cap 'smoke_chain_summary_r18.json') -Raw | ConvertFrom-Json
  $timing=Get-Content -LiteralPath (Join-Path $cap 'capture_timing_r18.json') -Raw | ConvertFrom-Json
  $preflight=Get-Content -LiteralPath (Join-Path $cap 'preflight_r18.json') -Raw | ConvertFrom-Json
  $lastEvent=Get-Content -LiteralPath (Join-Path $cap 'residency_events_r18.jsonl') -Tail 1 | ConvertFrom-Json
  $captureDuration=([double]$timing.qpc_end-[double]$timing.qpc_start)/[double]$timing.qpf_hz
  "P1_CAPTURE qpc_source=$($timing.qpc_source) qpc_start=$($timing.qpc_start) qpc_end=$($timing.qpc_end) qpf_hz=$($preflight.qpf_hz) duration_s=$captureDuration controlled_stop=$($timing.controlled_stop) stop_exit=$($timing.stop_exit) emon_exit=$($timing.emon_exit) bytes=$($timing.capture_bytes) last_block=$($lastEvent.block_id) last_event=$($lastEvent.event_id) event_count=$($summary.phases[-1].events)"
  "P2_INSTRUMENTATION state=$($summary.p2_instrumentation.state) samples=$($summary.p2_instrumentation.samples) max_fraction=$($summary.p2_instrumentation.max_overhead_fraction) threshold=$($summary.p2_instrumentation.threshold_fraction)"
  "P3_S20_FULL state=$($summary.s20_postprocess.phase_audit_state) S20=$($summary.s20_postprocess.state) phase_audit_sha256=$($summary.s20_postprocess.phase_audit_sha256)"
  "P6_D11 state=$($summary.d11_derivation.state) exact_union=$($summary.d11_derivation.event_union.exact) output=$($summary.d11_derivation.output)"
} else {
  "RESIDENCY_SMOKE_R18 NOT_REACHED gate_exit=$gateExit reason=early_gate_decision_or_real_blocker" 
}
$elapsed=([DateTime]::UtcNow-$startedUtc).TotalSeconds
"SMOKE_R18_PIPELINE COMPLETE attempt=$cap gate_exit=$gateExit attempt_bytes=$attemptBytes C_free_bytes=$free total_elapsed_s=$elapsed"
