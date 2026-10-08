param([switch]$RebuildAssets)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\EnginePath.ps1"
$CastleDotNet = Join-Path $CastleEngineRoot 'Engine\Binaries\ThirdParty\DotNet\10.0\win-x64\dotnet.exe'
$CastleUBT = Join-Path $CastleEngineRoot 'Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll'
& $CastleDotNet $CastleUBT testfordemoEditor Win64 Development "-Project=$CastleProjectFile" -WaitMutex -NoHotReloadFromIDE
if ($LASTEXITCODE -ne 0) { throw 'C++ build failed.' }
if ($RebuildAssets) {
    & (Join-Path $CastleEngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') $CastleProjectFile -run=CastleSetup -unattended -nullrhi -nosplash
    if ($LASTEXITCODE -ne 0) { throw 'Demo asset creation failed.' }
}
