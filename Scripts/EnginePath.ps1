$CastleEngineRoot = 'D:\UE5\UE_5.8'
if (!(Test-Path -LiteralPath (Join-Path $CastleEngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'))) {
    $CastleInstalled = Get-ItemProperty 'HKLM:\SOFTWARE\EpicGames\Unreal Engine\5.8' -ErrorAction SilentlyContinue
    if ($CastleInstalled.InstalledDirectory) { $CastleEngineRoot = $CastleInstalled.InstalledDirectory }
    else { throw 'UE 5.8 not found. Set CastleEngineRoot in Scripts/EnginePath.ps1.' }
}
$CastleProjectRoot = Split-Path -Parent $PSScriptRoot
$CastleProjectFile = Join-Path $CastleProjectRoot 'testfordemo.uproject'
