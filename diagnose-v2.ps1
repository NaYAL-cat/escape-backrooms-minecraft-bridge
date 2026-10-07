$ErrorActionPreference='Stop'
$root=$PSScriptRoot
. (Join-Path $root 'load-paths.ps1')
New-Item -ItemType Directory -Path (Join-Path $root 'logs') -Force | Out-Null
$out=Join-Path $root ('logs\diagnostic-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'.txt')
$rows=New-Object 'System.Collections.Generic.List[string]'
$rows.Add('ETB x Minecraft V2 diagnostics')
$rows.Add((Get-Date).ToString('o'))
foreach($name in @('runtime\status.json','runtime\deployment-v2.json')) {
    $path=Join-Path $root $name
    if(Test-Path -LiteralPath $path) { $rows.Add($name);$rows.Add((Get-Content -LiteralPath $path -Raw)) }
}
$game=$GameBinaries
foreach($path in @((Join-Path $root 'logs\bridge-v2.log'),(Join-Path $game 'ue4ss\UE4SS.log'),(Join-Path $game 'ReShade.log'))) {
    if(Test-Path -LiteralPath $path) { $rows.Add($path);foreach($line in Get-Content -LiteralPath $path -Tail 160) { $rows.Add($line) } }
}
$manifest=Join-Path $root 'runtime\deployment-v2.json'
if(Test-Path -LiteralPath $manifest) {
    $data=Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
    foreach($entry in $data.Files) {
        $ok=(Test-Path -LiteralPath $entry.Target) -and ((Get-FileHash -LiteralPath $entry.Target -Algorithm SHA256).Hash -eq $entry.SHA256)
        $rows.Add("Integrity=$ok $($entry.Target)")
    }
}
$rows | Set-Content -LiteralPath $out -Encoding utf8
Write-Output "Saved: $out"
