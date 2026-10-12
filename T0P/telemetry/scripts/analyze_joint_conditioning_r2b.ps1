param(
  [double]$ConfiguredTscMHz=2112.0,
  [string]$RawPath,
  [string]$MapPath,
  [string]$TccPath,
  [string]$OutPrefix='conditioning_analysis'
)
$ErrorActionPreference='Stop'
$telemetry=Split-Path -Parent $PSScriptRoot
if(!$RawPath){$RawPath=Join-Path $telemetry 'logs\conditioning_joint_raw.txt'}
if(!$MapPath){$MapPath=Join-Path $telemetry 'logs\conditioning_qpc_utc_map.csv'}
if(!$TccPath){$TccPath=Join-Path $telemetry 'logs\tcc_preflight_raw.txt'}
$outCoverage=Join-Path (Split-Path $RawPath) ($OutPrefix+'_coverage.csv')
$outConditioning=Join-Path (Split-Path $RawPath) ($OutPrefix+'_stability.csv')
$outStatus=Join-Path (Split-Path $RawPath) ($OutPrefix+'_temporal_status.txt')
foreach($p in @($outCoverage,$outConditioning,$outStatus)){if(Test-Path -LiteralPath $p){throw "Refusing to overwrite $p"}}
$inv=[Globalization.CultureInfo]::InvariantCulture
$qpcFrequency=[double][System.Diagnostics.Stopwatch]::Frequency
function Test-Finite([double]$x){return (-not [double]::IsNaN($x) -and -not [double]::IsInfinity($x))}
function Get-Num([string]$s){
  if($null -eq $s){return [double]::NaN}
  $t=$s.Trim()
  if(!$t -or $t -eq 'N/A' -or $t -eq '-') {return [double]::NaN}
  $v=0.0
  if([double]::TryParse(($t -replace ',',''),[Globalization.NumberStyles]::Float,$inv,[ref]$v)){return $v}
  return [double]::NaN
}
function Fit-Line($xs,$ys){
  $mx=($xs|Measure-Object -Average).Average;$my=($ys|Measure-Object -Average).Average;$num=0.0;$den=0.0
  for($i=0;$i -lt $xs.Count;$i++){$num+=($xs[$i]-$mx)*($ys[$i]-$my);$den+=($xs[$i]-$mx)*($xs[$i]-$mx)}
  if($den -le 0){throw 'clock map has no temporal span'}
  $s=$num/$den;return [pscustomobject]@{Slope=$s;Intercept=($my-$s*$mx)}
}
$clockRows=@(Import-Csv -LiteralPath $MapPath | Where-Object {$_.qpc_pre -match '^\d+$' -and $_.qpc_post -match '^\d+$' -and $_.filetime_100ns -match '^\d+$'})
if($clockRows.Count -lt 20){throw "Clock map has too few rows: $($clockRows.Count)"}
$mapX=@();$mapY=@();$brackets=@()
foreach($r in $clockRows){$mapX+=[double]$r.filetime_100ns;$mapY+=([double]$r.qpc_pre+[double]$r.qpc_post)/2;$brackets+=1000.0*([double]$r.qpc_post-[double]$r.qpc_pre)/$qpcFrequency}
$fit=Fit-Line $mapX $mapY
$mapResidual=@();for($i=0;$i -lt $mapX.Count;$i++){$mapResidual+=[Math]::Abs($mapY[$i]-($fit.Intercept+$fit.Slope*$mapX[$i]))/$qpcFrequency*1000}
$mapMaxResidual=($mapResidual|Measure-Object -Maximum).Maximum
$mapMaxBracket=($brackets|Measure-Object -Maximum).Maximum
$mapUncertaintyMs=$mapMaxResidual+$mapMaxBracket/2+0.5
function Convert-LocalStampToQpc([string]$stamp){
  $dt=[DateTime]::ParseExact($stamp,'M/d/yyyy HH:mm:ss.fff',$inv,[Globalization.DateTimeStyles]::AssumeLocal)
  $ft=[double]$dt.ToFileTimeUtc()
  return $fit.Intercept+$fit.Slope*$ft
}
$clockStart=$clockRows|Where-Object sample -eq 'BEGIN'|Select-Object -First 1
$clockEnd=$clockRows|Where-Object sample -eq 'END'|Select-Object -Last 1
if(!$clockStart -or !$clockEnd){throw 'Clock map lacks explicit BEGIN/END brackets'}
$blockStart=([double]$clockStart.qpc_pre+[double]$clockStart.qpc_post)/2
$blockEnd=([double]$clockEnd.qpc_pre+[double]$clockEnd.qpc_post)/2
$blockSeconds=($blockEnd-$blockStart)/$qpcFrequency
$fRows=@((Get-Content (Join-Path $telemetry 'logs\D1_raw.csv') | Select-Object -Skip 1 | ConvertFrom-Csv) | Where-Object {$_.core -match '^\d$'})
$tscMHz=(@($fRows|ForEach-Object { [double]$_.tsc_delta/[double]$_.qpc_ticks*$qpcFrequency/1e6 })|Measure-Object -Average).Average
if(!$tscMHz){$tscMHz=$ConfiguredTscMHz}
$tscHz=$tscMHz*1e6
$raw=Get-Content -LiteralPath $RawPath -Raw
$chunks=[regex]::Split($raw,'(?m)^==========\s*$')
$records=[System.Collections.Generic.List[object]]::new()
$eventsNeeded=@(
 'CPU_CLK_UNHALTED.THREAD','CPU_CLK_UNHALTED.REF_TSC','INST_RETIRED.ANY',
 'L2_RQSTS.DEMAND_DATA_RD_MISS','L2_RQSTS.ALL_DEMAND_DATA_RD',
 'OFFCORE_REQUESTS.L3_MISS_DEMAND_DATA_RD','OFFCORE_REQUESTS.DEMAND_DATA_RD',
 'MSR_EVENT:msr=0x606:type=STATIC:scope=PACKAGE',
 'MSR_EVENT:msr=0x611:type=FREERUN:scope=PACKAGE',
 'MSR_EVENT:msr=0x19C:type=STATIC:scope=THREAD',
 'MSR_EVENT:msr=0x1B1:type=STATIC:scope=PACKAGE')
