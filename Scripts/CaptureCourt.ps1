param([int[]]$Seeds = @(1337, 2026))
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/EnginePath.ps1"
$CastleRenderer = Join-Path $CastleEngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
foreach ($CastleReviewSeed in $Seeds) {
    if ($CastleReviewSeed -le 0) { throw 'Choose a positive map seed.' }
    $CastleReviewLog = Join-Path $CastleProjectRoot "Saved/Logs/CourtReview$CastleReviewSeed.log"
    $CastleStarted = [DateTime]::UtcNow
    & $CastleRenderer $CastleProjectFile '/Game/Demo/L_CastleDemo' '-game' '-RenderOffscreen' '-windowed' '-ForceRes' '-ResX=1440' '-ResY=900' '-unattended' '-nosound' '-nosplash' '-CastleLanguage=zh' "-CastleSeed=$CastleReviewSeed" '-CastleCourtCapture' "-abslog=$CastleReviewLog"
    if ($LASTEXITCODE -ne 0 -or !(Select-String -LiteralPath $CastleReviewLog -SimpleMatch "CASTLE_COURT_CAPTURE_OK Seed=$CastleReviewSeed Frames=3" -Quiet)) { throw "Court capture failed for seed $CastleReviewSeed." }
    foreach ($CastleView in 'Entrance','Combat','Rest') {
        $CastleImage = Get-Item -LiteralPath (Join-Path $CastleProjectRoot "Saved/Screenshots/MainCourt/Seed${CastleReviewSeed}_$CastleView.png")
        if ($CastleImage.Length -eq 0 -or $CastleImage.LastWriteTimeUtc -lt $CastleStarted) { throw "Missing current screenshot: $CastleImage" }
    }
}
