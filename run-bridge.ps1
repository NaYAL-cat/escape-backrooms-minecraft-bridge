$ErrorActionPreference='Stop'
$root=$PSScriptRoot
. (Join-Path $root 'load-paths.ps1')
$manifest=Join-Path $root 'runtime\deployment-v2.json'
if(-not(Test-Path -LiteralPath $manifest)) { throw 'V2 has not been deployed. Run deploy-v2.ps1 first.' }
$data=Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
if($data.Status -like 'RolledBack*') { throw 'This deployment was rolled back. Deploy V2 again before starting this coordinator.' }
if($data.Status -ne 'Installed') { throw 'V2 deployment is incomplete. See runtime/deployment-v2.json.' }
foreach($entry in $data.Files) {
    if(-not(Test-Path -LiteralPath $entry.Target)) { throw "Installed component missing: $($entry.Target)" }
    if((Get-FileHash -LiteralPath $entry.Target -Algorithm SHA256).Hash -ne $entry.SHA256) { throw "Installed component changed: $($entry.Target). Rebuild and deploy matching components." }
}
$env:PYTHONUTF8='1'
$env:PYTHONPYCACHEPREFIX=Join-Path $root 'runtime\pycache'
& py -3.12 (Join-Path $root 'host\bridge\bridge.py') --game-dir $GameDir
exit $LASTEXITCODE
