$CastleProjectRoot = Split-Path -Parent $PSScriptRoot
$CastleProjectFile = Join-Path $CastleProjectRoot 'testfordemo.uproject'
$CastleAssociation = (Get-Content -LiteralPath $CastleProjectFile -Raw | ConvertFrom-Json).EngineAssociation
$CastleRelease = $CastleAssociation -match '^\d+\.\d+$'

# Match UnrealVersionSelector: Epic maps a release such as 5.8 to UE_5.8 in its installation list.
$CastleLauncherPath = Join-Path ([Environment]::GetFolderPath('CommonApplicationData')) 'Epic/UnrealEngineLauncher/LauncherInstalled.dat'
$CastleManifest = $null
if ($CastleRelease -and (Test-Path -LiteralPath $CastleLauncherPath)) {
    try { $CastleManifest = Get-Content -LiteralPath $CastleLauncherPath -Raw | ConvertFrom-Json -ErrorAction Stop }
    catch { Write-Verbose "Unable to read the Epic installation list: $_" }
}

$CastleCandidates = @($env:CASTLE_UE_ROOT)
if ($CastleAssociation) {
    if ($CastleRelease) {
        $CastleCandidates += $CastleManifest.InstallationList | Where-Object { $_.AppName -ceq "UE_$CastleAssociation" } | ForEach-Object InstallLocation
        $CastleCandidates += (Get-ItemProperty -LiteralPath "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$CastleAssociation" -ErrorAction SilentlyContinue).InstalledDirectory
        $CastleCandidates += Join-Path ([Environment]::GetFolderPath('ProgramFiles')) "Epic Games/UE_$CastleAssociation"
    }
    try {
        $CastleCandidates += Get-ItemPropertyValue -LiteralPath 'HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds' -Name $CastleAssociation -ErrorAction Stop
    } catch { Write-Verbose "No per-user engine registration for '$CastleAssociation'." }
}

$CastleCandidates += @('E:\UE5\UE_5.8', 'E:\Epic Games\UE_5.8', 'D:\UE5\UE_5.8')
$CastleEngineRoot = $null
foreach ($CastleCandidate in $CastleCandidates | Where-Object { $_ } | Select-Object -Unique) {
    if (!(Test-Path -LiteralPath (Join-Path $CastleCandidate 'Engine/Binaries/Win64/UnrealEditor.exe') -PathType Leaf)) { continue }
    if ($CastleRelease) {
        $CastleVersionPath = Join-Path $CastleCandidate 'Engine/Build/Build.version'
        if (!(Test-Path -LiteralPath $CastleVersionPath -PathType Leaf)) { continue }
        try { $CastleVersion = Get-Content -LiteralPath $CastleVersionPath -Raw | ConvertFrom-Json -ErrorAction Stop }
        catch { continue }
        if ("$($CastleVersion.MajorVersion).$($CastleVersion.MinorVersion)" -ne $CastleAssociation) { continue }
    }
    $CastleEngineRoot = (Resolve-Path -LiteralPath $CastleCandidate).ProviderPath
    break
}
if (!$CastleEngineRoot) { throw "Unreal Engine '$CastleAssociation' was not found. Install or register this release, or set CASTLE_UE_ROOT to its installation folder." }
