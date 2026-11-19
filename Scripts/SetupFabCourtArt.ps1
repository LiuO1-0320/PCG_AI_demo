param([switch]$UpdateEditorPreview)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/EnginePath.ps1"

$CastleFabFiles = @(
    'Content/Dungeon/Meshes/Props/SM_Wall_00.uasset',
    'Content/Dungeon/Meshes/Props/SM_Ground.uasset',
    'Content/Dungeon/Meshes/Props/SM_Pillar.uasset',
    'Content/Dungeon/Candles/Meshes/SM_Candles_Holder_00.uasset',
    'Content/Dungeon/Candles/Meshes/SM_Candle_Melted_01.uasset'
)
$CastleMissingFab = $CastleFabFiles | Where-Object {
    !(Test-Path -LiteralPath (Join-Path $CastleProjectRoot $_) -PathType Leaf)
}
if ($CastleMissingFab) {
    throw "Install Medieval Dungeon from your Fab library into this Unreal project. Missing files: $($CastleMissingFab -join ', ')"
}

$CastleEditor = Join-Path $CastleEngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$CastleFabScript = Join-Path $PSScriptRoot 'CourtArt/prepare_masonry.py'
& $CastleEditor $CastleProjectFile '-run=pythonscript' "-script=$CastleFabScript" '-EnablePlugins=PythonScriptPlugin' '-unattended' '-nullrhi' '-nosplash'
if ($LASTEXITCODE -ne 0) { throw 'Could not prepare the local Fab models. See Saved/Logs/testfordemo.log.' }

if ($UpdateEditorPreview) {
    $CastleMapPath = Join-Path $CastleProjectRoot 'Content/Demo/L_CastleDemo.umap'
    $CastleBackupDir = Join-Path $CastleProjectRoot 'Saved/Backups'
    New-Item -ItemType Directory -Path $CastleBackupDir -Force | Out-Null
    $CastleMapBackup = Join-Path $CastleBackupDir ('L_CastleDemo-{0}.umap' -f [Guid]::NewGuid())
    Copy-Item -LiteralPath $CastleMapPath -Destination $CastleMapBackup
    Write-Host "Map backup: $CastleMapBackup"

    & $CastleEditor $CastleProjectFile '-run=CastleSetup' '-PreviewOnly' '-unattended' '-nullrhi' '-nosplash'
    if ($LASTEXITCODE -ne 0) { throw 'Could not save the local editor preview. Build the project with BuildDemo.ps1 first.' }
}
