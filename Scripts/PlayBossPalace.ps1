param([int]$Seed = 0)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\EnginePath.ps1"
if ($Seed -eq 0) { $Seed = Get-Random -Minimum 1 -Maximum 1000000000 }
if ($Seed -lt 1) { throw 'Seed must be positive.' }
Write-Host "Boss palace seed: $Seed"
& (Join-Path $CastleEngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe') $CastleProjectFile '/Game/Demo/L_CastleDemo' '-game' '-windowed' '-ResX=1600' '-ResY=1000' "-CastleSeed=$Seed" '-CastleBossPreview'
