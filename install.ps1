param([switch]$Refresh)
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'deploy-v2.ps1') @PSBoundParameters
exit $LASTEXITCODE
