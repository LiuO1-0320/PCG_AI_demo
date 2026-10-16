param([int]$Seed = 1337)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\EnginePath.ps1"
$CastleAILog = Join-Path $CastleProjectRoot 'Saved\Logs\CastleAISmoke.log'
$CastleAIStarted = Get-Date
& (Join-Path $CastleEngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') $CastleProjectFile '/Game/Demo/L_CastleDemo' '-game' '-nullrhi' '-unattended' '-nosplash' '-nosound' '-CastleAISmokeTest' "-CastleSeed=$Seed" "-abslog=$CastleAILog"
if (!(Test-Path -LiteralPath $CastleAILog) -or (Get-Item -LiteralPath $CastleAILog).LastWriteTime -lt $CastleAIStarted) { throw 'AI test did not produce a fresh log.' }
if ($LASTEXITCODE -ne 0 -or !(Select-String -LiteralPath $CastleAILog -Pattern 'CASTLE_AI_SMOKE_OK:' -Quiet)) { throw 'AI behavior test failed. See Saved/Logs/CastleAISmoke.log.' }
