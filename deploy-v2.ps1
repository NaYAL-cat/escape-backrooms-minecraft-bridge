param([switch]$Rollback,[switch]$Refresh)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
. (Join-Path $root 'load-paths.ps1')
$game=$GameBinaries
$mods=$MinecraftMods
$manifest=Join-Path $root 'runtime\deployment-v2.json'
$running=Get-CimInstance Win32_Process | Where-Object {
    $_.Name -match '^(EscapeTheBackrooms|Backrooms)-Win64-Shipping\.exe$' -or
    ($_.Name -in @('java.exe','javaw.exe') -and $_.CommandLine -match 'net\.fabricmc\.loader\.impl\.launch\.knot\.KnotClient|net\.minecraft\.client\.main\.Main')
}
if($running) { throw 'Close Minecraft and Escape the Backrooms before deploying or rolling back. No process was stopped.' }
function Restore-Deployment($data) {
    foreach($entry in $data.Files) {
        if($entry.Existed) { Copy-Item -LiteralPath $entry.Backup -Destination $entry.Target -Force }
        elseif(Test-Path -LiteralPath $entry.Target) {
            $stash=Join-Path $data.BackupRoot 'removed-on-rollback'
            New-Item -ItemType Directory -Path $stash -Force | Out-Null
            Move-Item -LiteralPath $entry.Target -Destination (Join-Path $stash ([IO.Path]::GetFileName($entry.Target))) -Force
        }
    }
    foreach($entry in $data.PreviousJars) { Copy-Item -LiteralPath $entry.Backup -Destination $entry.Target -Force }
}
if($Rollback) {
    if(-not(Test-Path -LiteralPath $manifest)) { throw 'No V2 deployment manifest exists.' }
    $data=Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
    Restore-Deployment $data
    $data.Status='RolledBack'
    $data | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifest -Encoding utf8
    Write-Output "Rolled back installed components. Worlds were not modified. Backup: $($data.BackupRoot)"
    exit 0
}
if(Test-Path -LiteralPath $manifest) {
    $old=Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
    if($old.Status -eq 'Installed') {
        if(-not $Refresh) { Write-Output 'V2 is already deployed. Use -Refresh to deploy a rebuilt V2 while preserving the original rollback.'; exit 0 }
        foreach($entry in $old.Files) { if(-not(Test-Path -LiteralPath $entry.Source)) { throw "Build artifact missing: $($entry.Source)" } }
        try {
            foreach($entry in $old.Files) {
                $hash=(Get-FileHash -LiteralPath $entry.Source -Algorithm SHA256).Hash
                New-Item -ItemType Directory -Path (Split-Path -Parent $entry.Target) -Force | Out-Null
                Copy-Item -LiteralPath $entry.Source -Destination $entry.Target -Force
                if((Get-FileHash -LiteralPath $entry.Target -Algorithm SHA256).Hash -ne $hash) { throw "Copy hash mismatch: $($entry.Target)" }
                $entry.SHA256=$hash
            }
            $old | Add-Member -NotePropertyName Refreshed -NotePropertyValue (Get-Date).ToString('o') -Force
            $old | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifest -Encoding utf8
            Write-Output "Refreshed V2; $($old.Files.Count) SHA256 checks passed. Original rollback preserved."
            exit 0
        } catch {
            Restore-Deployment $old
            $old.Status='RolledBackAfterError'
            $old | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifest -Encoding utf8
            throw
        }
    }
}
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$backup=Join-Path $BackupRoot "deploy-v2-$stamp"
New-Item -ItemType Directory -Path $backup,(Join-Path $root 'runtime') -Force | Out-Null
$pairs=@(
    @{Source=(Join-Path $root 'mc\build\libs\escape-backrooms-minecraft-bridge-0.2.0.jar');Target=(Join-Path $mods 'escape-backrooms-minecraft-bridge-0.2.0.jar')},
    @{Source=(Join-Path $root 'host\reshade\build\ETBMinecraft.addon64');Target=(Join-Path $game 'addons64\ETBMinecraft.addon64')},
    @{Source=(Join-Path $root 'host\reshade\shaders\ETBMinecraft.fx');Target=(Join-Path $game 'reshade-shaders\Shaders\ETBMinecraft.fx')}
)
foreach($name in @('main.lua','bridge_io.lua','bridge_camera.lua','bridge_collision.lua','bridge_entities.lua')) {
    $pairs+=@{Source=(Join-Path $root "host\ue4ss\Scripts\$name");Target=(Join-Path $game "ue4ss\Mods\BackroomsMinecraftBridge\Scripts\$name")}
}
$records=@(); $previous=@(); $index=0
foreach($pair in $pairs) {
    if(-not(Test-Path -LiteralPath $pair.Source)) { throw "Build artifact missing: $($pair.Source)" }
    $existed=Test-Path -LiteralPath $pair.Target
    $copy=Join-Path $backup (("{0:D2}-" -f $index)+[IO.Path]::GetFileName($pair.Target))
    $index++
    if($existed) { Copy-Item -LiteralPath $pair.Target -Destination $copy }
    $records += [ordered]@{Source=$pair.Source;Target=$pair.Target;Existed=[bool]$existed;Backup=$copy;SHA256=(Get-FileHash -LiteralPath $pair.Source -Algorithm SHA256).Hash}
}
foreach($jar in Get-ChildItem -LiteralPath $mods -Filter 'escape-backrooms-minecraft-bridge-*.jar') {
    if($jar.Name -eq 'escape-backrooms-minecraft-bridge-0.2.0.jar') { continue }
    $copy=Join-Path $backup $jar.Name
    Copy-Item -LiteralPath $jar.FullName -Destination $copy
    $previous += [ordered]@{Target=$jar.FullName;Backup=$copy}
}
$data=[ordered]@{Status='Prepared';Version='0.2.0';Protocol=2;BackupRoot=$backup;Files=$records;PreviousJars=$previous;Created=(Get-Date).ToString('o')}
$data | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifest -Encoding utf8
try {
    foreach($entry in $previous) {
        $archived=Join-Path $backup ('disabled-'+[IO.Path]::GetFileName($entry.Target))
        Move-Item -LiteralPath $entry.Target -Destination $archived
    }
    foreach($entry in $records) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $entry.Target) -Force | Out-Null
        Copy-Item -LiteralPath $entry.Source -Destination $entry.Target -Force
        if((Get-FileHash -LiteralPath $entry.Target -Algorithm SHA256).Hash -ne $entry.SHA256) { throw "Copy hash mismatch: $($entry.Target)" }
    }
    $data.Status='Installed'
    $data | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifest -Encoding utf8
    Write-Output "V2 deployed; $($records.Count) artifacts verified by SHA256. Backup: $backup"
} catch {
    Restore-Deployment $data
    $data.Status='RolledBackAfterError'
    $data | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifest -Encoding utf8
    throw
}
