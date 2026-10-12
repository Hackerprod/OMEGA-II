param()
$ErrorActionPreference='Stop'
$telemetry=Split-Path -Parent $PSScriptRoot
$emon='C:\Program Files (x86)\Intel\oneAPI\vtune\2026.4\bin64\emon.exe'
$apiBin='C:\Program Files (x86)\Intel\oneAPI\vtune\2026.4\bin64'
$reader=Join-Path $telemetry 'reader\telemetry_reader.exe'
$tccCfg=Join-Path $telemetry 'reader\emon_tcc_preflight.edp'
$fullCfg=Join-Path $telemetry 'reader\emon_joint_qualification.edp'
$log=Join-Path $telemetry 'logs'
$preOut=Join-Path $log 'tcc_preflight_raw.txt'
$preErr=Join-Path $log 'tcc_preflight_stderr.txt'
$fullOut=Join-Path $log 'conditioning_joint_raw.txt'
$fullErr=Join-Path $log 'conditioning_joint_stderr.txt'
$dummyMap=Join-Path $log 'conditioning_qpc_utc_map.csv'
$dummyOut=Join-Path $log 'conditioning_dummy_console.txt'
$dummyErr=Join-Path $log 'conditioning_dummy_stderr.txt'
$status=Join-Path $log 'conditioning_run_status_full.txt'
foreach($p in @($fullOut,$fullErr,$dummyMap,$dummyOut,$dummyErr,$status)){if(Test-Path -LiteralPath $p){throw "Refusing to overwrite existing evidence: $p"}}
if(!(Test-Path -LiteralPath $emon)){throw "EMON missing: $emon"}
if(!(Test-Path -LiteralPath $reader)){throw "Reader missing: $reader"}
$env:PATH="$apiBin;$env:PATH"
if(Test-Path -LiteralPath $preOut){
  $tccText=Get-Content -LiteralPath $preOut -Raw
  if($tccText -notmatch 'MSR_EVENT:msr=0x1A2:type=STATIC:scope=THREAD' -or $tccText -match 'ERROR with preparing|Event configuration failed|Failed to verify') {throw "Existing TCC preflight log is incomplete: $preOut"}
  $preExit='REUSED_EXISTING_VALID_LOG'
}else{
  $pre=Start-Process -FilePath $emon -ArgumentList ('-collect-edp "edp_file={0}"' -f $tccCfg) -WorkingDirectory $telemetry -RedirectStandardOutput $preOut -RedirectStandardError $preErr -PassThru -Wait -WindowStyle Hidden
  $preExit=$pre.ExitCode
  if($preExit -ne 0){"TCC_PREFLIGHT_EXIT=$preExit"|Set-Content -LiteralPath $status;throw "TCC preflight failed; see $preOut / $preErr"}
}
$collector=Start-Process -FilePath $emon -ArgumentList ('-collect-edp "edp_file={0}"' -f $fullCfg) -WorkingDirectory $telemetry -RedirectStandardOutput $fullOut -RedirectStandardError $fullErr -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2
if($collector.HasExited){"JOINT_EMON_EXIT_EARLY=$($collector.ExitCode)"|Set-Content -LiteralPath $status;throw "Joint EMON collector exited before DUMMY start; see $fullOut / $fullErr"}
$dummyArgs=@('--dummy','180',$dummyMap)
& $reader @dummyArgs 1> $dummyOut 2> $dummyErr
$dummyExit=$LASTEXITCODE
$collector.WaitForExit();$collector.Refresh();$collectorExit=$collector.ExitCode
$rawText=Get-Content -LiteralPath $fullOut -Raw
$groupCount=([regex]::Matches($rawText,'(?m)^==========\s*$')).Count
$completeByOutput=($groupCount -eq 3600 -and $rawText -match '(?m)^\s*\d+\.\d+s real\s+\d+\.\d+s user\s+\d+\.\d+s system\s*$' -and (Get-Item -LiteralPath $fullErr).Length -eq 0)
$emonStatus=if($completeByOutput){'COMPLETE_BY_3600_GROUPS_AND_TIMING_FOOTER'}else{'INCOMPLETE'}
$exitText=if($null -eq $collectorExit){'UNAVAILABLE'}else{[string]$collectorExit}
@("TCC_PREFLIGHT_EXIT=$preExit","DUMMY_EXIT=$dummyExit","EMON_JOINT_EXIT=$exitText","EMON_STATUS=$emonStatus","EMON_GROUPS=$groupCount","DUMMY_DURATION_SECONDS=180","DUMMY_MAP=$dummyMap","EMON_CONFIG=$fullCfg","EMON_RAW=$fullOut","EMON_STDERR=$fullErr","TCC_PREFLIGHT_RAW=$preOut","DUMMY=TRUE; throughput not recorded")|Set-Content -LiteralPath $status -Encoding utf8
if($dummyExit -ne 0){throw "DUMMY conditioning failed with exit $dummyExit; see $dummyOut / $dummyErr"}
if(-not $completeByOutput){throw "Joint EMON capture is incomplete; groups=$groupCount; see $fullOut / $fullErr"}
Get-Content -LiteralPath $status
