param()
$ErrorActionPreference='Stop'
$telemetry=Split-Path -Parent $PSScriptRoot
$emon='C:\Program Files (x86)\Intel\oneAPI\vtune\2026.4\bin64\emon.exe'
$apiBin='C:\Program Files (x86)\Intel\oneAPI\vtune\2026.4\bin64'
$reader=Join-Path $telemetry 'reader\telemetry_reader.exe'
$fullCfg=Join-Path $telemetry 'reader\emon_joint_qualification.edp'
$log=Join-Path $telemetry 'logs'
$fullOut=Join-Path $log 'conditioning_joint_raw_r3.txt'
$fullErr=Join-Path $log 'conditioning_joint_stderr_r3.txt'
$dummyMap=Join-Path $log 'conditioning_qpc_utc_map_r3.csv'
$dummyOut=Join-Path $log 'conditioning_dummy_console_r3.txt'
$dummyErr=Join-Path $log 'conditioning_dummy_stderr_r3.txt'
$status=Join-Path $log 'conditioning_run_status_r3.txt'
foreach($p in @($fullOut,$fullErr,$dummyMap,$dummyOut,$dummyErr,$status)){if(Test-Path -LiteralPath $p){throw "Refusing to overwrite existing evidence: $p"}}
if(!(Test-Path -LiteralPath $emon)){throw "EMON missing: $emon"}
if(!(Test-Path -LiteralPath $reader)){throw "Reader missing: $reader"}
$env:PATH="$apiBin;$env:PATH"
$collector=Start-Process -FilePath $emon -ArgumentList ('-collect-edp "edp_file={0}"' -f $fullCfg) -WorkingDirectory $telemetry -RedirectStandardOutput $fullOut -RedirectStandardError $fullErr -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2
if($collector.HasExited){"JOINT_EMON_EXIT_EARLY=$($collector.ExitCode)"|Set-Content -LiteralPath $status;throw "Joint EMON collector exited before DUMMY start; see $fullOut / $fullErr"}
& $reader --dummy 180 $dummyMap 1> $dummyOut 2> $dummyErr
$dummyExit=$LASTEXITCODE
$collector.WaitForExit();$collector.Refresh();$collectorExit=$collector.ExitCode
$rawText=Get-Content -LiteralPath $fullOut -Raw
$groupCount=([regex]::Matches($rawText,'(?m)^==========\s*$')).Count
$completeByOutput=($groupCount -eq 3600 -and $rawText -match '(?m)^\s*\d+\.\d+s real\s+\d+\.\d+s user\s+\d+\.\d+s system\s*$' -and (Get-Item -LiteralPath $fullErr).Length -eq 0)
$emonStatus=if($completeByOutput){'COMPLETE_BY_3600_GROUPS_AND_TIMING_FOOTER'}else{'INCOMPLETE'}
@("DUMMY_EXIT=$dummyExit","EMON_JOINT_EXIT=$collectorExit","EMON_STATUS=$emonStatus","EMON_GROUPS=$groupCount","DUMMY_DURATION_SECONDS=180","DUMMY_MAP=$dummyMap","EMON_CONFIG=$fullCfg","EMON_RAW=$fullOut","EMON_STDERR=$fullErr","DUMMY=TRUE; throughput not recorded")|Set-Content -LiteralPath $status -Encoding utf8
Get-Content -LiteralPath $status
if($dummyExit -ne 0){throw "DUMMY conditioning failed with exit $dummyExit; see $dummyOut / $dummyErr"}
if(-not $completeByOutput){throw "Joint EMON capture is incomplete; groups=$groupCount; see $fullOut / $fullErr"}
