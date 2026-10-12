param([string]$Capture='capture_blocks_r2',[switch]$SelfTest)
$ErrorActionPreference='Stop'

function Get-LowerCoverage([double]$a,[double]$b,[object[]]$windows,[double]$uSeconds){
    $span=$b-$a
    if($span -le 0 -or $uSeconds -lt 0){return [pscustomobject]@{covered_s=0.0;coverage=0.0;max_gap_s=[double]::PositiveInfinity;segments=@()}}
    $segments=[Collections.Generic.List[object]]::new()
    foreach($w in $windows){
        $s=[math]::Max($a,[double]$w.s+$uSeconds)
        $e=[math]::Min($b,[double]$w.e-$uSeconds)
        if($e -gt $s){$segments.Add([pscustomobject]@{s=$s;e=$e})}
    }
    $sorted=@($segments|Sort-Object s,e);$united=[Collections.Generic.List[object]]::new()
    foreach($seg in $sorted){
        if(!$united.Count -or $seg.s -gt $united[$united.Count-1].e){$united.Add([pscustomobject]@{s=$seg.s;e=$seg.e})}
        else{$united[$united.Count-1].e=[math]::Max($united[$united.Count-1].e,$seg.e)}
    }
    $covered=0.0;$maxGap=0.0;$cursor=$a
    foreach($seg in $united){$maxGap=[math]::Max($maxGap,[double]$seg.s-$cursor);$covered+=[double]$seg.e-[double]$seg.s;$cursor=[double]$seg.e}
    $maxGap=[math]::Max($maxGap,$b-$cursor)
    return [pscustomobject]@{covered_s=$covered;coverage=$covered/$span;max_gap_s=$maxGap;segments=@($united)}
}

function Run-SelfTest{
    $partial=Get-LowerCoverage 0.0 2.5 @([pscustomobject]@{s=0.0;e=1.0}) 0.0
    if([math]::Abs($partial.coverage-.4) -gt 1e-12){throw '1000ms/2500ms must be 40 percent'}
    $internal=Get-LowerCoverage 0.0 2.5 @([pscustomobject]@{s=0.0;e=1.0},[pscustomobject]@{s=1.025;e=2.5}) 0.0
    if([math]::Abs($internal.max_gap_s-.025) -gt 1e-12){throw 'internal hole was not measured'}
    $endpoint=Get-LowerCoverage 0.0 2.5 @([pscustomobject]@{s=.025;e=2.5}) 0.0
    if([math]::Abs($endpoint.max_gap_s-.025) -gt 1e-12){throw 'endpoint hole was not measured'}
    $contracted=Get-LowerCoverage 0.0 2.5 @([pscustomobject]@{s=0.0;e=1.25},[pscustomobject]@{s=1.25;e=2.5}) .005
    if([math]::Abs($contracted.covered_s-2.48) -gt 1e-12 -or [math]::Abs($contracted.max_gap_s-.01) -gt 1e-12){throw 'windows were not individually contracted before union'}
    'ANALYZER_SELF_TEST=PASS units=seconds cases=1000ms_of_2500ms_is_40pct,internal_hole,endpoint_hole,contract_each_window_before_union'
}

