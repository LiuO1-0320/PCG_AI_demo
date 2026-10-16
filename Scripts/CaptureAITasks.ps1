param([int]$Seed = 1337)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\EnginePath.ps1"
$CastleAICaptureDir = Join-Path $CastleProjectRoot 'output\ai_tasks_en_2026-10-14\screenshots'
New-Item -ItemType Directory -Force -Path $CastleAICaptureDir | Out-Null
$CastleAICaptureLog = Join-Path $CastleProjectRoot 'Saved\Logs\CastleAITaskCapture.log'
& (Join-Path $CastleEngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') $CastleProjectFile '/Game/Demo/L_CastleDemo' '-game' '-windowed' '-ForceRes' '-ResX=1920' '-ResY=1080' '-unattended' '-nosplash' '-nosound' '-CastleAICapture' '-CastleLanguage=en' "-CastleSeed=$Seed" "-abslog=$CastleAICaptureLog"
if ($LASTEXITCODE -ne 0 -or !(Select-String -LiteralPath $CastleAICaptureLog -Pattern 'CASTLE_AI_TASK_CAPTURE_OK:' -Quiet)) { throw 'AI task capture failed. See Saved/Logs/CastleAITaskCapture.log.' }
foreach ($CastleShotName in @('01_guard_ranged','02_guard_melee','03_boss_ranged','04_boss_enraged_ranged','05_boss_melee','06_boss_slam_charge')) {
    if (!(Test-Path -LiteralPath (Join-Path $CastleAICaptureDir "$CastleShotName.png"))) { throw "Missing screenshot: $CastleShotName" }
}
