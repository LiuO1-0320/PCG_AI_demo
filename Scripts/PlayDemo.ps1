param([int]$Seed = 0)
. "$PSScriptRoot\EnginePath.ps1"
$CastleArgs = @($CastleProjectFile, '/Game/Demo/L_CastleDemo', '-game', '-windowed', '-ForceRes', '-ResX=1440', '-ResY=900')
if ($Seed -gt 0) { $CastleArgs += "-CastleSeed=$Seed" }
& (Join-Path $CastleEngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe') @CastleArgs