if($SelfTest){Run-SelfTest;exit 0}
$root=Join-Path $PSScriptRoot $Capture
$rawPath=Join-Path $root 'emon_stdout.txt';$mapPath=Join-Path $root 'clock_map.csv';$marksPath=Join-Path $root 'eviction_windows.csv';$stdoutPath=Join-Path $root 'harness.stdout.txt';$latPath=Join-Path $root 'latencies.csv';$identityPath=Join-Path $root 'identity.txt'
foreach($p in @($rawPath,$mapPath,$marksPath,$stdoutPath,$latPath,$identityPath)){if(!(Test-Path -LiteralPath $p)){throw "required qualification evidence missing: $p"}}
$raw=Get-Content -LiteralPath $rawPath -Raw;$map=@(Import-Csv -LiteralPath $mapPath);$marks=@(Import-Csv -LiteralPath $marksPath);$latRows=@(Import-Csv -LiteralPath $latPath);$stdout=Get-Content -LiteralPath $stdoutPath -Raw;$identity=Get-Content -LiteralPath $identityPath -Raw
$qpf=[double][Diagnostics.Stopwatch]::Frequency
$ftscMatch=[regex]::Match($stdout,'(?m)^FTSC_QPC_HZ=([0-9.]+)\s*$')
$qpcMatch=[regex]::Match($stdout,'(?m)^QPC_FREQ_HZ=(\d+)\s*$')
$emonTscMatch=[regex]::Match($raw,'(?m)^TSC Freq\s+\.\.\.\.+\s+([0-9.]+) MHz\s*$')
if(!$ftscMatch.Success -or !$qpcMatch.Success -or !$emonTscMatch.Success){throw 'TSC/QPC frequency or EMON TSC declaration missing; HOLD_EVICTION_NOT_QUALIFIED'}
$ftsc=[double]::Parse($ftscMatch.Groups[1].Value,[Globalization.CultureInfo]::InvariantCulture);$qpcRecorded=[double]$qpcMatch.Groups[1].Value;$emonTsc=[double]::Parse($emonTscMatch.Groups[1].Value,[Globalization.CultureInfo]::InvariantCulture)*1e6
$ftscOk=($ftsc -gt 0 -and $qpcRecorded -eq $qpf -and [math]::Abs($ftsc-$emonTsc)/$emonTsc -le .001)

$x=[Collections.Generic.List[double]]::new();$y=[Collections.Generic.List[double]]::new();$brackets=[Collections.Generic.List[double]]::new()
foreach($r in $map){$x.Add([double]$r.filetime_100ns);$y.Add(([double]$r.qpc_pre+[double]$r.qpc_post)/2.0);$brackets.Add(([double]$r.qpc_post-[double]$r.qpc_pre)/$qpf)}
if($x.Count -lt 20){throw "clock map has fewer than 20 observations: $($x.Count)"}
$xm=($x|Measure-Object -Average).Average;$ym=($y|Measure-Object -Average).Average;$num=0.0;$den=0.0
for($i=0;$i -lt $x.Count;$i++){$num+=($x[$i]-$xm)*($y[$i]-$ym);$den+=($x[$i]-$xm)*($x[$i]-$xm)}
if($den -le 0){throw 'clock map is singular'}
$slope=$num/$den;$intercept=$ym-$slope*$xm;$residuals=[Collections.Generic.List[double]]::new()
for($i=0;$i -lt $x.Count;$i++){$residuals.Add([math]::Abs($y[$i]-($intercept+$slope*$x[$i]))/$qpf)}
$uSeconds=($residuals|Measure-Object -Maximum).Maximum+($brackets|Measure-Object -Maximum).Maximum/2.0+.0005

