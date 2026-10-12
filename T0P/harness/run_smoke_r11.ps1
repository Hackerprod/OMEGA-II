param(
  [string]$Executable=(Join-Path $PSScriptRoot 'harness_r11.exe'),
  [string]$Plan=(Join-Path $PSScriptRoot '..\collector\plan.json'),
  [string]$Collector=(Join-Path $PSScriptRoot '..\collector\collector.exe'),
  [ValidateSet(3,31)][int]$SampleCount=3,
  [ValidateSet(1,2)][int]$Attempt=1
)
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$root=Split-Path (Split-Path $here -Parent) -Parent
if(!(Test-Path -LiteralPath $Executable)){throw "harness missing: $Executable"}
if(!(Test-Path -LiteralPath $Collector)){throw "collector missing: $Collector"}
$id='smoke_'+(Get-Date -Format 'yyyyMMdd_HHmmss')+'_attempt'+$Attempt
$cap=Join-Path $here ('logs_r11\'+$id)
New-Item -ItemType Directory -Force -Path $cap|Out-Null
$planCheck=Join-Path $cap 'plan_check.txt'
& $Collector --plan-check $Plan *> $planCheck
if($LASTEXITCODE -ne 0){throw "plan rejected before calibration; see $planCheck"}
$planObj=Get-Content -LiteralPath $Plan -Raw|ConvertFrom-Json
$planSha=[string]$planObj.sha256
if($planSha -notmatch '^[0-9a-f]{64}$'){throw 'plan SHA256 invalid'}
$n=$SampleCount
$profilePath=Join-Path $root 'T0P\harness\logs_r9\smoke_20261011_233515\calibration.json'
if(!(Test-Path -LiteralPath $profilePath)){throw "verified calibration profile missing: $profilePath"}
$profile=Get-Content -LiteralPath $profilePath -Raw|ConvertFrom-Json
if($profile.plan_sha256 -ne $planSha -or $profile.unit -ne 'smoke_uniform_m1' -or [int]$profile.R_common -lt 1){throw 'calibration profile does not match the canonical plan/unit'}
$providers=@{}
foreach($group in ($profile.provider_workers|Group-Object provider)){
  $rates=@($group.Group|ForEach-Object{[double]$_.iterations_per_s}|Sort-Object)
  if($rates.Count -ne 8 -or $rates[0] -le 0){throw "invalid calibration profile for $($group.Name)"}
  $mid=($rates[3]+$rates[4])/2
  $providers[$group.Name]=$mid
}
$expectedProviders=@('omega','ggml_flat','ggml_repack','torch_mm')
if($providers.Count -ne $expectedProviders.Count -or @($expectedProviders|Where-Object{-not $providers.ContainsKey($_)}).Count){throw 'calibration provider set differs from the planned four-provider unit'}
$measurementSeconds=0.0
foreach($name in $expectedProviders){$measurementSeconds += $n*[double]$profile.R_common/$providers[$name]}
$estimatedSeconds=$measurementSeconds+$(if($n -eq 3){75.0}else{45.0})
$groupsPerSecond=3600.0/220.0
$safetyMargin=0.50
$requiredSeconds=$estimatedSeconds*(1+$safetyMargin)
$emonGroups=[int][Math]::Ceiling($requiredSeconds*$groupsPerSecond)
if($emonGroups -lt $n){$emonGroups=$n}
if($SampleCount -eq 31 -and $emonGroups -le 3600){throw "duration estimate did not exceed legacy EMON limit: $emonGroups groups"}
$sourceEdp=Join-Path $root 'T0P\telemetry\reader\emon_joint_qualification.edp'
$emonEdp=Join-Path $cap 'emon_joint_qualification_r11.edp'
$edpText=[IO.File]::ReadAllText($sourceEdp)
if(([regex]::Matches($edpText,'-l3600')).Count -ne 1){throw 'source EDP must contain exactly one legacy -l3600'}
$edpText=$edpText.Replace('-l3600',('-l'+$emonGroups))
[IO.File]::WriteAllText($emonEdp,$edpText,[Text.UTF8Encoding]::new($false))
$bytesPerGroup=7206907.0/3600.0
$estimatedRawBytes=[int64][Math]::Ceiling($bytesPerGroup*$emonGroups)
$drive=[IO.DriveInfo]::new([IO.Path]::GetPathRoot($cap))
if($estimatedRawBytes -gt 100MB){throw "raw EMON estimate is not reasonable: $estimatedRawBytes bytes"}
if($drive.AvailableFreeSpace -lt ($estimatedRawBytes*3+1GB)){throw "insufficient free space for EMON capture: available=$($drive.AvailableFreeSpace) estimated=$estimatedRawBytes"}
$estimate=[ordered]@{
  label='SMOKE';official_measurement=$false;metrics_used=$false;N=$n;unit=$profile.unit
  plan_sha256=$planSha;R_common=[int64]$profile.R_common;provider_rates_median=$providers
  measurement_seconds=[Math]::Round($measurementSeconds,3);estimated_plan_seconds=[Math]::Round($estimatedSeconds,3)
  safety_margin=$safetyMargin;required_capture_seconds=[Math]::Round($requiredSeconds,3)
  groups_per_second=$groupsPerSecond;emon_groups=$emonGroups;estimated_raw_bytes=$estimatedRawBytes
  free_bytes_before=$drive.AvailableFreeSpace;edp=$emonEdp
}
$estimate|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $cap 'capture_estimate.json') -Encoding utf8
$emon='C:\Program Files (x86)\Intel\oneAPI\vtune\2026.4\bin64\emon.exe'
$env:PATH=(Join-Path $root 'T0P\ref_ggml\build_llama\bin')+';'+(Join-Path (Split-Path $root -Parent) '.venv_cuda\Lib\site-packages\torch\lib')+';'+(Split-Path $emon -Parent)+';'+$env:PATH
$raw=Join-Path $cap 'emon_stdout.txt';$err=Join-Path $cap 'emon_stderr.txt'
if(!(Test-Path -LiteralPath $emon)){throw "EMON missing: $emon"}
Add-Type -TypeDefinition @'
using System;using System.Runtime.InteropServices;
public struct HarnessClockR11{public long pre;public long post;public long filetime;}
public static class HarnessQpcR11{[DllImport("kernel32.dll")]public static extern bool QueryPerformanceCounter(out long x);[DllImport("kernel32.dll")]public static extern bool QueryPerformanceFrequency(out long x);[DllImport("kernel32.dll")]public static extern void GetSystemTimePreciseAsFileTime(out long x);public static HarnessClockR11 Sample(){HarnessClockR11 s;QueryPerformanceCounter(out s.pre);GetSystemTimePreciseAsFileTime(out s.filetime);QueryPerformanceCounter(out s.post);return s;}}
'@
$sw=$null;$emonProc=$null;$harnessProc=$null;$earlyExit=$false;$harnessRc=$null;$retryable=$false;$failureReason=''
try{
  $emonProc=Start-Process -FilePath $emon -ArgumentList @('-collect-edp',"edp_file=$emonEdp") -WorkingDirectory $cap -RedirectStandardOutput $raw -RedirectStandardError $err -PassThru -WindowStyle Hidden
  Start-Sleep -Seconds 3;$emonProc.Refresh();if($emonProc.HasExited){throw "EMON exited before harness start: exit=$($emonProc.ExitCode)"}
  $qpcHz=[long]0;if(![HarnessQpcR11]::QueryPerformanceFrequency([ref]$qpcHz)-or $qpcHz -le 0){throw 'QueryPerformanceFrequency unavailable'}
  $map=Join-Path $cap 'clock_map.csv';$sw=[IO.StreamWriter]::new($map,$false,[Text.UTF8Encoding]::new($false))
  $sw.WriteLine('sample,qpc_pre,qpc_post,filetime_100ns,qpc_frequency');$s=[HarnessQpcR11]::Sample();$sw.WriteLine("BEGIN,$($s.pre),$($s.post),$($s.filetime),$qpcHz");$sw.Flush();$tick=0;$lastAliveQpc=[long]$s.post
  $mode=if($n -eq 31){'--n31'}else{'--smoke'}
  $quoted=@($mode,$cap,$Plan,$planSha)|ForEach-Object{'"'+$_+'"'}
  $harnessProc=Start-Process -FilePath $Executable -ArgumentList $quoted -WorkingDirectory $root -RedirectStandardOutput (Join-Path $cap 'harness.stdout.txt') -RedirectStandardError (Join-Path $cap 'harness.stderr.txt') -PassThru -WindowStyle Hidden
  while($true){
    $harnessProc.Refresh();$emonProc.Refresh()
    if($harnessProc.HasExited){$harnessProc.WaitForExit();$harnessRc=[int]$harnessProc.ExitCode;break}
    if($emonProc.HasExited -and !$earlyExit){$earlyExit=$true;"EMON_STOPPED_BEFORE_PLAN_END exit=UNEXPOSED_BY_STARTPROCESS`nlast_alive_qpc=$lastAliveQpc`nblocks_after_stop=INVALID"|Set-Content -LiteralPath (Join-Path $cap 'capture_early_stop.txt')}
    $s=[HarnessQpcR11]::Sample();$sw.WriteLine("MAP$tick,$($s.pre),$($s.post),$($s.filetime),$qpcHz");$sw.Flush();$tick++
    if(!$earlyExit){$emonProc.Refresh();if($emonProc.HasExited){$earlyExit=$true;"EMON_STOPPED_BEFORE_PLAN_END exit=UNEXPOSED_BY_STARTPROCESS`nlast_alive_qpc=$lastAliveQpc`nblocks_after_stop=INVALID"|Set-Content -LiteralPath (Join-Path $cap 'capture_early_stop.txt')}else{$lastAliveQpc=[long]$s.post}}
    Start-Sleep -Milliseconds 5
  }
  $s=[HarnessQpcR11]::Sample();$sw.WriteLine("END,$($s.pre),$($s.post),$($s.filetime),$qpcHz");$sw.Flush();$sw.Dispose();$sw=$null
  if(!$earlyExit){
    $captureDeadline=[DateTime]::UtcNow.AddSeconds([Math]::Max(600,$requiredSeconds*1.5))
    while($true){$emonProc.Refresh();if($emonProc.HasExited){break};if([DateTime]::UtcNow -gt $captureDeadline){throw 'EMON did not finish at configured group limit before deadline'};Start-Sleep -Seconds 2}
    $emonProc.WaitForExit();$emonProc.Refresh()
  }
  "harness_exit=$harnessRc`nemon_exit=CHECKED_BY_GROUP_COUNT_AND_FOOTER`nearly_exit=$earlyExit`nconfigured_groups=$emonGroups`n"|Set-Content -LiteralPath (Join-Path $cap 'capture_status.txt')
  if($harnessRc -ne 0){throw "harness exited nonzero: $harnessRc; see $cap\harness.stderr.txt"}
  $rawRows=@(Get-Content -LiteralPath (Join-Path $cap 'raw.jsonl')).Count;$summaryRows=@(Get-Content -LiteralPath (Join-Path $cap 'summary.jsonl')).Count;$expectedRows=4*$n*8;if($n -eq 3){$expectedRows+=8*$n*8};$expectedSummaries=if($n -eq 3){12}else{4};if($rawRows -ne $expectedRows -or $summaryRows -ne $expectedSummaries){throw "harness output incomplete: raw=$rawRows expected=$expectedRows summaries=$summaryRows expected_summaries=$expectedSummaries"}
  $emonText=Get-Content -LiteralPath $raw -Raw
  $groups=([regex]::Matches($emonText,'(?m)^==========\s*$')).Count
  $footer=$emonText -match '(?m)^\s*\d+\.\d+s real\s+\d+\.\d+s user\s+\d+\.\d+s system\s*$'
  if($groups -lt $emonGroups -or !$footer){$retryable=$true;$failureReason="EMON truncated: groups=$groups expected=$emonGroups footer=$footer";if(!$earlyExit){"EMON_CAPTURE_TRUNCATED_BEFORE_PLAN_END; blocks_after_last_complete_group=INVALID"|Set-Content -LiteralPath (Join-Path $cap 'capture_early_stop.txt');$earlyExit=$true}}
  $captureQuality=if($earlyExit){'EARLY_STOP_INVALID'}elseif($retryable){'TRUNCATED_INVALID'}else{'COMPLETE'}
  "captured_groups=$groups`nexpected_groups=$emonGroups`nfooter=$footer`nraw_bytes=$((Get-Item -LiteralPath $raw).Length)`nstatus=$captureQuality"|Set-Content -LiteralPath (Join-Path $cap 'capture_quality.txt')
  $schema=Join-Path $here 'validate_r11.py'
  & python $schema --raw (Join-Path $cap 'raw.jsonl') *> (Join-Path $cap 'schema_raw_prederive.txt');if($LASTEXITCODE -ne 0){throw "raw schema rejected; see $cap\schema_raw_prederive.txt"}
  & python $schema --summary (Join-Path $cap 'summary.jsonl') *> (Join-Path $cap 'schema_summary_prederive.txt');if($LASTEXITCODE -ne 0){throw "summary schema rejected; see $cap\schema_summary_prederive.txt"}
  & (Join-Path $here 'postprocess_r11.ps1') -Capture $cap
  if($LASTEXITCODE -ne 0){throw "postprocess failed for $cap"}
  $qualifier=Join-Path $root 'T0P\collector\qualify_r11.py'
  $qualifierOut=Join-Path $root ('T0P\collector\logs_r11\'+$id+'_qualification.json')
  New-Item -ItemType Directory -Force -Path (Split-Path $qualifierOut -Parent)|Out-Null
  & python $qualifier --raw (Join-Path $cap 'raw.jsonl') --summary (Join-Path $cap 'summary.jsonl') --plan $Plan --attempt $Attempt --output $qualifierOut *> (Join-Path $cap 'qualifier.stdout.txt')
  if($LASTEXITCODE -ne 0){throw "qualifier rejected harness outputs; see $cap\qualifier.stdout.txt"}
  Copy-Item -LiteralPath $qualifierOut -Destination (Join-Path $cap 'qualification.json') -Force
  $invalidBlocks=@(Get-Content -LiteralPath (Join-Path $cap 'telemetry_r11.txt')|Where-Object{$_ -match '^BLOCK .*status=INVALID_TELEMETRY'})
  if($invalidBlocks.Count -gt 0){$retryable=$true;if(!$failureReason){$failureReason="S-20 telemetry invalid blocks=$($invalidBlocks.Count)"}}
  if($earlyExit){$retryable=$true;"invalid_blocks=$($invalidBlocks.Count)`nreason=EMON_PROCESS_STOPPED_BEFORE_PLAN_END`nstatus=INVALID"|Set-Content -LiteralPath (Join-Path $cap 'early_stop_block_report.txt')}
  & python $schema --self-test *> (Join-Path $cap 'schema_selftest.txt');if($LASTEXITCODE -ne 0){throw 'strict JSONL negative tests failed'}
  if(!$retryable){Write-Output "SMOKE_CAPTURE=$cap"}
}catch{
  $message=$_.Exception.Message
  if($message -match 'EMON|telemetry|postprocess|raw schema|summary schema'){$retryable=$true;$failureReason=$message}
  else{throw}
}finally{
  if($sw){$sw.Dispose()}
  if($harnessProc){$harnessProc.Refresh();if(!$harnessProc.HasExited){Stop-Process -Id $harnessProc.Id -Force}}
  if($emonProc){$emonProc.Refresh();if(!$emonProc.HasExited){"EMON_NOT_FINISHED_CONFIGURED_GROUP_LIMIT=$emonGroups"|Set-Content -LiteralPath (Join-Path $cap 'capture_status.txt')}}
}
if($retryable){
  "attempt=$Attempt`nstatus=INVALID_INSTRUMENTATION`nreason=$failureReason`nresidency=NOT_STARTED"|Set-Content -LiteralPath (Join-Path $cap 'qualification_status.txt')
  if($Attempt -eq 1){
    "attempt1=$cap`nreason=$failureReason`nretry=one_global_same_plan_seed_order`nresidency=NOT_STARTED"|Set-Content -LiteralPath (Join-Path $here 'logs_r11\GLOBAL_RETRY_STATUS.txt')
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Executable $Executable -Plan $Plan -Collector $Collector -SampleCount $SampleCount -Attempt 2
    exit $LASTEXITCODE
  }
  "attempt2=$cap`nstate=HOLD_INSTRUMENTATION`ncampaign=CLOSED_Q4_256_THIS_I7`nresidency=NOT_STARTED`nreason=$failureReason"|Set-Content -LiteralPath (Join-Path $here 'logs_r11\GLOBAL_RETRY_STATUS.txt')
  throw "HOLD_INSTRUMENTATION after the single global retry: $failureReason"
}
"attempt=$Attempt`nstate=SMOKE_TELEMETRY_ANALYZABLE`ninvalid_blocks=0`nresidency=NOT_STARTED"|Set-Content -LiteralPath (Join-Path $cap 'qualification_status.txt')
if($Attempt -eq 2){"attempt1=INVALID_RETRIED`nattempt2=$cap`nstate=SMOKE_TELEMETRY_ANALYZABLE`nresidency=NOT_STARTED"|Set-Content -LiteralPath (Join-Path $here 'logs_r11\GLOBAL_RETRY_STATUS.txt')}
