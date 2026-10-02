# build_audio_provider.ps1 - javax.sound microphone provider (voice chat mods)
param(
    [Parameter(Mandatory = $true)][string]$OutputJar
)

$ErrorActionPreference = "Stop"

. (Join-Path (Split-Path $PSScriptRoot -Parent) "scripts\common.ps1")

$srcJava = Join-Path $PSScriptRoot "src\main\java"
$srcResources = Join-Path $PSScriptRoot "src\main\resources"
$buildRoot = Join-Path (Get-ConfigPath "BuildDir") "audio_provider"
$classesDir = Join-Path $buildRoot "classes"

$sources = @(Get-ChildItem $srcJava -Recurse -Filter "*.java" | Select-Object -ExpandProperty FullName)
$resources = @(Get-ChildItem $srcResources -Recurse -File | Select-Object -ExpandProperty FullName)
if (-not $sources) { throw "No audio provider sources found under $srcJava" }

$stampPath = Join-Path $buildRoot "build.stamp"
$stamp = New-BuildStamp `
    -Values @("audio_provider", "jar=$OutputJar") `
    -ContentFiles (@($PSCommandPath) + $sources + $resources)

if (Test-BuildStampCurrent -StampPath $stampPath -Stamp $stamp -RequiredOutputs @($OutputJar)) {
    Write-Host "Audio input provider up to date: $OutputJar"
    return
}

Remove-Item -Recurse -Force $buildRoot -ErrorAction SilentlyContinue
Ensure-Dir $classesDir

$javaHome = Resolve-JavaHome
$javacExe = Join-Path $javaHome "bin\javac.exe"
$jarExe = Join-Path $javaHome "bin\jar.exe"
if (-not (Test-Path $javacExe)) { throw "javac.exe not found at $javacExe; audio input provider requires a JDK." }

# --release 8: the same jar must load on the Java 8 runtime (1.12.2) and on
# the modern runtime (1.20.1+). Pure javax.sound.sampled + JNI, no deps.
Write-Host "Building audio input provider -> $OutputJar"
$argsFile = Join-Path $buildRoot "javac-args.txt"
[System.IO.File]::WriteAllLines($argsFile, @(
    "--release", "8",
    "-proc:none",
    "-d", $classesDir
) + $sources)
& $javacExe "@$argsFile"
if ($LASTEXITCODE -ne 0) { throw "Audio input provider compile failed." }

Copy-Item -Recurse "$srcResources\*" $classesDir -Force

Ensure-Dir (Split-Path $OutputJar -Parent)
Push-Location $classesDir
& $jarExe cf $OutputJar .
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    throw "Audio input provider jar creation failed."
}
Pop-Location

$jarListing = @(& $jarExe tf $OutputJar)
foreach ($requiredEntry in @(
        "META-INF/services/javax.sound.sampled.spi.MixerProvider",
        "banditvault/audio/BanditMixerProvider.class",
        "banditvault/audio/BanditMicTargetDataLine.class")) {
    if ($jarListing -notcontains $requiredEntry) {
        throw "Audio input provider jar is missing $requiredEntry"
    }
}

Set-BuildStamp -StampPath $stampPath -Stamp $stamp
Write-Host "Audio input provider built: $OutputJar"