$eventNames=@('CPU_CLK_UNHALTED.THREAD','CPU_CLK_UNHALTED.REF_TSC','L2_RQSTS.DEMAND_DATA_RD_MISS','L2_RQSTS.ALL_DEMAND_DATA_RD','OFFCORE_REQUESTS.L3_MISS_DEMAND_DATA_RD','OFFCORE_REQUESTS.DEMAND_DATA_RD')
function Parse-Count([string]$s){$s=$s.Trim();if($s -notmatch '^\d[\d,]*(?:\.\d+)?$'){return [double]::NaN};return [double]::Parse($s.Replace(',',''),[Globalization.CultureInfo]::InvariantCulture)}
function Is-Finite([double]$v){return -not [double]::IsNaN($v) -and -not [double]::IsInfinity($v)}
$records=[Collections.Generic.List[object]]::new()
foreach($chunk in [regex]::Split($raw,'(?m)^==========\s*$')){
    $tm=[regex]::Match($chunk,'(?m)^(\d{1,2}/\d{1,2}/\d{4}\s+\d{2}:\d{2}:\d{2}\.\d{3})\s*$');if(!$tm.Success){continue}
    $dt=[datetime]::ParseExact($tm.Groups[1].Value,'M/d/yyyy HH:mm:ss.fff',[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::AssumeLocal);$ft=[double]$dt.ToFileTimeUtc();$endQpc=$intercept+$slope*$ft;$events=@{}
    foreach($line in ($chunk -split "`r?`n")){if(!$line.Contains("`t")){continue};$fields=$line -split "`t";$name=$fields[0].Trim();if($eventNames -notcontains $name){continue};$v=[Collections.Generic.List[double]]::new();for($j=1;$j -lt $fields.Count;$j++){$v.Add((Parse-Count $fields[$j]))};$events[$name]=$v.ToArray()}
    if(@($eventNames|Where-Object {!$events.ContainsKey($_)}).Count){continue}
    $records.Add([pscustomobject]@{end_s=$endQpc/$qpf;events=$events})
}
if($records.Count -lt 20){throw "EMON has fewer than 20 complete event groups: $($records.Count)"}
$latencies=@{}
foreach($r in $latRows){$core=[int]$r.Pcore;$cond=$r.condition;if(!$latencies.ContainsKey($core)){$latencies[$core]=@{}};if(!$latencies[$core].ContainsKey($cond)){$latencies[$core][$cond]=[Collections.Generic.List[double]]::new()};$latencies[$core][$cond].Add([double]$r.cycles)}
$summaries=@{}
foreach($line in ($stdout -split "`r?`n")){if($line -match '^CACHE_LATENCY Pcore=(\d+) LP=(\d+) condition=(\S+) samples=(\d+) median_cycles=(\d+) p95_cycles=(\d+)'){ $key="$($Matches[1])/$($Matches[3])";$summaries[$key]=[pscustomobject]@{core=[int]$Matches[1];lp=[int]$Matches[2];condition=$Matches[3];samples=[int]$Matches[4];median=[double]$Matches[5];p95=[double]$Matches[6]} }}
$coverageLines=@([regex]::Matches($stdout,'(?m)^WEIGHT_COVERAGE core=(\d+) LP=(\d+) bytes=294912 lines=4608 expected_lines=4608 status=PASS\s*$'))
$identityOk=($identity -match '(?m)^LINKED_OBJECT=evict_r3\.obj\s*$' -and $identity -match '(?m)^SOURCE_SHA256=[0-9a-f]{64}\s*$' -and $identity -match '(?m)^OBJECT_SHA256=[0-9a-f]{64}\s*$' -and $identity -match '(?m)^EXECUTABLE_SHA256=[0-9a-f]{64}\s*$')

$out=[Collections.Generic.List[object]]::new();$marksByKey=@{}
foreach($m in $marks){$key="$([int]$m.Pcore)/$($m.condition)";if($marksByKey.ContainsKey($key)){throw "duplicate condition mark: $key"};$marksByKey[$key]=$m}
for($core=0;$core -lt 8;$core++){
    foreach($cond in @('cold_shards8','hot_shards8')){
        $key="$core/$cond";$mark=$marksByKey[$key];$temporal=$false;$physical=$false;$ratioPass=$false;$llc=[double]::NaN;$l2=[double]::NaN;$ratio=[double]::NaN;$windows=0;$coverage=0.0;$gap=[double]::PositiveInfinity;$maxWin=0.0;$mi=0.0;$mf=0.0;$l2m=0.0;$l2d=0.0;$llcm=0.0;$llcd=0.0
        if($mark){
            $padA=[double]$mark.qpc_pad_start/$qpf;$a=[double]$mark.qpc_start/$qpf;$b=[double]$mark.qpc_end/$qpf;$padB=[double]$mark.qpc_pad_end/$qpf;$span=$b-$a;$mi=$a-$padA;$mf=$padB-$b;$usable=[Collections.Generic.List[object]]::new()
            foreach($rec in $records){$ref=$rec.events['CPU_CLK_UNHALTED.REF_TSC'];$index=2*$core+1;if($ref.Count -le $index){continue};$dur=$ref[$index]/$ftsc;if(!(Is-Finite $dur) -or $dur -le 0){continue};$s=$rec.end_s-$dur;$e=$rec.end_s;if($s -lt $b -and $e -gt $a -and $s-$uSeconds -ge $padA -and $e+$uSeconds -le $padB){$usable.Add([pscustomobject]@{s=$s;e=$e;dur_s=$dur;events=$rec.events})}}
            $windows=$usable.Count;$cov=Get-LowerCoverage $a $b @($usable|ForEach-Object {[pscustomobject]@{s=$_.s;e=$_.e}}) $uSeconds;$coverage=$cov.coverage;$gap=$cov.max_gap_s;if($usable.Count){$maxWin=($usable|Measure-Object dur_s -Maximum).Maximum}
            foreach($rec in $usable){foreach($lp in @(2*$core,2*$core+1)){$idx=$lp+1;$l2m+=$rec.events['L2_RQSTS.DEMAND_DATA_RD_MISS'][$idx];$l2d+=$rec.events['L2_RQSTS.ALL_DEMAND_DATA_RD'][$idx];$llcm+=$rec.events['OFFCORE_REQUESTS.L3_MISS_DEMAND_DATA_RD'][$idx];$llcd+=$rec.events['OFFCORE_REQUESTS.DEMAND_DATA_RD'][$idx]}}
            if($llcd -gt 0){$llc=$llcm/$llcd};if($l2d -gt 0){$l2=$l2m/$l2d}
            $temporal=($ftscOk -and $mi -ge .200 -and $mf -ge .200 -and $windows -ge 20 -and $coverage -ge .95 -and $gap -le .020 -and $maxWin -le .100 -and $uSeconds -le .001 -and $span -ge 2.5)
            $physical=if($cond -eq 'cold_shards8'){(Is-Finite $llc) -and $llc -ge .90}else{(Is-Finite $llc) -and $llc -le .10}
            $coldSummary=$summaries["$core/cold_shards8"];if($coldSummary -and $summaries["$core/hot_shards8"]){$hotSummary=$summaries["$core/hot_shards8"];if($hotSummary.median -gt 0){$ratio=$coldSummary.median/$hotSummary.median};$ratioPass=((Is-Finite $ratio) -and $ratio -ge 1.50 -and $coldSummary.samples -ge 20 -and $hotSummary.samples -ge 20)}
        }
        $status=if($identityOk -and $coverageLines.Count -eq 8 -and $temporal -and $physical -and $ratioPass){'EVICTION_QUALIFIED'}else{'HOLD_EVICTION_NOT_QUALIFIED'}
        $out.Add([pscustomobject]@{Pcore=$core;LP_pair="$(2*$core),$(2*$core+1)";condition=$cond;span_s=[math]::Round($(if($mark){$span}else{0}),6);margin_ini_s=[math]::Round($mi,6);margin_fin_s=[math]::Round($mf,6);windows=$windows;C_low_pct=[math]::Round(100*$coverage,4);gap_upper_s=[math]::Round($gap,6);max_window_s=[math]::Round($maxWin,6);ratio=[math]::Round($ratio,6);LLC_miss=$llcm;LLC_demand=$llcd;LLC_fraction=[math]::Round($llc,6);L2_miss_pair=$l2m;L2_demand_pair=$l2d;L2_fraction_pair=[math]::Round($l2,6);temporal=if($temporal){'PASS'}else{'HOLD'};physical=if($physical){'PASS'}else{'HOLD'};ratio_control=if($ratioPass){'PASS'}else{'HOLD'};status=$status})
    }
}
$dest=Join-Path $root 'padded_block_analysis.csv';$out|Export-Csv -NoTypeInformation -Encoding utf8 -LiteralPath $dest
$required=@($marksByKey.Keys|Where-Object {$_ -match '^(\d+)/(cold_shards8|hot_shards8)$'});$all=$out.Count -eq 16 -and $required.Count -eq 16 -and $out.Where({$_.status -ne 'EVICTION_QUALIFIED'}).Count -eq 0
"EMON_GROUPS_PARSED=$($records.Count) CLOCK_MAP_ROWS=$($map.Count) QPC_HZ=$qpf FTSC_HZ=$ftsc EMON_TSC_HZ=$emonTsc TSC_QPC_MATCH=$ftscOk U_seconds=$([math]::Round($uSeconds,9)) weight_coverage_cores=$($coverageLines.Count)/8 identity=$identityOk"
'WINDOW_LABEL=windows of the same condition that cover M; counter values are interval deltas and are not claimed to be exclusive to the timed load.'
$out|Format-Table -AutoSize|Out-String -Width 320|Write-Output
"EVICTION_QUALIFICATION=$(if($all){'EVICTION_QUALIFIED'}else{'HOLD_EVICTION_NOT_QUALIFIED'})"
if(!$all){exit 2}
