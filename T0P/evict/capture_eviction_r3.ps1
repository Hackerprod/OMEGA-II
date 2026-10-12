param([ValidateSet('qualification','revalidation')][string]$Mode='qualification',[Parameter(Mandatory=$true)][string]$CaptureDirectory)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$telemetry=Join-Path (Split-Path $root -Parent) 'telemetry'
$emon='C:\Program Files (x86)\Intel\oneAPI\vtune\2026.4\bin64\emon.exe'
$cfg=Join-Path $telemetry 'reader\emon_joint_qualification.edp'
$cap=Join-Path $root $CaptureDirectory
if(Test-Path -LiteralPath $cap){throw "Refusing to overwrite qualification evidence: $cap"}
New-Item -ItemType Directory -Path $cap|Out-Null
foreach($p in @($emon,$cfg,(Join-Path $root 'evict_qualification_r3.exe'),(Join-Path $root 'evict.cpp'),(Join-Path $root 'evict_r3.obj'))){if(!(Test-Path -LiteralPath $p)){throw "required installed or built input missing: $p"}}
$raw=Join-Path $cap 'emon_stdout.txt';$err=Join-Path $cap 'emon_stderr.txt';$workerOut=Join-Path $cap 'harness.stdout.txt';$workerErr=Join-Path $cap 'harness.stderr.txt';$map=Join-Path $cap 'clock_map.csv';$status=Join-Path $cap 'capture_status.txt';$identity=Join-Path $cap 'identity.txt'
$sourceHash=(Get-FileHash -LiteralPath (Join-Path $root 'evict.cpp') -Algorithm SHA256).Hash.ToLowerInvariant();$objectHash=(Get-FileHash -LiteralPath (Join-Path $root 'evict_r3.obj') -Algorithm SHA256).Hash.ToLowerInvariant();$exeHash=(Get-FileHash -LiteralPath (Join-Path $root 'evict_qualification_r3.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
@("LINKED_OBJECT=evict_r3.obj","SOURCE_SHA256=$sourceHash","OBJECT_SHA256=$objectHash","EXECUTABLE_SHA256=$exeHash","BUILD_INPUTS=evict.cpp+evict_qualification_r3.cpp","EDP=$cfg")|Set-Content -LiteralPath $identity -Encoding ascii
$env:PATH="$(Split-Path $emon);$env:PATH"
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public struct EvictR3ClockSample { public long pre; public long post; public long filetime; }
public static class EvictR3Clock {
 [DllImport("kernel32.dll")] public static extern bool QueryPerformanceCounter(out long x);
 [DllImport("kernel32.dll")] public static extern bool QueryPerformanceFrequency(out long x);
 [DllImport("kernel32.dll")] public static extern void GetSystemTimePreciseAsFileTime(out long x);
 public static EvictR3ClockSample GetSample(){EvictR3ClockSample s;QueryPerformanceCounter(out s.pre);GetSystemTimePreciseAsFileTime(out s.filetime);QueryPerformanceCounter(out s.post);return s;}
}
'@
$emonProc=Start-Process -FilePath $emon -ArgumentList ('-collect-edp "edp_file={0}"' -f $cfg) -WorkingDirectory $telemetry -RedirectStandardOutput $raw -RedirectStandardError $err -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2
if($emonProc.HasExited){@("EMON_EXIT_EARLY=$($emonProc.ExitCode)","CONFIG=$cfg")|Set-Content -LiteralPath $status -Encoding ascii;exit 3}
$exe=Join-Path $root 'evict_qualification_r3.exe';$arg=if($Mode -eq 'qualification'){'--qualification'}else{'--revalidation'}
$worker=Start-Process -FilePath $exe -ArgumentList $arg -WorkingDirectory $cap -RedirectStandardOutput $workerOut -RedirectStandardError $workerErr -PassThru -WindowStyle Hidden
$sw=[IO.StreamWriter]::new($map,$false,[Text.UTF8Encoding]::new($false));$sw.WriteLine('sample,qpc_pre,qpc_post,filetime_100ns');$index=0
function Sample([string]$tag){$s=[EvictR3Clock]::GetSample();$sw.WriteLine("$tag,$($s.pre),$($s.post),$($s.filetime)");$sw.Flush()}
Sample 'BEGIN'
while(-not $emonProc.HasExited){Sample "MAP$index";$index++;Start-Sleep -Milliseconds 5}
Sample 'END';$sw.Dispose();$worker.WaitForExit();$emonProc.Refresh()
$txt=Get-Content -LiteralPath $raw -Raw;$groups=([regex]::Matches($txt,'(?m)^==========\s*$')).Count
$footer=$txt -match '(?m)^\s*\d+\.\d+s real\s+\d+\.\d+s user\s+\d+\.\d+s system\s*$'
$ok=($groups -eq 3600 -and $footer -and (Get-Item -LiteralPath $err).Length -eq 0)
@("MODE=$Mode","EMON_EXIT=$($emonProc.ExitCode)","EMON_GROUPS=$groups","EMON_3600_AND_FOOTER=$ok","WORKER_EXIT=$($worker.ExitCode)","CONFIG=$cfg","EDP_SHA256=$((Get-FileHash -LiteralPath $cfg -Algorithm SHA256).Hash.ToLowerInvariant())","RAW=$raw","CLOCK_MAP=$map","WORKER_RAW=$workerOut","WORKER_STDERR=$workerErr","IDENTITY=$identity")|Set-Content -LiteralPath $status -Encoding ascii
Get-Content -LiteralPath $status
if(!$ok -or $worker.ExitCode -ne 0){exit 2}
