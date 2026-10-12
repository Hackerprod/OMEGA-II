param([string]$Executable=(Join-Path $PSScriptRoot 'harness_r9.exe'),[string]$Plan=(Join-Path $PSScriptRoot '..\collector\plan.json'),[string]$Collector=(Join-Path $PSScriptRoot '..\collector\collector.exe'))
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$root=Split-Path (Split-Path $here -Parent) -Parent
if(!(Test-Path -LiteralPath $Executable)){throw "harness missing: $Executable"}
if(!(Test-Path -LiteralPath $Collector)){throw "collector missing: $Collector"}
$id='official_'+(Get-Date -Format 'yyyyMMdd_HHmmss')
$cap=Join-Path $here ('logs_r9\'+$id)
New-Item -ItemType Directory -Force -Path $cap|Out-Null
& $Collector --plan-check $Plan *> (Join-Path $cap 'plan_check_before_calibration.txt')
if($LASTEXITCODE -ne 0){throw "plan rejected before authorization/calibration; see $cap\plan_check_before_calibration.txt"}
$planObj=Get-Content -LiteralPath $Plan -Raw|ConvertFrom-Json
$planSha=[string]$planObj.sha256
if($planSha -notmatch '^[0-9a-f]{64}$'){throw 'plan sha256 invalid'}
$emon='C:\Program Files (x86)\Intel\oneAPI\vtune\2026.4\bin64\emon.exe'
$edp=Join-Path $root 'T0P\telemetry\reader\emon_joint_qualification.edp'
$env:PATH=(Join-Path $root 'T0P\ref_ggml\build_llama\bin')+';'+(Join-Path (Split-Path $root -Parent) '.venv_cuda\Lib\site-packages\torch\lib')+';'+(Split-Path $emon -Parent)+';'+$env:PATH
$gate=Join-Path $here 'launch_auth_gate_r9.cmd'
$gateArgs='/d /c ""'+$gate+'" "'+$Executable+'" "'+$cap+'""'
$gp=Start-Process -FilePath $env:ComSpec -ArgumentList $gateArgs -WorkingDirectory $root -PassThru -Wait -WindowStyle Hidden
$gateExit=Join-Path $cap 'auth_gate.exit.txt'
if(!(Test-Path -LiteralPath $gateExit)){throw "official authorization gate produced no exit code; see $cap"}
$gateRc=[int](Get-Content -LiteralPath $gateExit -Raw)
if($gateRc -ne 0){throw "--official rejected before EMON; exit=$gateRc; see $cap\auth_gate.stderr.txt"}
if(!(Test-Path -LiteralPath $emon)){throw "EMON missing: $emon"};if(!(Test-Path -LiteralPath $edp)){throw "EDP missing: $edp"}
Add-Type -TypeDefinition @'
using System;using System.Runtime.InteropServices;
public struct HarnessOfficialClockSample{public long pre;public long post;public long filetime;}
public static class HarnessOfficialClock{[DllImport("kernel32.dll")]public static extern bool QueryPerformanceCounter(out long x);[DllImport("kernel32.dll")]public static extern bool QueryPerformanceFrequency(out long x);[DllImport("kernel32.dll")]public static extern void GetSystemTimePreciseAsFileTime(out long x);public static HarnessOfficialClockSample Sample(){HarnessOfficialClockSample s;QueryPerformanceCounter(out s.pre);GetSystemTimePreciseAsFileTime(out s.filetime);QueryPerformanceCounter(out s.post);return s;}}
'@
$proc=$null;$sw=$null
try{
  $proc=Start-Process -FilePath $emon -ArgumentList @('-collect-edp',"edp_file=$edp") -WorkingDirectory (Split-Path $edp -Parent) -RedirectStandardOutput (Join-Path $cap 'emon_stdout.txt') -RedirectStandardError (Join-Path $cap 'emon_stderr.txt') -PassThru -WindowStyle Hidden
  Start-Sleep -Seconds 2;$proc.Refresh();if($proc.HasExited){throw "EMON CLI exited early=$($proc.ExitCode); see $cap\emon_stderr.txt"}
  $qpcHz=[long]0;if(![HarnessOfficialClock]::QueryPerformanceFrequency([ref]$qpcHz)-or $qpcHz -le 0){throw 'QueryPerformanceFrequency unavailable'}
  $sw=[IO.StreamWriter]::new((Join-Path $cap 'clock_map.csv'),$false,[Text.UTF8Encoding]::new($false));$sw.WriteLine('sample,qpc_pre,qpc_post,filetime_100ns,qpc_frequency');$n=0
  $s=[HarnessOfficialClock]::Sample();$sw.WriteLine("BEGIN,$($s.pre),$($s.post),$($s.filetime),$qpcHz");$sw.Flush()
  $env:HARNESS_R9_EMON_CAPTURE='1';$launcher=Join-Path $here 'launch_smoke_r9.cmd'
  $args='/d /c ""'+$launcher+'" "'+$Executable+'" "'+$cap+'" "'+$Plan+'" "'+$planSha+'" "--official""'
  $hp=Start-Process -FilePath $env:ComSpec -ArgumentList $args -WorkingDirectory $root -PassThru -WindowStyle Hidden
  while(-not $hp.HasExited){$s=[HarnessOfficialClock]::Sample();$sw.WriteLine("MAP$n,$($s.pre),$($s.post),$($s.filetime),$qpcHz");$sw.Flush();$n++;Start-Sleep -Milliseconds 5;$hp.Refresh()}
  $hp.WaitForExit();$hp.Refresh();$s=[HarnessOfficialClock]::Sample();$sw.WriteLine("END,$($s.pre),$($s.post),$($s.filetime),$qpcHz");$sw.Flush();$sw.Dispose();$sw=$null
  $exitFile=Join-Path $cap 'harness.exit.txt';if(!(Test-Path -LiteralPath $exitFile)){throw "official harness exited without code file; see $cap"};$rc=[int](Get-Content -LiteralPath $exitFile -Raw);if($rc -ne 0){throw "harness --official exited $rc; see $cap\harness.stdout.txt and $cap\harness.stderr.txt"}
  Copy-Item -LiteralPath (Join-Path $cap 'geometry_raw.jsonl') -Destination (Join-Path $cap 'raw.jsonl')
  Copy-Item -LiteralPath (Join-Path $cap 'geometry_summary.jsonl') -Destination (Join-Path $cap 'summary.jsonl')
  & python (Join-Path $here 'validate_r9.py') --raw (Join-Path $cap 'raw.jsonl') *> (Join-Path $cap 'schema_raw_prederive.txt');if($LASTEXITCODE -ne 0){throw "official raw schema rejected; see $cap\schema_raw_prederive.txt"}
  & python (Join-Path $here 'validate_r9.py') --summary (Join-Path $cap 'summary.jsonl') *> (Join-Path $cap 'schema_summary_prederive.txt');if($LASTEXITCODE -ne 0){throw "official summary schema rejected; see $cap\schema_summary_prederive.txt"}
}finally{
  if($sw){$sw.Dispose()};if($proc){$proc.Refresh();if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force;Start-Sleep -Milliseconds 500};$proc.Refresh();"EMON_EXIT=$($proc.ExitCode)"|Set-Content -LiteralPath (Join-Path $cap 'capture_status.txt')}
}
& (Join-Path $here 'postprocess_r9.ps1') -Capture $cap
if($LASTEXITCODE -ne 0){throw "postprocess_r9.ps1 failed for $cap"}
Write-Output "OFFICIAL_CAPTURE=$cap"