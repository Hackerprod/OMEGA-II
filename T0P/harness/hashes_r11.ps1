param([switch]$Verify)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$manifest = Join-Path $root 'SHA256SUMS_harness_r11.txt'
$files = Get-ChildItem -LiteralPath $root -File -Recurse | Where-Object {
    $relative = $_.FullName.Substring($root.Length).TrimStart('\').Replace('\','/')
    $parts = $relative.Split('/')
    $underBuildOutput = $parts -contains 'obj_r11'
    $r11Deliverable = $_.Name -match '_r11(\.|$)'
    $relative -ne 'SHA256SUMS_harness_r11.txt' -and
        -not ($parts -contains 'logs_r11') -and
        -not $relative.StartsWith('obj_r11/tmp/', [StringComparison]::OrdinalIgnoreCase) -and
        ($underBuildOutput -or $r11Deliverable)
} | Sort-Object FullName
$lines = foreach ($file in $files) {
    $relative = $file.FullName.Substring($root.Length).TrimStart('\').Replace('\','/')
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash.ToLowerInvariant()
    "$hash  $relative"
}
if ($Verify) {
    if (!(Test-Path -LiteralPath $manifest -PathType Leaf)) { throw "manifest_missing:$manifest" }
    $expected = [IO.File]::ReadAllLines($manifest, [Text.Encoding]::ASCII)
    if (($expected -join "`n") -ne ($lines -join "`n")) { throw 'SHA256SUMS_harness_r11_mismatch' }
    Write-Output "SHA256_VERIFY PASS entries=$($lines.Count) file=$manifest"
} else {
    [IO.File]::WriteAllLines($manifest, [string[]]$lines, [Text.Encoding]::ASCII)
    Write-Output "SHA256_MANIFEST entries=$($lines.Count) file=$manifest"
}
