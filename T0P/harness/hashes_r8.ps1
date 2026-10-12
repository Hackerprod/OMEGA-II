$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$manifest=Join-Path $root 'SHA256SUMS_harness_r8.txt'
$items=Get-ChildItem -LiteralPath $root -File -Recurse|Where-Object{$_.FullName -ne $manifest}|Sort-Object FullName
$lines=foreach($f in $items){$rel=$f.FullName.Substring($root.Length).TrimStart('\').Replace('\','/');$h=(Get-FileHash -Algorithm SHA256 -LiteralPath $f.FullName).Hash.ToLowerInvariant();"$h  $rel"}
$lines|Set-Content -LiteralPath $manifest -Encoding ascii
"SHA256_MANIFEST entries=$($items.Count) excludes_self=true file=$manifest"

