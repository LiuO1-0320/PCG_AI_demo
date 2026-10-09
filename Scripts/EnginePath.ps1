$CastleInstalled = Get-ItemProperty 'HKLM:\SOFTWARE\EpicGames\Unreal Engine\5.8' -ErrorAction SilentlyContinue
$CastleCandidates = @(
    $env:CASTLE_UE_ROOT
    $CastleInstalled.InstalledDirectory
    'E:\Epic Games\UE_5.8'
    'D:\UE5\UE_5.8'
    'C:\Program Files\Epic Games\UE_5.8'
)
$CastleEngineRoot = $CastleCandidates | Where-Object {
    $_ -and (Test-Path -LiteralPath (Join-Path $_ 'Engine\Binaries\Win64\UnrealEditor.exe'))
} | Select-Object -First 1
if (!$CastleEngineRoot) { throw 'UE 5.8 not found. Set CASTLE_UE_ROOT to your UE 5.8 installation directory.' }
$CastleProjectRoot = Split-Path -Parent $PSScriptRoot
$CastleProjectFile = Join-Path $CastleProjectRoot 'testfordemo.uproject'
