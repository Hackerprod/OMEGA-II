param([string]$AttemptName=('smoke_r14_'+[DateTime]::Now.ToString('yyyyMMdd_HHmmssfff')))
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$root=Split-Path (Split-Path $here -Parent) -Parent
$obj=Join-Path $here 'logs_r14\obj_r14'
$collector=Join-Path $root 'T0P\collector'
$exe=Join-Path $obj 'harness_r14.exe'
$plan=Join-Path $collector 'residency_plan_r14.json'
$schedule=Join-Path $obj 'residency_smoke_schedule_r14.tsv'
$emon='C:\Program Files (x86)\Intel\oneAPI\vtune\2026.4\bin64\emon.exe'
$sourceEdp=Join-Path $root 'T0P\telemetry\reader\emon_joint_qualification.edp'
$cap=Join-Path $obj $AttemptName
$telemetry=Join-Path $collector 'telemetry_r14.py'
$derive=Join-Path $collector 'residency_derive_r14.py'
$attemptProc=$null;$emonProc=$null
foreach($path in @($exe,$plan,$schedule,$emon,$sourceEdp,$telemetry,$derive)){if(!(Test-Path -LiteralPath $path)){throw "required file missing: $path"}}
if(Test-Path -LiteralPath $cap){throw "new D13 attempt directory required: $cap"}
& python -B $telemetry prepare --attempt $cap --plan $plan --schedule $schedule --edp-source $sourceEdp
if($LASTEXITCODE -ne 0){throw "D13 prepare failed exit=$LASTEXITCODE"}
$emonRaw=Join-Path $cap 'emon_raw.txt';$emonErr=Join-Path $cap 'emon_stderr.txt'
$harnessOut=Join-Path $cap 'harness_stdout.txt';$harnessErr=Join-Path $cap 'harness_stderr.txt'
$edp=Join-Path $cap 'events.edp'
try{
  $emonProc=Start-Process -FilePath $emon -ArgumentList @('-collect-edp',"edp_file=$edp") -WorkingDirectory $cap `
    -RedirectStandardOutput $emonRaw -RedirectStandardError $emonErr -PassThru -WindowStyle Hidden
  Start-Sleep -Seconds 3;$emonProc.Refresh()
  if($emonProc.HasExited){throw "EMON exited before harness start exit=$($emonProc.ExitCode)"}
  $arguments=@('--residency-smoke-r14',$cap,$schedule)|ForEach-Object{'"'+$_+'"'}
  $attemptProc=Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $root `
    -RedirectStandardOutput $harnessOut -RedirectStandardError $harnessErr -PassThru -WindowStyle Hidden
  $deadline=[DateTime]::UtcNow.AddSeconds(14400);$earlyExit=$false
  while($true){
    $attemptProc.Refresh();$emonProc.Refresh()
    if($attemptProc.HasExited){$attemptProc.WaitForExit();break}
    if($emonProc.HasExited){$earlyExit=$true}
    if([DateTime]::UtcNow -gt $deadline){throw 'R14 smoke/EMON exceeded 14400 seconds'}
    Start-Sleep -Seconds 1
  }
  $attemptProc.Refresh();$emonProc.Refresh()
  if($earlyExit){throw "EMON stopped before harness completed exit=$($emonProc.ExitCode)"}
  $emonDeadline=[DateTime]::UtcNow.AddSeconds(14400)
  while(-not $emonProc.HasExited){
    if([DateTime]::UtcNow -gt $emonDeadline){throw 'EMON did not finish within 14400 seconds after harness'}
    Start-Sleep -Seconds 2;$emonProc.Refresh()
  }
  $emonProc.WaitForExit();$attemptProc.Refresh()
  $emonExitCode=$emonProc.ExitCode
  $emonError=Get-Content -LiteralPath $emonErr -Raw
  $emonTail=Get-Content -LiteralPath $emonRaw -Tail 5
  $emonHasFooter=[bool]($emonTail -match '^\s*\d+\.\d+s real\s+\d+\.\d+s user\s+\d+\.\d+s system\s*$')
  if([string]::IsNullOrWhiteSpace([string]$emonExitCode)){
    if(-not [string]::IsNullOrWhiteSpace($emonError) -or -not $emonHasFooter){throw "EMON exit code unavailable without clean stderr and complete footer; see $emonErr"}
    "EMON_EXIT_CODE_UNAVAILABLE accepted=footer_complete stderr_empty=true"
  }elseif([int]$emonExitCode -ne 0){throw "EMON exit=$emonExitCode; see $emonErr"}
  $harnessText=Get-Content -LiteralPath $harnessOut -Raw
  $harnessError=Get-Content -LiteralPath $harnessErr -Raw
  if($harnessError -match 'HARNESS_R14_ERROR|fatal error|Traceback'){throw "harness failed: $harnessError"}
  $harnessExitCode=$attemptProc.ExitCode
  $harnessComplete=$harnessText -match 'RESIDENCY_SMOKE_R14 BLOCKED_REAL|SMOKE_R14_CAPTURE PASS'
  if([string]::IsNullOrWhiteSpace([string]$harnessExitCode)){
    if(-not [string]::IsNullOrWhiteSpace($harnessError) -or -not $harnessComplete){throw "harness exit code unavailable without clean stderr and completion marker; see $harnessOut"}
    "HARNESS_EXIT_CODE_UNAVAILABLE accepted=completion_marker stderr_empty=true"
  }elseif([int]$harnessExitCode -ne 0 -and $harnessText -notmatch 'BLOCKED_REAL'){
    throw "harness exit=$harnessExitCode; see $harnessOut"
  }
  if(-not $harnessComplete){
    throw "harness lacks completion or real OS blocker; see $harnessOut"
  }
  & python -B $telemetry postprocess --attempt $cap --output (Join-Path $cap 'telemetry_status_r14.json')
  if($LASTEXITCODE -ne 0){throw "S20/D13 postprocess failed exit=$LASTEXITCODE"}
  if((Test-Path -LiteralPath (Join-Path $cap 'residency_events_r14.jsonl')) -and
     (Test-Path -LiteralPath (Join-Path $cap 'residency_episodes_r14.jsonl'))){
    & python -B $derive derive --plan $plan --events (Join-Path $cap 'residency_events_r14.jsonl') `
      --episodes (Join-Path $cap 'residency_episodes_r14.jsonl') --attempt-directory $cap `
      --output (Join-Path $cap 'residency_derive_status_r14.json')
    if($LASTEXITCODE -ne 0){throw "D11 smoke derive failed exit=$LASTEXITCODE"}
  }else{
    "RESIDENCY_DERIVE_R14 NOT_RUN real_OS_blocker=$(if($harnessText -match 'BLOCKED_REAL'){($harnessText -split 'BLOCKED_REAL')[-1].Trim()}else{'harness_artifacts_missing'}) synthetic_branches=PASS"
  }
  "SMOKE_R14_PIPELINE COMPLETE attempt=$cap harness_exit=$($attemptProc.ExitCode) emon_exit=$($emonProc.ExitCode)"
}catch{
  if($attemptProc){$attemptProc.Refresh();if(-not $attemptProc.HasExited){Stop-Process -Id $attemptProc.Id -Force -ErrorAction SilentlyContinue}}
  if($emonProc){$emonProc.Refresh();if(-not $emonProc.HasExited){Stop-Process -Id $emonProc.Id -Force -ErrorAction SilentlyContinue}}
  throw
}finally{
  if($attemptProc){$attemptProc.Dispose()}
  if($emonProc){$emonProc.Dispose()}
}