foreach($chunk in $chunks){
  $date=([regex]::Match($chunk,'(?m)^\d{1,2}/\d{1,2}/\d{4}\s+\d{2}:\d{2}:\d{2}\.\d{3}\s*$')).Value.Trim()
  if(!$date){continue}
  $e=@{}
  foreach($line in ($chunk -split "`r?`n")){
    if(!$line.Contains("`t")){continue};$f=$line -split "`t";if($f.Count -lt 3){continue};$name=$f[0].Trim()
    if($eventsNeeded -notcontains $name){continue}
    $vals=@();for($j=1;$j -lt $f.Count;$j++){$vals+=Get-Num $f[$j]};$e[$name]=$vals
  }
  $missing=@($eventsNeeded|Where-Object {!$e.ContainsKey($_)})
  $dur=if($e.ContainsKey('CPU_CLK_UNHALTED.THREAD')){$e['CPU_CLK_UNHALTED.THREAD'][0]}else{[double]::NaN}
  $endQ=Convert-LocalStampToQpc $date
  $durationS=$dur/$tscHz
  if($missing.Count -eq 0 -and (Test-Finite $durationS) -and $durationS -gt 0){
    $rec=[ordered]@{stamp=$date;end_qpc=$endQ;start_qpc=($endQ-$durationS*$qpcFrequency);duration_s=$durationS;events=$e}
    $records.Add([pscustomobject]$rec)
  }
}
$inside=@($records|Where-Object {$_.end_qpc -gt $blockStart -and $_.start_qpc -lt $blockEnd})
$unitRows=@(Get-Content -LiteralPath $TccPath | Where-Object {$_ -match '^MSR_EVENT:msr=0x1A2:type=STATIC:scope=THREAD\t'})
$tccValues=@();foreach($line in $unitRows){$f=$line -split "`t";for($i=2;$i -lt $f.Count;$i++){$tccValues+=Get-Num $f[$i]}}
$tccRaw=@($tccValues|Where-Object {Test-Finite $_})
$tccUniform=($tccRaw.Count -ge 24 -and (@($tccRaw|Select-Object -Unique).Count -eq 1))
$tccC=if($tccUniform){[int](([uint64]$tccRaw[0] -shr 16) -band 0xff)}else{0}
$unit606=@();foreach($r in $inside){$v=$r.events['MSR_EVENT:msr=0x606:type=STATIC:scope=PACKAGE'][1];if(Test-Finite $v){$unit606+=$v}}
$unit606Unique=@($unit606|Select-Object -Unique);$energyExponent=if($unit606Unique.Count -eq 1){[int](([uint64]$unit606Unique[0] -shr 8) -band 0x1f)}else{-1}
$energyUnitJ=if($energyExponent -ge 0){[Math]::Pow(2,-$energyExponent)}else{[double]::NaN}
function Measure-Coverage($items){
  $s=@($items|Sort-Object start_qpc);$cursor=$blockStart;$covered=0.0;$gap=0.0;$maxDur=0.0
  foreach($r in $s){$a=[Math]::Max($blockStart,[double]$r.start_qpc);$b=[Math]::Min($blockEnd,[double]$r.end_qpc);$maxDur=[Math]::Max($maxDur,[double]$r.duration_s);if($b -le $a){continue};if($a -gt $cursor){$gap=[Math]::Max($gap,$a-$cursor)};$covered+=[Math]::Max(0.0,$b-[Math]::Max($a,$cursor));$cursor=[Math]::Max($cursor,$b)}
  $gap=[Math]::Max($gap,$blockEnd-$cursor);$lower=[Math]::Max(0.0,$covered-2*$mapUncertaintyMs/1000*$s.Count)/($blockSeconds+2*$mapUncertaintyMs/1000);$gapUpper=$gap+2*$mapUncertaintyMs/1000
  return [pscustomobject]@{N=$s.Count;CoverageLower=$lower;MaxGapUpper=$gapUpper;MaxWindow=$maxDur}
}
$coverageRows=[System.Collections.Generic.List[object]]::new();$allSamples=[System.Collections.Generic.List[object]]::new()
for($c=0;$c -lt 8;$c++){
  $lp=2*$c;$lp2=$lp+1
  $channelNames=@('THREAD_REF_TSC','L2_DEMAND_DATA_READ','LLC_DEMAND_DATA_READ','IA32_THERM_STATUS_19C','MSR_PKG_ENERGY_STATUS_611','IA32_PACKAGE_THERM_STATUS_1B1')
  foreach($ch in $channelNames){$valid=[System.Collections.Generic.List[object]]::new();foreach($r in $inside){$e=$r.events;$ok=$true
      if($ch -eq 'THREAD_REF_TSC'){$vt=$e['CPU_CLK_UNHALTED.THREAD'][$lp+1];$vr=$e['CPU_CLK_UNHALTED.REF_TSC'][$lp+1];$ok=(Test-Finite $vt) -and (Test-Finite $vr)}
      elseif($ch -eq 'L2_DEMAND_DATA_READ'){$a=$e['L2_RQSTS.DEMAND_DATA_RD_MISS'][$lp+1];$b=$e['L2_RQSTS.ALL_DEMAND_DATA_RD'][$lp+1];$aa=$e['L2_RQSTS.DEMAND_DATA_RD_MISS'][$lp2+1];$bb=$e['L2_RQSTS.ALL_DEMAND_DATA_RD'][$lp2+1];$ok=(Test-Finite $a) -and (Test-Finite $b) -and (Test-Finite $aa) -and (Test-Finite $bb)}
      elseif($ch -eq 'LLC_DEMAND_DATA_READ'){$a=$e['OFFCORE_REQUESTS.L3_MISS_DEMAND_DATA_RD'][$lp+1];$b=$e['OFFCORE_REQUESTS.DEMAND_DATA_RD'][$lp+1];$aa=$e['OFFCORE_REQUESTS.L3_MISS_DEMAND_DATA_RD'][$lp2+1];$bb=$e['OFFCORE_REQUESTS.DEMAND_DATA_RD'][$lp2+1];$ok=(Test-Finite $a) -and (Test-Finite $b) -and (Test-Finite $aa) -and (Test-Finite $bb)}
      elseif($ch -eq 'IA32_THERM_STATUS_19C'){$raw19=$e['MSR_EVENT:msr=0x19C:type=STATIC:scope=THREAD'][$lp+1];$ok=(Test-Finite $raw19) -and (([uint64]$raw19 -band 0x80000000) -ne 0)}
      elseif($ch -eq 'MSR_PKG_ENERGY_STATUS_611'){$v=$e['MSR_EVENT:msr=0x611:type=FREERUN:scope=PACKAGE'][1];$ok=Test-Finite $v}
      elseif($ch -eq 'IA32_PACKAGE_THERM_STATUS_1B1'){$v=$e['MSR_EVENT:msr=0x1B1:type=STATIC:scope=PACKAGE'][1];$ok=Test-Finite $v}
      if($ok){$valid.Add($r)}
    }
    $m=Measure-Coverage @($valid);$ok=$m.N -ge 20 -and $m.CoverageLower -ge .95 -and $m.MaxGapUpper -le .020000001 -and $m.MaxWindow -le .100000001 -and $mapUncertaintyMs -le 1.0
    $state=if($mapUncertaintyMs -gt 1.0){'HOLD_INSTRUMENTATION'}elseif($ok){'PASS'}else{'FAIL'}
    $reason=if($mapUncertaintyMs -gt 1.0){'time alignment uncertainty >1ms'}elseif($m.N -lt 20){'fewer than 20 windows'}elseif($m.CoverageLower -lt .95){'coverage lower bound <95%'}elseif($m.MaxGapUpper -gt .02){'max gap upper bound >20ms'}elseif($m.MaxWindow -gt .1){'real window >100ms'}else{'all S-20 temporal limits pass'}
    $coverageRows.Add([pscustomobject]@{core=$c;channel=$ch;state=$state;reason=$reason;windows=$m.N;coverage_lower_pct=100*$m.CoverageLower;max_gap_upper_ms=1000*$m.MaxGapUpper;max_window_ms=1000*$m.MaxWindow;alignment_uncertainty_ms=$mapUncertaintyMs})
  }
  foreach($bin in 0..2){$binStart=$blockStart+(90+30*$bin)*$qpcFrequency;$binEnd=$binStart+30*$qpcFrequency;$reps=@($inside|Where-Object {$_.end_qpc -gt $binStart -and $_.start_qpc -lt $binEnd});$sumT=0.0;$sumR=0.0
    foreach($r in $reps){$sumT+=$r.events['CPU_CLK_UNHALTED.THREAD'][$lp+1];$sumR+=$r.events['CPU_CLK_UNHALTED.REF_TSC'][$lp+1]}
    $factive=if($sumR -gt 0){$tscHz*$sumT/$sumR/1e6}else{[double]::NaN}
    if($c -eq 0){$sumE=0.0;foreach($r in $reps){$sumE+=$r.events['MSR_EVENT:msr=0x611:type=FREERUN:scope=PACKAGE'][1]};$pPkg=if($energyExponent -ge 0){$sumE*$energyUnitJ/30.0}else{[double]::NaN};}
    $raw19=if($reps.Count){[uint64]$reps[-1].events['MSR_EVENT:msr=0x19C:type=STATIC:scope=THREAD'][$lp+1]}else{0};$margin=($raw19 -shr 16) -band 0x7f;$flagsCurrent=$raw19 -band 0x5555;$flagsHistorical=$raw19 -band 0xAAAA;$tempEst=if($tccC){$tccC-$margin}else{[double]::NaN}
    $allSamples.Add([pscustomobject]@{core=$c;lp=$lp;bin=$bin+1;windows=$reps.Count;f_active_MHz=$factive;TCC_margin_C=$margin;estimated_sensor_C=$tempEst;THERM_current_bits=('0x{0:X4}' -f $flagsCurrent);THERM_historical_bits=('0x{0:X4}' -f $flagsHistorical);TCC_target_C=$tccC;DUMMY=$true})
  }
}
$coverageRows|Export-Csv -LiteralPath $outCoverage -NoTypeInformation -Encoding utf8
$allSamples|Export-Csv -LiteralPath $outConditioning -NoTypeInformation -Encoding utf8
$freqChecks=for($c=0;$c -lt 8;$c++){$r=@($allSamples|Where-Object core -eq $c);$f=@($r|%{[double]$_.f_active_MHz});$mean=($f|Measure-Object -Average).Average;[pscustomobject]@{core=$c;f_min_MHz=($f|Measure-Object -Minimum).Minimum;f_max_MHz=($f|Measure-Object -Maximum).Maximum;f_mean_MHz=$mean;range_fraction=if($mean){(($f|Measure-Object -Maximum).Maximum-($f|Measure-Object -Minimum).Minimum)/$mean}else{[double]::NaN};PASS=($mean -gt 0 -and ((($f|Measure-Object -Maximum).Maximum-($f|Measure-Object -Minimum).Minimum)/$mean) -le .03)}}
$powerBins=@(); # overwritten below from raw package-energy deltas for the exact final three bins
for($bin=0;$bin -lt 3;$bin++){$bs=$blockStart+(90+30*$bin)*$qpcFrequency;$be=$bs+30*$qpcFrequency;$rr=@($inside|Where-Object {$_.end_qpc -gt $bs -and $_.start_qpc -lt $be});$se=0.0;foreach($r in $rr){$se+=$r.events['MSR_EVENT:msr=0x611:type=FREERUN:scope=PACKAGE'][1]};$powerBins+=($se*$energyUnitJ/30.0)}
$pMean=($powerBins|Measure-Object -Average).Average;$pRange=if($pMean){(($powerBins|Measure-Object -Maximum).Maximum-($powerBins|Measure-Object -Minimum).Minimum)/$pMean}else{[double]::NaN}
$covStates=@($coverageRows|Group-Object channel|ForEach-Object {"$($_.Name)=$((@($_.Group|Select-Object -ExpandProperty state -Unique)) -join ',')"})
@("DUMMY=TRUE; no throughput used","EMON groups parsed=$($records.Count); windows overlapping conditioning block=$($inside.Count)","conditioning QPC seconds=$blockSeconds","f_TSC calibrated from D1=$tscMHz MHz; configured=$ConfiguredTscMHz MHz","QPC-UTC map samples=$($clockRows.Count); max linear residual=$mapMaxResidual ms; max bracket=$mapMaxBracket ms; civil millisecond rounding=0.5 ms; conservative endpoint uncertainty=$mapUncertaintyMs ms","energy 0x606 values=$($unit606Unique -join ','); energy_unit_exponent=$energyExponent; u_E_J=$energyUnitJ; 0x611 label=INTERVAL_DELTA","TCC 0x1A2 uniform across P/E LP=$tccUniform; TCC_target_C=$tccC","STATES=$($covStates -join '; ')","POWER_BIN_MEAN_W=$pMean; POWER_BIN_RANGE_FRACTION=$pRange; POWER_PASS=$($pMean -gt 0 -and $pRange -le .05)","Frequency checks:") + @($freqChecks|ForEach-Object {"P$($_.core): n=3 fmin=$($_.f_min_MHz) fmax=$($_.f_max_MHz) mean=$($_.f_mean_MHz) range=$($_.range_fraction) PASS=$($_.PASS)"}) | Set-Content -LiteralPath $outStatus -Encoding utf8
Get-Content -LiteralPath $outStatus


