param([string]$Capture='capture_blocks_r2')
$ErrorActionPreference='Stop'
$root=Join-Path $PSScriptRoot $Capture
$raw=Get-Content -LiteralPath (Join-Path $root 'emon_stdout.txt') -Raw
$map=@(Import-Csv (Join-Path $root 'clock_map.csv'))
$marks=@(Import-Csv (Join-Path $root 'eviction_windows.csv'))
$qpf=[double][Diagnostics.Stopwatch]::Frequency
$x=@();$y=@();$br=@()
foreach($r in $map){$x+=[double]$r.filetime_100ns;$y+=([double]$r.qpc_pre+[double]$r.qpc_post)/2;$br+=1000.0*([double]$r.qpc_post-[double]$r.qpc_pre)/$qpf}
$xm=($x|Measure-Object -Average).Average;$ym=($y|Measure-Object -Average).Average;$num=0.0;$den=0.0
for($i=0;$i -lt $x.Count;$i++){$num+=($x[$i]-$xm)*($y[$i]-$ym);$den+=($x[$i]-$xm)*($x[$i]-$xm)}
$slope=$num/$den;$intercept=$ym-$slope*$xm;$res=@();for($i=0;$i -lt $x.Count;$i++){$res+=[math]::Abs($y[$i]-($intercept+$slope*$x[$i]))/$qpf*1000}
$maxResidual=($res|Measure-Object -Maximum).Maximum;$maxBracket=($br|Measure-Object -Maximum).Maximum
# U includes clock-map fit residual, half-bracket, and 0.5 ms civil timestamp quantization.
$uMs=$maxResidual+$maxBracket/2+0.5;$uQpc=$uMs*$qpf/1000
$inv=[Globalization.CultureInfo]::InvariantCulture
function Num([string]$s){$s=$s.Trim();if($s -notmatch '^\d[\d,]*$'){return [double]::NaN};return [double]($s.Replace(',',''))}
$ev=@('CPU_CLK_UNHALTED.THREAD','CPU_CLK_UNHALTED.REF_TSC','L2_RQSTS.DEMAND_DATA_RD_MISS','L2_RQSTS.ALL_DEMAND_DATA_RD','OFFCORE_REQUESTS.L3_MISS_DEMAND_DATA_RD','OFFCORE_REQUESTS.DEMAND_DATA_RD')
$records=[Collections.Generic.List[object]]::new()
foreach($ch in [regex]::Split($raw,'(?m)^==========\s*$')){
 $m=[regex]::Match($ch,'(?m)^(\d{1,2}/\d{1,2}/\d{4}\s+\d{2}:\d{2}:\d{2}\.\d{3})\s*$');if(!$m.Success){continue}
 $dt=[datetime]::ParseExact($m.Groups[1].Value,'M/d/yyyy HH:mm:ss.fff',$inv,[Globalization.DateTimeStyles]::AssumeLocal);$ft=[double]$dt.ToFileTimeUtc();$end=$intercept+$slope*$ft;$e=@{}
 foreach($line in ($ch -split "`r?`n")){if(!$line.Contains("`t")){continue};$f=$line -split "`t";$name=$f[0].Trim();if($ev -notcontains $name){continue};$v=@();for($j=1;$j -lt $f.Count;$j++){$v+=Num $f[$j]};$e[$name]=$v}
 if(@($ev|Where-Object {!$e.ContainsKey($_)}).Count){continue};$dur=$e['CPU_CLK_UNHALTED.THREAD'][0]/2112.003049e6;if(!$dur -or [double]::IsNaN($dur)){continue}
 $records.Add([pscustomobject]@{end=$end;start=$end-$dur*$qpf;dur_ms=$dur*1000;e=$e})
}
$ratios=@{}
foreach($line in (Get-Content (Join-Path $root 'harness.stdout.txt'))){if($line -match '^P(\d+) LP\d+ N=31 .*?ratio=([0-9.]+).*?CLFLUSH_diag.*?ratio=([0-9.]+)'){$ratios[[int]$Matches[1]]=@{selected=[double]$Matches[2];clflush=[double]$Matches[3]}}}
$out=[Collections.Generic.List[object]]::new()
foreach($mark in $marks){
 $p=[int]$mark.Pcore;$cond=$mark.condition;$padA=[double]$mark.qpc_pad_start;$a=[double]$mark.qpc_start;$b=[double]$mark.qpc_end;$padB=[double]$mark.qpc_pad_end
 $span=($b-$a)/$qpf;$mi=1000.0*($a-$padA)/$qpf;$mf=1000.0*($padB-$b)/$qpf
 $overlap=@($records|Where-Object {$_.start -lt $b -and $_.end -gt $a}|Sort-Object start)
 $inside=[Collections.Generic.List[object]]::new();$excluded=0
 foreach($r in $overlap){if($r.start -ge ($padA+$uQpc) -and $r.end -le ($padB-$uQpc)){$inside.Add($r)}else{$excluded++}}
 $maxWin=0.0;$l2m=0.0;$l2d=0.0;$lm=0.0;$ld=0.0
 $lp0=2*$p;$lp1=$lp0+1;$segments=[Collections.Generic.List[object]]::new()
 foreach($r in $inside){$cs=[math]::Max($a,$r.start);$ce=[math]::Min($b,$r.end);$segments.Add([pscustomobject]@{s=$cs;e=$ce});$maxWin=[math]::Max($maxWin,$r.dur_ms)
  foreach($lp in @($lp0,$lp1)){$l2m+=$r.e['L2_RQSTS.DEMAND_DATA_RD_MISS'][$lp+1];$l2d+=$r.e['L2_RQSTS.ALL_DEMAND_DATA_RD'][$lp+1];$lm+=$r.e['OFFCORE_REQUESTS.L3_MISS_DEMAND_DATA_RD'][$lp+1];$ld+=$r.e['OFFCORE_REQUESTS.DEMAND_DATA_RD'][$lp+1]}
 }
 $united=[Collections.Generic.List[object]]::new();foreach($seg in $segments){if(!$united.Count -or $seg.s -gt $united[$united.Count-1].e){$united.Add([pscustomobject]@{s=$seg.s;e=$seg.e})}else{$united[$united.Count-1].e=[math]::Max($united[$united.Count-1].e,$seg.e)}}
 $covered=0.0;$maxGap=0.0;$cursor=$a;$first=$true;foreach($seg in $united){$gapMs=($seg.s-$cursor)/$qpf*1000+$(if($first){$uMs}else{2*$uMs});$maxGap=[math]::Max($maxGap,$gapMs);$covered+=[math]::Max(0.0,(($seg.e-$seg.s)/$qpf*1000)-2*$uMs);$cursor=$seg.e;$first=$false};$maxGap=[math]::Max($maxGap,(($b-$cursor)/$qpf*1000)+$uMs);$coverage=if($span -gt 0){[math]::Min(1.0,$covered/($span/$qpf*1000))}else{0.0}
 $llc=if($ld -gt 0){$lm/$ld}else{[double]::NaN};$l2=if($l2d -gt 0){$l2m/$l2d}else{[double]::NaN}
 $temporal=($mi -ge 200 -and $mf -ge 200 -and $inside.Count -ge 20 -and $coverage -ge .95 -and $maxGap -le 20 -and $maxWin -le 100 -and $uMs -le 1.0 -and $span -ge 2)
 $ratio=if($cond -eq 'arch_evict8'){'N/A'}elseif($cond -match 'clflush'){$ratios[$p].clflush}else{$ratios[$p].selected}
 $physical=if($cond -match '^cold'){$llc -ge .90}else{if($cond -match '^hot'){$llc -le .10}else{$true}}
 $ratioPass=($cond -eq 'arch_evict8') -or ($ratio -ge 1.50)
 $status=if($temporal -and $physical -and $ratioPass){'EVICTION_QUALIFIED'}else{'HOLD_EVICTION_NOT_QUALIFIED'}
 $out.Add([pscustomobject]@{P=$p;LP_pair="$lp0,$lp1";condition=$cond;margin_ini_ms=[math]::Round($mi,3);margin_fin_ms=[math]::Round($mf,3);windows=$inside.Count;excluded_cross_pad=$excluded;coverage_pct=[math]::Round(100*$coverage,3);gap_upper_ms=[math]::Round($maxGap,3);max_window_ms=[math]::Round($maxWin,3);ratio=if($ratio -is [string]){'N/A'}else{[math]::Round($ratio,6)};LLC_fraction=[math]::Round($llc,6);L2_fraction=[math]::Round($l2,6);temporal=if($temporal){'PASS'}else{'HOLD'};physical=if($physical){'PASS'}else{'HOLD'};status=$status})
}
$dest=Join-Path $root 'padded_block_analysis.csv';$out|Export-Csv -NoTypeInformation -Encoding utf8 -LiteralPath $dest
"EMON_GROUPS_PARSED=$($records.Count) CLOCK_MAP_ROWS=$($map.Count) QPC_HZ=$qpf U_ms=$([math]::Round($uMs,6)) max_residual_ms=$([math]::Round($maxResidual,6)) max_bracket_ms=$([math]::Round($maxBracket,6))"
"PAD_MIN_MS=200 WINDOWS=complete windows overlapping measured region and contained in same-condition pad; crossing windows excluded/reported; no interpolation."
$out|Format-Table -AutoSize|Out-String -Width 260|Write-Output
