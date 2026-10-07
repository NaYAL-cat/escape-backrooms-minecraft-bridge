$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'deploy-v2.ps1') -Rollback
exit $LASTEXITCODE
