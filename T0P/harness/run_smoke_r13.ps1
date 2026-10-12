param([string]$AttemptName='smoke_N3_r13_emon_final2',[switch]$VerifyOnly)
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$root=Split-Path (Split-Path $here -Parent) -Parent
$obj=Join-Path $here 'logs_r13\obj_r13'
$exe=Join-Path $obj 'harness_r13.exe'
$plan=Join-Path $root 'T0P\collector\plan.json'
$sha=[string]((Get-Content -LiteralPath $plan -Raw | ConvertFrom-Json).sha256)
$emon='C:\Program Files (x86)\Intel\oneAPI\vtune\2026.4\bin64\emon.exe'
$sourceEdp=Join-Path $root 'T0P\telemetry\reader\emon_joint_qualification.edp'
$edp=Join-Path $obj 'emon_joint_qualification_r13.edp'
$capture=Join-Path $obj 'emon_smoke_N3_r13_edp.txt'
$captureErr=Join-Path $obj 'emon_smoke_N3_r13_edp.stderr.txt'
$harnessOut=Join-Path $obj 'smoke_N3_r13_emon.stdout.log'
$harnessErr=Join-Path $obj 'smoke_N3_r13_emon.stderr.log'
$cap=Join-Path $obj $AttemptName
foreach($path in @($exe,$plan,$emon,$sourceEdp)){if(!(Test-Path -LiteralPath $path)){throw "required file missing: $path"}}
if((Test-Path -LiteralPath $cap) -and -not $VerifyOnly){throw "new attempt directory required: $cap"}
$edpText=[IO.File]::ReadAllText($sourceEdp)
if(([regex]::Matches($edpText,'-l3600')).Count -ne 1){throw 'source EDP must contain one -l3600 loop limit'}
[IO.File]::WriteAllText($edp,$edpText.Replace('-l3600','-l7200'),[Text.UTF8Encoding]::new($false))
$torch='C:\Projects\OMEGA\.venv_cuda\Lib\site-packages\torch'
$env:PATH=(Join-Path $root 'T0P\ref_ggml\build_llama\bin')+';'+(Join-Path $torch 'lib')+';'+(Split-Path $emon -Parent)+';'+$env:PATH
$emonProc=$null;$harnessProc=$null;$earlyExit=$false
try{
  if(-not $VerifyOnly){
    $emonProc=Start-Process -FilePath $emon -ArgumentList @('-collect-edp',"edp_file=$edp") -WorkingDirectory $obj -RedirectStandardOutput $capture -RedirectStandardError $captureErr -PassThru -WindowStyle Hidden
    Start-Sleep -Seconds 3;$emonProc.Refresh();if($emonProc.HasExited){throw "EMON exited before harness start, exit=$($emonProc.ExitCode)"}
    $arguments=@('--smoke',$cap,$plan,$sha)|ForEach-Object{'"'+$_+'"'}
    $harnessProc=Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $root -RedirectStandardOutput $harnessOut -RedirectStandardError $harnessErr -PassThru -WindowStyle Hidden
    while($true){
      $harnessProc.Refresh();$emonProc.Refresh()
      if($harnessProc.HasExited){$harnessProc.WaitForExit();$harnessProc.Refresh();break}
      if($emonProc.HasExited){$earlyExit=$true}
      Start-Sleep -Seconds 1
    }
    $deadline=[DateTime]::UtcNow.AddSeconds(650)
    while($true){$emonProc.Refresh();if($emonProc.HasExited){break};if([DateTime]::UtcNow -gt $deadline){throw 'EMON did not finish within 650 seconds'};Start-Sleep -Seconds 2}
    $emonProc.WaitForExit();$emonProc.Refresh()
    if($earlyExit){throw 'EMON stopped before the harness completed'}
    if($emonProc.ExitCode -ne 0){throw "EMON exit=$($emonProc.ExitCode); see $captureErr"}
  }
  $harnessText=Get-Content -LiteralPath $harnessOut -Raw
  $harnessErrorText=Get-Content -LiteralPath $harnessErr -Raw
  if($harnessText -notmatch 'HARNESS_SMOKE_R13 PASS' -or $harnessErrorText -match 'HARNESS_ERROR|fatal error|Traceback'){throw "harness did not record successful completion; see $harnessOut and $harnessErr"}
  $rawRows=@(Get-Content -LiteralPath (Join-Path $cap 'harness_raw.jsonl')).Count
  $summaryRows=@(Get-Content -LiteralPath (Join-Path $cap 'harness_summary.jsonl')).Count
  if($rawRows -ne 864){throw "incomplete smoke rows=$rawRows expected=864"}
  if($summaryRows -ne 266){throw "incomplete summary rows=$summaryRows expected=266"}
  $preflight=Get-Content -LiteralPath (Join-Path $cap 'preflight_r13.json') -Raw|ConvertFrom-Json
  if($preflight.state -ne 'PASS' -or $preflight.workers.Count -ne 8){throw 'preflight JSON/map invalid'}
  $emonText=Get-Content -LiteralPath $capture -Raw
  if($emonText -notmatch 'INST_RETIRED\.ANY' -or $emonText -notmatch '(?m)^\s*\d+\.\d+s real\s+\d+\.\d+s user\s+\d+\.\d+s system\s*$'){throw 'EMON output lacks INST_RETIRED.ANY or completion footer'}
  "SMOKE_R13_CAPTURE PASS raw_rows=$rawRows summary_rows=$summaryRows emon_bytes=$((Get-Item -LiteralPath $capture).Length) verify_only=$VerifyOnly label=SMOKE numeric_metrics_used=false"
}finally{
  if($harnessProc){$harnessProc.Dispose()}
  if($emonProc){$emonProc.Dispose()}
}
