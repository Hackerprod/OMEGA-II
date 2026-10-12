param([string]$RawPath=(Join-Path (Split-Path -Parent $PSScriptRoot) 'logs\D2_raw_r3_repeat.csv'))
$ErrorActionPreference='Stop'
$d=@(Import-Csv -LiteralPath $RawPath)
if($d.Count -ne 248){throw "Expected 31 x 8 rows; found $($d.Count)"}
$summary=[System.Collections.Generic.List[object]]::new()
foreach($lp in 0,2,4,6,8,10,12,14){
  $rows=@($d|Where-Object {[int]$_.lp -eq $lp})
  $bad=@($rows|Where-Object {[double]$_.ref_ratio -lt .98 -or [double]$_.ref_ratio -gt 1.02})
  $ratios=@($rows|ForEach-Object {[double]$_.ref_ratio})
  $summary.Add([pscustomobject]@{lp=$lp;core=($lp/2);intervals=$rows.Count;pass=$rows.Count-$bad.Count;fail=$bad.Count;min_ratio=($ratios|Measure-Object -Minimum).Minimum;max_ratio=($ratios|Measure-Object -Maximum).Maximum;failed_indices=($bad|ForEach-Object {$_.interval}) -join ',';failure_reason=if($bad.Count){'PARTIAL_DELTA_RATIO_OUTSIDE_0.98_1.02'}else{'PASS'}})
}
$out=Join-Path (Split-Path $RawPath) 'D2_summary_r3_repeat_v2.csv'
$summary|Export-Csv -LiteralPath $out -NoTypeInformation -Encoding utf8
$intervals=@($d|Group-Object interval|ForEach-Object {$g=$_.Group;$r=$g|Select-Object -First 1;$ratioMin=($g|ForEach-Object {[double]$_.ref_ratio}|Measure-Object -Minimum).Minimum;$failed=@($g|Where-Object {[double]$_.ref_ratio -lt .98 -or [double]$_.ref_ratio -gt 1.02}).Count;[pscustomobject]@{interval=[int]$_.Name;dt_qpc_s=[double]$r.dt_s;api_timestamp_delta=([double]$r.api_ts_end-[double]$r.api_ts_start);ref_ratio_min_all_cores=$ratioMin;failed_cores=$failed;status=if($failed){'INVALID_PARTIAL_DELTA_REF_TSC_OUTSIDE_0.98_1.02'}else{'PASS'}}}|Sort-Object interval)
$intervalPath=Join-Path (Split-Path $RawPath) 'D2_intervals_r3_repeat_v2.csv'
$intervals|Export-Csv -LiteralPath $intervalPath -NoTypeInformation -Encoding utf8
Get-Content -LiteralPath $out
Import-Csv -LiteralPath $intervalPath|Where-Object failed_cores -gt 0|Format-Table -AutoSize
