$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$manifestName = 'SHA256SUMS_harness_r12.txt'
$files = @(Get-ChildItem -LiteralPath $root -File | Where-Object {
  $_.Name -match '_r12\.' -and $_.Name -ne $manifestName
} | Sort-Object Name)
if (-not $files.Count) { throw 'R12 manifest has no source files' }
$lines = foreach ($file in $files) {
  $sha = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
  "$sha  $($file.Name)"
}
$manifestPath = Join-Path $root $manifestName
[IO.File]::WriteAllLines($manifestPath, $lines, [Text.UTF8Encoding]::new($false))
$verify = [IO.File]::ReadAllLines($manifestPath)
if ($verify.Count -ne $files.Count) { throw "R12 manifest count mismatch expected=$($files.Count) actual=$($verify.Count)" }
foreach ($line in $verify) {
  if ($line -notmatch '^([0-9a-f]{64})  (.+)$') { throw "Bad manifest line: $line" }
  $path = Join-Path $root $Matches[2]
  $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($actual -ne $Matches[1]) { throw "R12 manifest mismatch: $($Matches[2])" }
}
"SHA256SUMS_harness_r12 PASS files=$($files.Count) coverage=100%"
