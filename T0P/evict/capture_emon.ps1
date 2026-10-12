param([string]$CaptureDirectory='capture')
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$telemetry=Join-Path (Split-Path $root -Parent) 'telemetry'
$emon='C:\Program Files (x86)\Intel\oneAPI\vtune\2026.4\bin64\emon.exe'
$cfg=Join-Path $telemetry 'reader\emon_joint_qualification.edp'
$cap=Join-Path $root $CaptureDirectory
New-Item -ItemType Directory -Force -Path $cap|Out-Null
$raw=Join-Path $cap 'emon_joint_raw.txt';$err=Join-Path $cap 'emon_joint_stderr.txt'
$workerOut=Join-Path $cap 'evict_qa_raw.txt';$workerErr=Join-Path $cap 'evict_qa_stderr.txt'
$map=Join-Path $cap 'clock_map.csv';$status=Join-Path $cap 'capture_status.txt'
foreach($p in @($raw,$err,$workerOut,$workerErr,$map,$status)){if(Test-Path -LiteralPath $p){throw "Refusing to overwrite evidence: $p"}}
if(!(Test-Path -LiteralPath $emon)){throw "EMON missing: $emon"}
if(!(Test-Path -LiteralPath $cfg)){throw "Official joint EDP missing: $cfg"}
$env:PATH="$(Split-Path $emon);$env:PATH"
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public struct ClockSample { public long pre; public long post; public long filetime; }
public static class EvictClock {
 [DllImport("kernel32.dll")] public static extern bool QueryPerformanceCounter(out long x);
 [DllImport("kernel32.dll")] public static extern bool QueryPerformanceFrequency(out long x);
 [DllImport("kernel32.dll")] public static extern void GetSystemTimePreciseAsFileTime(out long x);
 public static ClockSample GetSample(){ClockSample s;QueryPerformanceCounter(out s.pre);GetSystemTimePreciseAsFileTime(out s.filetime);QueryPerformanceCounter(out s.post);return s;}
}
'@
$emonProc=Start-Process -FilePath $emon -ArgumentList ('-collect-edp "edp_file={0}"' -f $cfg) -WorkingDirectory $telemetry -RedirectStandardOutput $raw -RedirectStandardError $err -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2
if($emonProc.HasExited){"EMON_EXIT_EARLY=$($emonProc.ExitCode)"|Set-Content $status;exit 3}
$exe=Join-Path $root 'evict_qa.exe'
$worker=Start-Process -FilePath $exe -WorkingDirectory $cap -RedirectStandardOutput $workerOut -RedirectStandardError $workerErr -PassThru -WindowStyle Hidden
$sw=[IO.StreamWriter]::new($map,$false,[Text.UTF8Encoding]::new($false));$sw.WriteLine('sample,qpc_pre,qpc_post,filetime_100ns');
function Sample([string]$tag){$s=[EvictClock]::GetSample();$sw.WriteLine("$tag,$($s.pre),$($s.post),$($s.filetime)");$sw.Flush()}
Sample 'BEGIN'
while(-not $emonProc.HasExited){Sample 'MAP';Start-Sleep -Milliseconds 5}
Sample 'END';$sw.Dispose()
$worker.WaitForExit();$emonProc.Refresh()
$txt=Get-Content -LiteralPath $raw -Raw
$groups=([regex]::Matches($txt,'(?m)^==========\s*$')).Count
$ok=($groups -eq 3600 -and $txt -match '(?m)^\s*\d+\.\d+s real\s+\d+\.\d+s user\s+\d+\.\d+s system\s*$' -and (Get-Item $err).Length -eq 0)
@("EMON_EXIT=$($emonProc.ExitCode)","EMON_GROUPS=$groups","EMON_3600_AND_FOOTER=$ok","WORKER_EXIT=$($worker.ExitCode)","CONFIG=$cfg","RAW=$raw","CLOCK_MAP=$map","WORKER_RAW=$workerOut","WORKER_STDERR=$workerErr")|Set-Content -LiteralPath $status -Encoding utf8
Get-Content $status
