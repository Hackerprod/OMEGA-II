param([switch]$Verify)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$manifest = Join-Path $root 'SHA256SUMS_collector_r11.txt'
$include = @(
    'COLLECTOR_SPEC_r11.md', 'OFFICIAL_RUN_r11.md', 'PAGE_EXPECTED_COLLECTOR_r11.json',
    'README_r11.md', 'STATUS_r11.md', 'hashes_r11.ps1', 'hashes_evidence_r11.ps1', 'official_run_r11.py',
    'plan_base_snapshot_r11.json', 'qualify_r11.py', 'residency_plan_r11.json',
    'test_r11/residency_smoke_conditions_r11.csv'
)
$lines = foreach ($relative in $include) {
    $path = Join-Path $root ($relative.Replace('/', '\'))
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "hash_input_missing:$relative" }
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant()
    "$hash  $relative"
}
if ($Verify) {
    if (!(Test-Path -LiteralPath $manifest -PathType Leaf)) { throw "manifest_missing:$manifest" }
    $expected = [IO.File]::ReadAllLines($manifest, [Text.Encoding]::ASCII)
    if (($expected -join "`n") -ne ($lines -join "`n")) { throw 'SHA256SUMS_collector_r11_mismatch' }
    Write-Output "SHA256_VERIFY PASS entries=$($lines.Count) file=$manifest"
} else {
    [IO.File]::WriteAllLines($manifest, [string[]]$lines, [Text.Encoding]::ASCII)
    Write-Output "SHA256_MANIFEST entries=$($lines.Count) file=$manifest"
}
