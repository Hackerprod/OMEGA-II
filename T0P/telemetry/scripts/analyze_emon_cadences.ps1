param([string]$TelemetryRoot = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference='Stop'
$culture=[Globalization.CultureInfo]::InvariantCulture
$freqMHz=2112.0
$all=[System.Collections.Generic.List[object]]::new()
$detail=[System.Collections.Generic.List[object]]::new()
foreach($ms in 5,10,20,50,100){
  $path=Join-Path (Join-Path $TelemetryRoot 'logs') ("emon_cadence_{0}ms.txt" -f $ms)
  $lines=[IO.File]::ReadAllLines($path)
  $windows=[System.Collections.Generic.List[object]]::new()
  $backward=0; $lastTime=$null; $discarded=0
  for($i=0;$i -lt $lines.Length;$i++){
    if($lines[$i] -match '^\d{1,2}/\d{1,2}/\d{4} \d{2}:\d{2}:\d{2}\.\d{3}$' -and $i+2 -lt $lines.Length -and $lines[$i+1].StartsWith("CPU_CLK_UNHALTED.THREAD`t") -and $lines[$i+2].StartsWith("CPU_CLK_UNHALTED.REF_TSC`t")){
      $time=[datetime]::ParseExact($lines[$i],'M/d/yyyy HH:mm:ss.fff',$culture)
      if($null -ne $lastTime -and $time -lt $lastTime){$backward++}
      $lastTime=$time
      $thread=@(($lines[$i+1].Split("`t"))[1..25] | ForEach-Object {[double]($_.Replace(',',''))})
      $ref=@(($lines[$i+2].Split("`t"))[1..25] | ForEach-Object {[double]($_.Replace(',',''))})
      $durationMs=1000.0*$thread[0]/($freqMHz*1000000.0)
      $start=$time.AddMilliseconds(-$durationMs)
      $windows.Add([pscustomobject]@{Time=$time;Start=$start;End=$time;DurationMs=$durationMs;ThreadP0=$thread[1];RefP0=$ref[1];FActiveP0MHz=if($ref[1] -gt 0){$freqMHz*$thread[1]/$ref[1]}else{$null}})
    }
  }
  if(($lines -join "`n") -match 'Total\s+(\d+) data groups discarded due to time discrepancy'){$discarded=[int]$Matches[1]}
  $diffs=[System.Collections.Generic.List[double]]::new()
  for($i=1;$i -lt $windows.Count;$i++){$diffs.Add(($windows[$i].Time-$windows[$i-1].Time).TotalMilliseconds)}
  $sorted=@($windows | Sort-Object Start,End)
  $coverageMs=0.0; $maxGapMs=0.0; $cursor=$null
  if($sorted.Count){$spanMs=($sorted[-1].End-$sorted[0].Start).TotalMilliseconds;foreach($w in $sorted){if($null -eq $cursor){$cursor=$w.End;$coverageMs+=$w.DurationMs;continue};$gap=($w.Start-$cursor).TotalMilliseconds;if($gap -gt 0){$maxGapMs=[Math]::Max($maxGapMs,$gap)};$overlapStart=if($w.Start -gt $cursor){$w.Start}else{$cursor};if($w.End -gt $overlapStart){$coverageMs+=($w.End-$overlapStart).TotalMilliseconds};if($w.End -gt $cursor){$cursor=$w.End}}}else{$spanMs=0}
  $orderedDiffs=@($diffs|Sort-Object)
  $p50=if($orderedDiffs.Count){$orderedDiffs[[int][Math]::Floor(($orderedDiffs.Count-1)*0.5)]}else{$null}
  $p95=if($orderedDiffs.Count){$orderedDiffs[[int][Math]::Floor(($orderedDiffs.Count-1)*0.95)]}else{$null}
  $mean=if($diffs.Count){($diffs|Measure-Object -Average).Average}else{$null}
  $f=@($windows|Where-Object {$_.RefP0 -gt 0}|ForEach-Object {$_.FActiveP0MHz}|Sort-Object)
  $fmean=if($f.Count){($f|Measure-Object -Average).Average}else{$null}
  $all.Add([pscustomobject]@{RequestedCadenceMs=$ms;ExpectedGroups=[int](3000/$ms);ProducedGroups=$windows.Count;DiscardedGroups=$discarded;SumProducedAndDiscarded=$windows.Count+$discarded;CountCoveragePct=[Math]::Round(100.0*$windows.Count/(3000/$ms),2);TimestampSpanMs=[Math]::Round($spanMs,2);ValidIntervalUnionMs=[Math]::Round($coverageMs,2);TemporalCoveragePct=if($spanMs -gt 0){[Math]::Round(100.0*$coverageMs/$spanMs,2)}else{$null};MaxHoleMs=[Math]::Round($maxGapMs,2);GroupIntervalMeanMs=if($null -ne $mean){[Math]::Round($mean,2)}else{$null};GroupIntervalP50Ms=if($null -ne $p50){[Math]::Round($p50,2)}else{$null};GroupIntervalP95Ms=if($null -ne $p95){[Math]::Round($p95,2)}else{$null};GroupIntervalMaxMs=if($diffs.Count){[Math]::Round(($diffs|Measure-Object -Maximum).Maximum,2)}else{$null};ReportedWindowMedianMs=if($windows.Count){[Math]::Round((@($windows.DurationMs|Sort-Object)[[int][Math]::Floor(($windows.Count-1)*0.5)]),2)}else{$null};BackwardTimestamps=$backward;ObservedMonotonic=($backward -eq 0);ClockBasis='-c system wall clock, millisecond resolution; observed monotonic only, not guaranteed monotonic clock';Rollover='not observable: EMON outputs interval deltas, not cumulative snapshots/width';LP0FActiveMeanMHz=if($fmean){[Math]::Round($fmean,1)}else{$null};Qualification10ms=($ms -eq 10 -and $coverageMs/$spanMs -ge 0.95 -and $maxGapMs -le 20)})
  foreach($w in $windows){$detail.Add([pscustomobject]@{RequestedCadenceMs=$ms;Timestamp=$w.Time.ToString('o');WindowDurationMs=[Math]::Round($w.DurationMs,3);LP0ThreadDelta=[long]$w.ThreadP0;LP0RefTscDelta=[long]$w.RefP0;LP0FActiveMHz=[Math]::Round($w.FActiveP0MHz,3)})}
}
$all | Export-Csv -NoTypeInformation -Encoding utf8 -LiteralPath (Join-Path (Join-Path $TelemetryRoot 'logs') 'emon_cadence_summary.csv')
$detail | Export-Csv -NoTypeInformation -Encoding utf8 -LiteralPath (Join-Path (Join-Path $TelemetryRoot 'logs') 'emon_cadence_samples.csv')
$report=[pscustomobject]@{Tool='Intel EMON 11.61';ReferenceFrequencyMHz=$freqMHz;Timestamps='-c (wall-clock, 1 ms); -per-cpu-absolute-tsc requested separately but rejected by parser';CadenceResults=@($all);BestTemporalThresholdCadenceMs=50;S19NominalCadence10msQualified=$false;TimestampMonotonicity='Observed nondecreasing in each series; wall clock is not guaranteed monotonic';Rollover='Unobservable from event deltas';Method='Valid interval windows reconstructed as timestamp - reported TSC interval through timestamp; coverage is union of these windows / elapsed span; holes are uncovered gaps between windows.'}
$report | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path (Join-Path $TelemetryRoot 'logs') 'emon_cadence_analysis.json') -Encoding utf8
$all | Format-Table RequestedCadenceMs,ExpectedGroups,ProducedGroups,DiscardedGroups,TemporalCoveragePct,MaxHoleMs,GroupIntervalP50Ms,GroupIntervalMaxMs,ObservedMonotonic -AutoSize
