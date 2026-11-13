param([int]$Seed = 0, [switch]$MemoryRoom)
. "$PSScriptRoot\EnginePath.ps1"
$CastleArgs = @($CastleProjectFile, '/Game/Demo/L_CastleDemo', '-game', '-windowed', '-ForceRes', '-ResX=1440', '-ResY=900')
if ($MemoryRoom -and $Seed -le 0) { $Seed = 1337 }
if ($Seed -gt 0) { $CastleArgs += "-CastleSeed=$Seed" }
if ($MemoryRoom) { $CastleArgs += '-CastleMemoryPreview' }
& (Join-Path $CastleEngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe') @CastleArgs
