$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$manifest=Join-Path $root 'SHA256SUMS_collector_r10.txt'
$lines=Get-ChildItem -LiteralPath $root -File -Recurse | Where-Object { $_.FullName -ne $manifest } | Sort-Object FullName | ForEach-Object {
    $relative=$_.FullName.Substring($root.Length).TrimStart('\').Replace('\','/')
    $hash=(Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLowerInvariant()
    "$hash  $relative"
}
$lines | Set-Content -LiteralPath $manifest -Encoding ascii
Write-Output "SHA256_MANIFEST entries=$($lines.Count) file=$manifest"
