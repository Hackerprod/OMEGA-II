param([string]$Executable=(Join-Path $PSScriptRoot 'harness_r9.exe'),[string]$Plan=(Join-Path $PSScriptRoot '..\collector\plan.json'),[string]$Collector=(Join-Path $PSScriptRoot '..\collector\collector.exe'),[ValidateSet(3,31)][int]$SampleCount=3)
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$root=Split-Path (Split-Path $here -Parent) -Parent
if(!(Test-Path -LiteralPath $collector)){throw "collector missing: $collector"}
$planCheckLog=Join-Path $here ('logs_r9\plan_before_calibration_'+[IO.Path]::GetFileNameWithoutExtension($Plan)+'.txt')
& $collector --plan-check $plan *> $planCheckLog
if($LASTEXITCODE -ne 0){throw "plan rejected before calibration; see $planCheckLog"}
$planObj=Get-Content -LiteralPath $plan -Raw|ConvertFrom-Json
$planSha=[string]$planObj.sha256
if($planSha -notmatch '^[0-9a-f]{64}$'){throw 'plan sha256 is invalid'}
$planObj=$null
$emon='C:\Program Files (x86)\Intel\oneAPI\vtune\2026.4\bin64\emon.exe'
$edp=Join-Path $root 'T0P\telemetry\reader\emon_joint_qualification.edp'
$env:PATH=(Join-Path $root 'T0P\ref_ggml\build_llama\bin')+';'+(Join-Path (Split-Path $root -Parent) '.venv_cuda\Lib\site-packages\torch\lib')+';'+(Split-Path $emon -Parent)+';'+$env:PATH
$id='smoke_'+(Get-Date -Format 'yyyyMMdd_HHmmss')
$cap=Join-Path $here ('logs_r9\'+$id)
New-Item -ItemType Directory -Force -Path $cap|Out-Null
$raw=Join-Path $cap 'emon_stdout.txt';$err=Join-Path $cap 'emon_stderr.txt';$mapPath=Join-Path $cap 'clock_map.csv';$launcher=Join-Path $here 'launch_smoke_r9.cmd'
if(!(Test-Path -LiteralPath $emon)){throw "EMON missing: $emon"};if(!(Test-Path -LiteralPath $edp)){throw "EDP missing: $edp"}
Add-Type -TypeDefinition @'
using System;using System.Runtime.InteropServices;
public struct HarnessClockSample{public long pre;public long post;public long filetime;}
public static class HarnessClock{[DllImport("kernel32.dll")]public static extern bool QueryPerformanceCounter(out long x);[DllImport("kernel32.dll")]public static extern bool QueryPerformanceFrequency(out long x);[DllImport("kernel32.dll")]public static extern void GetSystemTimePreciseAsFileTime(out long x);public static HarnessClockSample Sample(){HarnessClockSample s;QueryPerformanceCounter(out s.pre);GetSystemTimePreciseAsFileTime(out s.filetime);QueryPerformanceCounter(out s.post);return s;}}
'@
$proc=$null;$sw=$null
try{
  $proc=Start-Process -FilePath $emon -ArgumentList @('-collect-edp',"edp_file=$edp") -WorkingDirectory (Split-Path $edp -Parent) -RedirectStandardOutput $raw -RedirectStandardError $err -PassThru -WindowStyle Hidden
  Start-Sleep -Seconds 2;$proc.Refresh();if($proc.HasExited){throw "EMON CLI exited early=$($proc.ExitCode); see $err"}
  $qpcHz=[long]0;if(![HarnessClock]::QueryPerformanceFrequency([ref]$qpcHz)-or $qpcHz -le 0){throw 'QueryPerformanceFrequency unavailable'}
  $sw=[IO.StreamWriter]::new($mapPath,$false,[Text.UTF8Encoding]::new($false));$sw.WriteLine('sample,qpc_pre,qpc_post,filetime_100ns,qpc_frequency');$n=0
  $s=[HarnessClock]::Sample();$sw.WriteLine("BEGIN,$($s.pre),$($s.post),$($s.filetime),$qpcHz");$sw.Flush()
  $mode=if($SampleCount -eq 31){'--n31'}else{'--smoke'}
  $args='/d /c ""'+$launcher+'" "'+$Executable+'" "'+$cap+'" "'+$plan+'" "'+$planSha+'" "'+$mode+'""'
  $hp=Start-Process -FilePath $env:ComSpec -ArgumentList $args -WorkingDirectory $root -PassThru -WindowStyle Hidden
  while(-not $hp.HasExited){$s=[HarnessClock]::Sample();$sw.WriteLine("MAP$n,$($s.pre),$($s.post),$($s.filetime),$qpcHz");$sw.Flush();$n++;Start-Sleep -Milliseconds 5;$hp.Refresh()}
  $hp.WaitForExit();$hp.Refresh();$s=[HarnessClock]::Sample();$sw.WriteLine("END,$($s.pre),$($s.post),$($s.filetime),$qpcHz");$sw.Flush();$sw.Dispose();$sw=$null
  $exitFile=Join-Path $cap 'harness.exit.txt';if(!(Test-Path -LiteralPath $exitFile)){throw "SMOKE launcher exited without code file; see $cap"};$rc=[int](Get-Content -LiteralPath $exitFile -Raw);if($rc -ne 0){throw "harness_r9.exe --smoke exited $rc; see $cap\harness.stdout.txt and $cap\harness.stderr.txt"}
  $schema=Join-Path $here 'validate_r9.py'
  & python $schema --raw (Join-Path $cap 'raw.jsonl') *> (Join-Path $cap 'schema_raw_prederive.txt');if($LASTEXITCODE -ne 0){throw "raw JSONL schema rejected before derivation; see $cap\schema_raw_prederive.txt"}
  & python $schema --summary (Join-Path $cap 'summary.jsonl') *> (Join-Path $cap 'schema_summary_prederive.txt');if($LASTEXITCODE -ne 0){throw "summary schema rejected before derivation; see $cap\schema_summary_prederive.txt"}
  $rows=Get-Content (Join-Path $cap 'raw.jsonl')|ForEach-Object{$_|ConvertFrom-Json}|ForEach-Object{[pscustomobject]@{Pcore=[int]($_.lp/2);replica=$_.replica;condition='arch_evict8';qpc_pad_start=$_.t_pad_ini_qpc;qpc_start=$_.t_ini_qpc;qpc_end=$_.t_fin_qpc;qpc_pad_end=$_.t_pad_fin_qpc;provider=$_.provider}}
  $rows|Export-Csv -NoTypeInformation -Encoding utf8 -LiteralPath (Join-Path $cap 'eviction_windows.csv')
}finally{
  if($sw){$sw.Dispose()};if($proc){$proc.Refresh();if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force;Start-Sleep -Milliseconds 500};$proc.Refresh();"EMON_EXIT=$($proc.ExitCode)"|Set-Content -LiteralPath (Join-Path $cap 'capture_status.txt')}
}
& (Join-Path $here 'postprocess_r9.ps1') -Capture $cap
if($LASTEXITCODE -ne 0){throw "postprocess_r9.ps1 failed for $cap"}
Write-Output "SMOKE_CAPTURE=$cap"