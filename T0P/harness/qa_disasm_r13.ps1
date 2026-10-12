$ErrorActionPreference = 'Stop'
$harness = $PSScriptRoot
$out = Join-Path $harness 'logs_r13\obj_r13'
$exe = Join-Path $out 'harness_r13.exe'
$map = Join-Path $out 'harness_r13.map'
$source = Join-Path $harness 'provider_aux_r13.cpp'
$dumpbin = Get-ChildItem 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe' | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
if (-not (Test-Path -LiteralPath $exe)) { throw "BLOCKED: missing built executable: $exe" }
if (-not (Test-Path -LiteralPath $map)) { throw "BLOCKED: missing linker map: $map" }
if (-not $dumpbin) { throw 'BLOCKED: dumpbin.exe not found in installed VS BuildTools' }

$disasmPath = Join-Path $out 'pinstr_disassembly_r13.txt'
$countsPath = Join-Path $out 'pinstr_disassembly_counts_r13.txt'
$qaPath = Join-Path $out 'disassembly_qa_r13.json'
$disasmLines = @(& $dumpbin /DISASM $exe 2>&1)
if ($LASTEXITCODE -ne 0) { throw "BLOCKED: command `"$dumpbin /DISASM $exe`" exit=$LASTEXITCODE; $($disasmLines -join ' ')" }
$disasmText = ($disasmLines -join "`n") + "`n"
$utf8 = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText($disasmPath, $disasmText, $utf8)
$disasm = [IO.File]::ReadAllLines($disasmPath)
$mapLines = [IO.File]::ReadAllLines($map)
$variants = @(@{c=1;u=8},@{c=2;u=8},@{c=4;u=8},@{c=8;u=8},@{c=10;u=8},@{c=12;u=8},@{c=14;u=8},@{c=12;u=16},@{c=14;u=16})
$symbols = @()
$allTextAddresses = @()
foreach ($line in $mapLines) {
  if ($line -match '^\s*0001:[0-9A-Fa-f]{8}\s+\S+\s+([0-9A-Fa-f]{16})\s+') {
    $allTextAddresses += [Convert]::ToUInt64($Matches[1],16)
  }
}
$allTextAddresses = @($allTextAddresses | Sort-Object -Unique)
foreach ($v in $variants) {
  $name = "pinstr_c$($v.c)_u$($v.u)"
  $mapLine = $mapLines | Where-Object { $_ -match "\s$name\s+([0-9A-Fa-f]{16})\s" } | Select-Object -First 1
  if (-not $mapLine -or $mapLine -notmatch "\s$name\s+([0-9A-Fa-f]{16})\s") { throw "BLOCKED: real map has no linked symbol $name" }
  $symbols += [pscustomobject]@{ name=$name; c=[int]$v.c; u=[int]$v.u; address=[Convert]::ToUInt64($Matches[1],16) }
}
$ordered = @($symbols | Sort-Object address)
$counts = @()
for ($i=0; $i -lt $ordered.Count; $i++) {
  $sym = $ordered[$i]
  $nextAddress = @($allTextAddresses | Where-Object { $_ -gt $sym.address } | Select-Object -First 1)
  $end = if ($nextAddress.Count) { [UInt64]$nextAddress[0] } else { [UInt64]::MaxValue }
  $ins = @()
  foreach ($line in $disasm) {
    if ($line -match '^\s*([0-9A-Fa-f]{16}):.*\bvpdpbusd\s+(ymm\d+)\s*,\s*(ymm\d+)\s*,\s*(ymm\d+)') {
      $address = [Convert]::ToUInt64($Matches[1],16)
      if ($address -ge $sym.address -and $address -lt $end) { $ins += [pscustomobject]@{dest=$Matches[2].ToLowerInvariant(); src1=$Matches[3].ToLowerInvariant(); src2=$Matches[4].ToLowerInvariant()} }
    }
  }
  $registers = @($ins | Group-Object dest | Sort-Object Name)
  $sourcePairs = @($ins | ForEach-Object { "$($_.src1):$($_.src2)" } | Sort-Object -Unique)
  $expectedInstructions = $sym.c * $sym.u
  $expectedRegisters = $sym.c
  $perChain = @($registers | Where-Object { $_.Count -ne $sym.u }).Count -eq 0
  $dependencies = $sourcePairs.Count -eq 1 -and $perChain
  $pass = $ins.Count -eq $expectedInstructions -and $registers.Count -eq $expectedRegisters -and $perChain -and $dependencies
  $counts += [pscustomobject]@{ variant=$sym.name; address=('0x{0:X16}' -f $sym.address); vpdpbusd=$ins.Count; expected=$expectedInstructions; independent_accumulators=$registers.Count; expected_accumulators=$expectedRegisters; instructions_per_accumulator=(@($registers | ForEach-Object Count) -join ','); dependency_chain=$dependencies; shared_inputs=($sourcePairs -join ','); pass=$pass }
}
$sourceText = [IO.File]::ReadAllText($source)
$modularSink = $sourceText.Contains('std::uint32_t sink=0') -and $sourceText.Contains('volatile std::uint32_t observed=sink')
$allPass = $modularSink -and @($counts | Where-Object { -not $_.pass }).Count -eq 0
$countText = @('P_instr real disassembly QA',"executable=$exe", "disassembly=$disasmPath", "disassembly_sha256=$((Get-FileHash -LiteralPath $disasmPath -Algorithm SHA256).Hash.ToLowerInvariant())", "modular_uint32_sink=$modularSink") + @($counts | ForEach-Object { "variant=$($_.variant) address=$($_.address) vpdpbusd=$($_.vpdpbusd)/$($_.expected) accumulators=$($_.independent_accumulators)/$($_.expected_accumulators) instructions_per_accumulator=$($_.instructions_per_accumulator) dependency_chain=$($_.dependency_chain) shared_inputs=$($_.shared_inputs) pass=$($_.pass)" })
[IO.File]::WriteAllText($countsPath, ($countText -join "`n") + "`n", $utf8)
$sha = (Get-FileHash -LiteralPath $disasmPath -Algorithm SHA256).Hash.ToLowerInvariant()
$result = [ordered]@{ state=$(if($allPass){'PASS'}else{'HOLD_INSTRUMENTATION'}); sha256=$sha; source='dumpbin /DISASM harness_r13.exe'; executable='harness_r13.exe'; modular_uint32_sink=$modularSink; variants=$counts }
$json = $result | ConvertTo-Json -Depth 8
[IO.File]::WriteAllText($qaPath, $json + "`n", $utf8)
Get-Content $countsPath
if (-not $allPass) { throw "P_instr disassembly does not qualify; state=HOLD_INSTRUMENTATION evidence=$countsPath" }
Write-Output "P_INSTR_DISASSEMBLY_QA state=PASS variants=$($counts.Count) sha256=$sha artifact=$disasmPath counts=$countsPath"
