param([string]$Capture='capture_sync_r1',[double]$DetectionMaxMs=157.103,[double]$ClockUms=1.0)
$ErrorActionPreference='Stop';$root=Join-Path $PSScriptRoot $Capture
$raw=Get-Content -LiteralPath (Join-Path $root 'emon_stdout.txt') -Raw
$marks=@(Import-Csv (Join-Path $root 'eviction_windows.csv'))
$qpf=[double][Diagnostics.Stopwatch]::Frequency
# The synchronized begin/end calls bracketed QPC and FILETIME together. Fit their
# paired samples to map civil EMON stamps onto QPC; U remains conservatively 1 ms.
$x=@();$y=@()
foreach($m in $marks){foreach($side in @('start','end')){$x+=[double]$m.PSObject.Properties["utc_filetime_$side"].Value;$y+=[double]$m.PSObject.Properties["qpc_$side"].Value}}
$xm=($x|Measure-Object -Average).Average;$ym=($y|Measure-Object -Average).Average;$num=0.0;$den=0.0
for($i=0;$i -lt $x.Count;$i++){$num+=($x[$i]-$xm)*($y[$i]-$ym);$den+=($x[$i]-$xm)*($x[$i]-$xm)}
$slope=$num/$den;$intercept=$ym-$slope*$xm;$res=@();for($i=0;$i -lt $x.Count;$i++){$res+=[math]::Abs($y[$i]-($intercept+$slope*$x[$i]))/$qpf*1000}
$maxResidual=($res|Measure-Object -Maximum).Maximum;$effectiveU=$ClockUms+$DetectionMaxMs;$uQpc=$ClockUms*$qpf/1000
$inv=[Globalization.CultureInfo]::InvariantCulture
function Num([string]$s){$s=$s.Trim();if($s -notmatch '^\d[\d,]*$'){return [double]::NaN};return [double]($s.Replace(',',''))}
$ev=@('CPU_CLK_UNHALTED.THREAD','L2_RQSTS.DEMAND_DATA_RD_MISS','L2_RQSTS.ALL_DEMAND_DATA_RD','OFFCORE_REQUESTS.L3_MISS_DEMAND_DATA_RD','OFFCORE_REQUESTS.DEMAND_DATA_RD')
$records=[Collections.Generic.List[object]]::new()
foreach($ch in [regex]::Split($raw,'(?m)^==========\s*$')){
 $m=[regex]::Match($ch,'(?m)^(\d{1,2}/\d{1,2}/\d{4}\s+\d{2}:\d{2}:\d{2}\.\d{3})\s*$');if(!$m.Success){continue}
 $dt=[datetime]::ParseExact($m.Groups[1].Value,'M/d/yyyy HH:mm:ss.fff',$inv,[Globalization.DateTimeStyles]::AssumeLocal);$ft=[double]$dt.ToFileTimeUtc();$end=$intercept+$slope*$ft;$e=@{}
 foreach($line in ($ch -split "`r?`n")){if(!$line.Contains("`t")){continue};$f=$line -split "`t";$name=$f[0].Trim();if($ev -notcontains $name){continue};$v=@();for($j=1;$j -lt $f.Count;$j++){$v+=Num $f[$j]};$e[$name]=$v}
 if(@($ev|Where-Object {!$e.ContainsKey($_)}).Count){continue};$dur=$e['CPU_CLK_UNHALTED.THREAD'][0]/(2112.003049e6);if(!$dur -or [double]::IsNaN($dur)){continue}
 $records.Add([pscustomobject]@{end=$end;start=$end-$dur*$qpf;dur=$dur;e=$e})
}
"EMON_GROUPS_PARSED=$($records.Count) BLOCKS=$($marks.Count) QPC_HZ=$qpf CLOCK_U_MS=$ClockUms DETECTION_MAX_MS=$DetectionMaxMs EFFECTIVE_ENDPOINT_U_MS=$effectiveU FIT_RESIDUAL_MAX_MS=$([math]::Round($maxResidual,6))"
"RULE=full windows only; no interpolation; per-window coverage uses U=$ClockUms ms; extreme block-edge gap uses U+max_detection_delay=$effectiveU ms as required by S-20 A."
$out=[Collections.Generic.List[object]]::new()
for($coreIndex=0;$coreIndex -lt 8;$coreIndex++){foreach($cond in @('cold_selected','hot_selected','cold_clflush_diag','hot_clflush_diag')){
 $mark=@($marks|Where-Object {[int]$_.Pcore -eq $coreIndex -and $_.condition -eq $cond})[0];if(!$mark){continue};$q0=[double]$mark.qpc_start;$q1=[double]$mark.qpc_end;$a=$q0+$uQpc;$b=$q1-$uQpc;$span=($q1-$q0)/$qpf
 $inside=@($records|Where-Object {$_.start -ge $a -and $_.end -le $b}|Sort-Object start)
 $lm=0.0;$ld=0.0;$l2m=0.0;$l2d=0.0;$covered=0.0;$maxWin=0.0;$maxGap=0.0;$last=$null
 foreach($r in $inside){$covered+=[math]::Max(0.0,$r.dur-2*$ClockUms/1000);$maxWin=[math]::Max($maxWin,$r.dur);if($null -eq $last){$g=($r.start-$q0)/$qpf+$effectiveU/1000}else{$g=($r.start-$last)/$qpf+2*$ClockUms/1000};$maxGap=[math]::Max($maxGap,$g);$last=$r.end
  $lp0=2*$coreIndex;$lp1=$lp0+1;foreach($lp in @($lp0,$lp1)){$l2m+=$r.e['L2_RQSTS.DEMAND_DATA_RD_MISS'][$lp+1];$l2d+=$r.e['L2_RQSTS.ALL_DEMAND_DATA_RD'][$lp+1];$lm+=$r.e['OFFCORE_REQUESTS.L3_MISS_DEMAND_DATA_RD'][$lp+1];$ld+=$r.e['OFFCORE_REQUESTS.DEMAND_DATA_RD'][$lp+1]}
 };if($inside.Count){$maxGap=[math]::Max($maxGap,($q1-$last)/$qpf+$effectiveU/1000)}else{$maxGap=$span+2*$effectiveU/1000}
 $coverage=if($span -gt 0){$covered/$span}else{0};$llc=if($ld -gt 0){$lm/$ld}else{[double]::NaN};$l2=if($l2d -gt 0){$l2m/$l2d}else{[double]::NaN};$temporal=($span -ge 2.0 -and $inside.Count -ge 20 -and $coverage -ge .95 -and $maxGap -le .020 -and $maxWin -le .100 -and $effectiveU -le 1.0)
 $out.Add([pscustomobject]@{Pcore=$coreIndex;LP_pair="$lp0,$lp1";condition=$cond;phase_s=[math]::Round($span,6);windows=$inside.Count;coverage_lower=[math]::Round($coverage,6);gap_upper_ms=[math]::Round($maxGap*1000,3);max_window_ms=[math]::Round($maxWin*1000,3);L2_miss=$l2m;L2_demand=$l2d;L2_fraction=$l2;LLC_miss=$lm;LLC_demand=$ld;LLC_fraction=$llc;temporal_qualification=if($temporal){'PASS'}else{'HOLD'}})
}}
$out|Export-Csv -NoTypeInformation -Encoding utf8 -LiteralPath (Join-Path $root 'eviction_cache_fractions.csv')
$out|Format-Table -AutoSize|Out-String -Width 240|Write-Output
