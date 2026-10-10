# Run in a fresh PowerShell process: setup, focused tests, then the signed APPX.
# Requires Windows/MSVC/SDK and only an exact JDK 21 (including lib/src.zip).
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$root = Resolve-RepoRoot
$target = Select-NeoForge1211Target -Catalog @(Import-Csv (Join-Path $root $ProjectConfig.VersionCatalog) -Delimiter "`t")
$savedJavaHome = $env:JAVA_HOME
$savedMcVersion = $env:MC_VERSION
$savedBuildGameDir = $env:BANDIT_BUILD_GAME_DIR
try {
    $env:JAVA_HOME = Resolve-JavaHomeExact -MajorVersion 21
    $env:MC_VERSION = "1.21.1"
    Write-Host "Only Minecraft 1.21.1 / NeoForge $($target.loaderVersion) / Java 21 will be prepared and packaged."
    & (Join-Path $PSScriptRoot "setup.ps1") -NeoForgeOnly
    & (Join-Path $PSScriptRoot "test-uwp-launch.ps1") -NeoForgeOnly
    & (Join-Path $root "build.ps1") -NeoForgeOnly -StrictTargets
} finally {
    $env:JAVA_HOME = $savedJavaHome
    $env:MC_VERSION = $savedMcVersion
    $env:BANDIT_BUILD_GAME_DIR = $savedBuildGameDir
}
