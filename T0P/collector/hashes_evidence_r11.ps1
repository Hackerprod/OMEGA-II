param([switch]$Verify)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$harnessRoot = Join-Path $repoRoot 'T0P\harness\logs_r11'
$collectorRoot = Join-Path $PSScriptRoot 'logs_r11'
$testRoot = Join-Path $PSScriptRoot 'test_r11'
$manifest = Join-Path $collectorRoot 'SHA256SUMS_evidence_r11.txt'
$files = @()
foreach ($directory in @($harnessRoot, $collectorRoot, $testRoot)) {
    if (Test-Path -LiteralPath $directory -PathType Container) {
        $files += Get-ChildItem -LiteralPath $directory -File -Recurse | Where-Object { $_.FullName -ne $manifest }
    }
}
$lines = foreach ($file in ($files | Sort-Object FullName)) {
    $relative = $file.FullName.Substring($repoRoot.Length).TrimStart('\').Replace('\','/')
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash.ToLowerInvariant()
    "$hash  $relative"
}
if ($Verify) {
    if (!(Test-Path -LiteralPath $manifest -PathType Leaf)) { throw "manifest_missing:$manifest" }
    $expected = [IO.File]::ReadAllLines($manifest, [Text.Encoding]::ASCII)
    if (($expected -join "`n") -ne ($lines -join "`n")) { throw 'SHA256SUMS_evidence_r11_mismatch' }
    Write-Output "SHA256_VERIFY PASS entries=$($lines.Count) file=$manifest"
} else {
    [IO.File]::WriteAllLines($manifest, [string[]]$lines, [Text.Encoding]::ASCII)
    Write-Output "SHA256_MANIFEST entries=$($lines.Count) file=$manifest"
}
