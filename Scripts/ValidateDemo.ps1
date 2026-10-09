param([int]$Seed = 1337, [ValidateSet('zh','en')][string]$Language, [switch]$MenuTest)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\EnginePath.ps1"
$CastleSmokeLog = Join-Path $CastleProjectRoot 'Saved\Logs\CastleSmoke.log'
$CastleValidationStarted = Get-Date
$CastleValidationArgs = @($CastleProjectFile, '/Game/Demo/L_CastleDemo', '-game', '-nullrhi', '-unattended', '-nosplash', '-nosound', '-CastleSmokeTest', "-abslog=$CastleSmokeLog")
if ($MenuTest) { $CastleValidationArgs += '-CastleMenuSmokeTest' } else { $CastleValidationArgs += "-CastleSeed=$Seed" }
if ($Language) { $CastleValidationArgs += "-CastleLanguage=$Language" }
& (Join-Path $CastleEngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') @CastleValidationArgs
if (!(Test-Path -LiteralPath $CastleSmokeLog) -or (Get-Item -LiteralPath $CastleSmokeLog).LastWriteTime -lt $CastleValidationStarted) { throw 'Runtime smoke test did not produce a fresh log. Check whether UnrealEditor-Cmd could start.' }
if ($LASTEXITCODE -ne 0 -or !(Select-String -LiteralPath $CastleSmokeLog -Pattern 'CASTLE_SMOKE_OK:' -Quiet)) { throw 'Runtime smoke test failed. See Saved/Logs/CastleSmoke.log.' }
