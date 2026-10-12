param([ValidateSet('weights','nonweights','idle')][string]$Mode,[Parameter(Mandatory=$true)][string]$CaptureDirectory)
$ErrorActionPreference='Stop';$root=$PSScriptRoot;$cap=Join-Path $root $CaptureDirectory
if(Test-Path -LiteralPath $cap){throw "Refusing to overwrite: $cap"};New-Item -ItemType Directory -Path $cap|Out-Null
$emon='C:\Program Files (x86)\Intel\oneAPI\vtune\2026.4\bin64\emon.exe';$telemetry=Join-Path (Split-Path $root -Parent) 'telemetry';$cfg=Join-Path $telemetry 'reader\emon_joint_qualification.edp';$exe=Join-Path $root 'diag_control_r5.exe'
foreach($p in @($emon,$cfg,$exe)){if(!(Test-Path -LiteralPath $p)){throw "required input missing: $p"}}
$raw=Join-Path $cap 'emon_stdout.txt';$err=Join-Path $cap 'emon_stderr.txt';$out=Join-Path $cap 'control.stdout.txt';$workerErr=Join-Path $cap 'control.stderr.txt';$map=Join-Path $cap 'clock_map.csv';$status=Join-Path $cap 'capture_status.txt'
$proc=Start-Process -FilePath $emon -ArgumentList ('-collect-edp "edp_file={0}"' -f $cfg) -WorkingDirectory $telemetry -RedirectStandardOutput $raw -RedirectStandardError $err -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2;if($proc.HasExited){"EMON_EARLY_EXIT=$($proc.ExitCode)"|Set-Content $status;exit 3}
$worker=Start-Process -FilePath $exe -ArgumentList $Mode -WorkingDirectory $cap -RedirectStandardOutput $out -RedirectStandardError $workerErr -PassThru -WindowStyle Hidden
Add-Type -TypeDefinition @'
using System;using System.Runtime.InteropServices;public static class DClock{[DllImport("kernel32.dll")]public static extern bool QueryPerformanceCounter(out long x);[DllImport("kernel32.dll")]public static extern void GetSystemTimePreciseAsFileTime(out long x);}
'@
$sw=[IO.StreamWriter]::new($map,$false,[Text.UTF8Encoding]::new($false));$sw.WriteLine('sample,qpc_pre,qpc_post,filetime_100ns');$n=0
while(-not $proc.HasExited){$a=0L;$b=0L;$ft=0L;[DClock]::QueryPerformanceCounter([ref]$a)|Out-Null;[DClock]::GetSystemTimePreciseAsFileTime([ref]$ft);[DClock]::QueryPerformanceCounter([ref]$b)|Out-Null;$sw.WriteLine("MAP$n,$a,$b,$ft");$sw.Flush();$n++;Start-Sleep -Milliseconds 5};$sw.Dispose();$worker.WaitForExit();$proc.Refresh()
$txt=Get-Content -LiteralPath $raw -Raw;$groups=([regex]::Matches($txt,'(?m)^==========\s*$')).Count;$footer=$txt -match '(?m)^\s*\d+\.\d+s real\s+\d+\.\d+s user\s+\d+\.\d+s system\s*$'
@("MODE=$Mode","EMON_EXIT=$($proc.ExitCode)","EMON_GROUPS=$groups","EMON_FOOTER=$footer","WORKER_EXIT=$($worker.ExitCode)","EDP_SHA256=$((Get-FileHash $cfg -Algorithm SHA256).Hash.ToLowerInvariant())")|Set-Content -LiteralPath $status -Encoding ascii
Get-Content $status;if($groups -ne 3600 -or !$footer -or $worker.ExitCode -ne 0){exit 2}
