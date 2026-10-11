$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$manifest = Join-Path $root 'SHA256SUMS_kernel.txt'
$verification = Join-Path $root 'logs\hash_verify.txt'
$files = Get-ChildItem -LiteralPath $root -Recurse -File |
    Where-Object { $_.FullName -ne $manifest -and $_.FullName -ne $verification } |
    Sort-Object { $_.FullName.Substring($root.Length + 1) }
$rows = foreach ($file in $files) {
    $relative = $file.FullName.Substring($root.Length + 1).Replace('\', '/')
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $relative"
}
Set-Content -LiteralPath $manifest -Value $rows -Encoding ascii
$checked = 0
$failed = 0
foreach ($row in Get-Content -LiteralPath $manifest -Encoding ascii) {
    if ($row -notmatch '^([0-9a-f]{64})  (.+)$') { $failed++; continue }
    $expected = $Matches[1]
    $relative = $Matches[2].Replace('/', '\')
    $path = Join-Path $root $relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { $failed++; continue }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    $checked++
    if ($actual -ne $expected) { $failed++ }
}
$manifestCount = (Get-Content -LiteralPath $manifest -Encoding ascii).Count
$status = if ($failed -eq 0 -and $checked -eq $manifestCount) { 'PASS' } else { 'FAIL' }
$summary = "SHA256_MANIFEST status=$status checked=$checked entries=$manifestCount failures=$failed excluded=SHA256SUMS_kernel.txt,logs/hash_verify.txt"
Set-Content -LiteralPath $verification -Value $summary -Encoding ascii
Write-Output $summary
if ($status -ne 'PASS') { exit 2 }
