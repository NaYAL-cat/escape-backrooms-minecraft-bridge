# Copy this file to paths.local.ps1 and set your own installation paths.
$GameDir = 'D:\SteamLibrary\steamapps\common\EscapeTheBackrooms'
$MinecraftVersionDir = Join-Path $env:APPDATA '.minecraft\versions\26.3-Fabric 0.19.5'
# Keep generated state and backups beside this checkout unless you override it.
$BackupRoot = Join-Path $PSScriptRoot 'runtime\backups'
