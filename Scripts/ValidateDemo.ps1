param([int]$Seed = 1337)
. "$PSScriptRoot\EnginePath.ps1"
$CastleSmokeLog = Join-Path $CastleProjectRoot 'Saved\Logs\CastleSmoke.log'
& (Join-Path $CastleEngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') $CastleProjectFile '/Game/Demo/L_CastleDemo' -game -nullrhi -unattended -nosplash -nosound -CastleSmokeTest "-CastleSeed=$Seed" "-abslog=$CastleSmokeLog"
if ($LASTEXITCODE -ne 0 -or !(Select-String -LiteralPath $CastleSmokeLog -Pattern 'CASTLE_SMOKE_OK:' -Quiet)) { throw 'Runtime smoke test failed. See Saved/Logs/CastleSmoke.log.' }
