$settings = Join-Path $PSScriptRoot 'paths.local.ps1'
if (-not (Test-Path -LiteralPath $settings)) {
    throw 'Copy paths.example.ps1 to paths.local.ps1 and configure GameDir and MinecraftVersionDir first.'
}
. $settings
foreach ($name in @('GameDir','MinecraftVersionDir')) {
    $value = Get-Variable -Name $name -ValueOnly -ErrorAction SilentlyContinue
    if ([string]::IsNullOrWhiteSpace([string]$value)) { throw "Missing setting: $name" }
}
if (-not (Get-Variable -Name BackupRoot -ValueOnly -ErrorAction SilentlyContinue)) {
    $BackupRoot = Join-Path $PSScriptRoot 'runtime\backups'
}
$GameBinaries = Join-Path $GameDir 'EscapeTheBackrooms\Binaries\Win64'
$MinecraftMods = Join-Path $MinecraftVersionDir 'mods'
