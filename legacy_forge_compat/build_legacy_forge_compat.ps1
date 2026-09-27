param(
    [string]$MinecraftVersion = "1.12.2",
    [string]$ForgeVersion = "1.12.2-14.23.5.2864",
    [string]$OutputDir
)

$ErrorActionPreference = "Stop"

. (Join-Path (Split-Path $PSScriptRoot -Parent) "scripts\common.ps1")

$gameDir = Get-ConfigPath "GameDir"
if (-not $OutputDir) {
    $OutputDir = Join-Path $gameDir "mods"
}

$forgeDir = Join-Path $gameDir "libraries\net\minecraftforge\forge\$ForgeVersion"
$forgeJar = Join-Path $forgeDir "forge-$ForgeVersion-universal.jar"
$launchwrapperDir = Join-Path $gameDir "libraries\net\minecraft\launchwrapper\1.12"
$launchwrapperJar = Join-Path $launchwrapperDir "launchwrapper-1.12.jar"
$asmDir = Join-Path $gameDir "libraries\org\ow2\asm\asm-debug-all\5.2"
$asmJar = Join-Path $asmDir "asm-debug-all-5.2.jar"

$lwjglRoot = Join-Path $gameDir "libraries\org\lwjgl\lwjgl"
$lwjglCandidates = @(
    Get-ChildItem -LiteralPath $lwjglRoot -Recurse -Filter "lwjgl-*.jar" -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^lwjgl-2\.9\.4(?:-|\.)' } |
        Sort-Object FullName
)
$lwjglJar = if ($lwjglCandidates.Count -gt 0) { $lwjglCandidates[0].FullName } else { $null }

if (-not (Test-Path $forgeJar)) { throw "Forge universal jar missing: $forgeJar. Run the legacy Forge setup first." }
if (-not (Test-Path $launchwrapperJar)) { throw "LaunchWrapper 1.12 missing: $launchwrapperJar." }
if (-not (Test-Path $asmJar)) { throw "ASM 5.2 missing: $asmJar." }
if (-not $lwjglJar -or -not (Test-Path $lwjglJar)) {
    throw "LWJGL 2.9.4 artifact missing under $lwjglRoot."
}

$javaHome = Resolve-JavaHome
$javac = Join-Path $javaHome "bin\javac.exe"
$jarExe = Join-Path $javaHome "bin\jar.exe"
if (-not (Test-Path $javac)) { throw "javac missing from Java home: $javaHome" }
if (-not (Test-Path $jarExe)) { throw "jar.exe missing from Java home: $javaHome" }
$javaExe = Join-Path $javaHome "bin\java.exe"
if (-not (Test-Path $javaExe)) { throw "java.exe missing from Java home: $javaHome" }


$buildRoot = Join-Path (Get-ConfigPath "BuildDir") "legacy_forge_compat\$MinecraftVersion-$ForgeVersion"
$classesDir = Join-Path $buildRoot "classes"
$jarPath = Join-Path $buildRoot "banditvault-legacy-forge-compat.jar"
$stampPath = Join-Path $buildRoot "build.stamp"
$sourceDir = Join-Path $PSScriptRoot "src\main\java"
$resourceDir = Join-Path $PSScriptRoot "src\main\resources"
$sources = @(Get-ChildItem $sourceDir -Recurse -Filter "*.java" | Select-Object -ExpandProperty FullName)
if (-not $sources) { throw "No legacy Forge compat Java sources found." }
$resourceFiles = @(Get-ChildItem $resourceDir -Recurse -File | Select-Object -ExpandProperty FullName)

$stampValues = @("legacy_forge_compat", $MinecraftVersion, $ForgeVersion, $javaHome)
$stamp = New-BuildStamp -Values $stampValues -ContentFiles (@($PSCommandPath) + $sources + $resourceFiles) -DependencyFiles @($forgeJar, $launchwrapperJar, $asmJar, $lwjglJar)

if (-not (Test-BuildStampCurrent -StampPath $stampPath -Stamp $stamp -RequiredOutputs @($jarPath))) {
    Remove-Item -Recurse -Force $buildRoot -ErrorAction SilentlyContinue
    Ensure-Dir $classesDir

    $cp = @($forgeJar, $launchwrapperJar, $asmJar) -join ";"
    & $javac --release 8 -cp $cp -d $classesDir $sources
    if ($LASTEXITCODE -ne 0) { throw "Legacy Forge compat coremod compile failed." }

    Copy-Item -Recurse "$resourceDir\*" $classesDir -Force
    $manifestPath = Join-Path $resourceDir "META-INF\MANIFEST.MF"
    Remove-Item -LiteralPath (Join-Path $classesDir "META-INF\MANIFEST.MF") -Force -ErrorAction SilentlyContinue
    & $jarExe cfm $jarPath $manifestPath -C $classesDir .
    if ($LASTEXITCODE -ne 0) { throw "Legacy Forge compat coremod JAR creation failed." }
    Set-BuildStamp -StampPath $stampPath -Stamp $stamp
}

# LaunchWrapper keeps org.lwjgl.* delegated to the parent classloader, so the
# IClassTransformer cannot reliably see WindowsDisplay. Patch the actual LWJGL
# 2.9.4 jar in-place; this keeps all other Minecraft versions untouched.
& $javaExe -cp "$classesDir;$asmJar;$launchwrapperJar" banditvault.legacyforge.LegacyLwjglJarPatcher $lwjglJar
if ($LASTEXITCODE -ne 0) { throw "LWJGL 2.9.4 UWP WindowsDisplay patch failed." }

# Keep the stock Maven artifact immutable for the download manifest. The patched
# compatibility copy is packaged separately and selected only by the legacy
# 1.12.2 Forge launcher path at runtime.
$packageDir = Get-ConfigPath "PackageContentDir"
$legacyLwjglPackageDir = Join-Path $packageDir "runtimelegacy-forge"
$legacyLwjglPackageJar = Join-Path $legacyLwjglPackageDir "lwjgl-2.9.4-uwp.jar"
Ensure-Dir $legacyLwjglPackageDir
Copy-Item -LiteralPath $lwjglJar -Destination $legacyLwjglPackageJar -Force
Write-Host "Packaged legacy LWJGL compatibility jar: $legacyLwjglPackageJar"

Ensure-Dir $OutputDir
$targetJar = Join-Path $OutputDir "banditvault-legacy-forge-compat.jar"
Copy-Item -LiteralPath $jarPath -Destination $targetJar -Force
Write-Host "Legacy Forge 1.12.2 ZipFS coremod: $targetJar"
