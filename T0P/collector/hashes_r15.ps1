$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $PSScriptRoot).Path
$obj = Join-Path $root 'logs_r15\obj_r15'
if (!(Test-Path -LiteralPath $obj -PathType Container)) { throw "R15 object/log directory missing: $obj" }
$manifestName = 'SHA256SUMS_collector_r15.txt'
$manifestPath = Join-Path $root $manifestName
$files = @(
  Get-ChildItem -LiteralPath $obj -File -Recurse
  Get-ChildItem -LiteralPath $root -File | Where-Object {
    $_.Name -match 'r15' -and $_.Name -ne $manifestName -and $_.FullName -ne $manifestPath
  }
) | Sort-Object FullName -Unique
$entries = foreach ($file in $files) {
  $relative = $file.FullName.Substring($root.Length).TrimStart('\').Replace('\','/')
  $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
  "$hash  $relative"
}
[IO.File]::WriteAllLines($manifestPath, [string[]]$entries, [Text.UTF8Encoding]::new($false))
$listed = @()
$valid = 0
foreach ($line in Get-Content -LiteralPath $manifestPath) {
  if ($line -notmatch '^([0-9a-f]{64})  (.+)$') { throw "Malformed manifest entry: $line" }
  $expected = $Matches[1]; $relative = $Matches[2]; $listed += $relative
  $path = Join-Path $root ($relative.Replace('/','\'))
  $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($actual -ne $expected) { throw "SHA256 mismatch: $relative" }
  $valid++
}
$expectedNames = @($files | ForEach-Object { $_.FullName.Substring($root.Length).TrimStart('\').Replace('\','/') } | Sort-Object)
$listedNames = @($listed | Sort-Object)
if (($expectedNames -join "`n") -ne ($listedNames -join "`n")) { throw 'Manifest file set is incomplete or contains duplicates' }
$manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
"SHA256SUMS_COLLECTOR_R15 PASS scope_files=$($files.Count) hashes_valid=$valid set_exact=true manifest_sha256=$manifestHash"
