$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$manifest=Join-Path $root 'SHA256SUMS_evict.txt_r5'
$verification=Join-Path $root 'SHA256_MANIFEST_VERIFY_r5.txt'
$files=Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object {
    $_.FullName -ne $manifest -and $_.FullName -ne $verification -and
    $_.FullName -notmatch '(?i)[\\/]__pycache__[\\/]' -and
    $_.Extension -notin @('.tmp','.temp','.bak')
} | Sort-Object { $_.FullName.Substring($root.Length+1) }
$rows=foreach($file in $files){
    $relative=$file.FullName.Substring($root.Length+1).Replace('\','/')
    $hash=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $relative"
}
Set-Content -LiteralPath $manifest -Value $rows -Encoding ascii
$checked=0;$failed=0
foreach($row in Get-Content -LiteralPath $manifest -Encoding ascii){
    if($row -notmatch '^([0-9a-f]{64})  (.+)$'){$failed++;continue}
    $expected=$Matches[1];$relative=$Matches[2].Replace('/','\');$path=Join-Path $root $relative
    if(!(Test-Path -LiteralPath $path -PathType Leaf)){$failed++;continue}
    $checked++
    if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected){$failed++}
}
$entries=(Get-Content -LiteralPath $manifest -Encoding ascii).Count
$status=if($failed -eq 0 -and $checked -eq $entries){'PASS'}else{'FAIL'}
$summary="SHA256_MANIFEST_R5 status=$status checked=$checked entries=$entries failures=$failed excluded=manifest,verification,__pycache__,temporary_extensions"
Set-Content -LiteralPath $verification -Value $summary -Encoding ascii
$summary
if($status -ne 'PASS'){exit 2}
