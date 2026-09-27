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
$jnaDir = Join-Path $gameDir "libraries\net\java\dev\jna\jna\4.4.0"
$jnaJar = Join-Path $jnaDir "jna-4.4.0.jar"

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
if (-not (Test-Path $jnaJar)) { throw "JNA 4.4.0 missing: $jnaJar." }
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

$packageDir = Get-ConfigPath "PackageContentDir"
$legacyLwjglPackageDir = Join-Path $packageDir "runtime\legacy-forge"
$legacyWindowsDisplayClass = Join-Path $legacyLwjglPackageDir "org\lwjgl\opengl\WindowsDisplay.class"
$legacyWindowsDisplayPeerInfoClass = Join-Path $legacyLwjglPackageDir "org\lwjgl\opengl\WindowsDisplayPeerInfo.class"
$legacyWindowsContextImplClass = Join-Path $legacyLwjglPackageDir "org\lwjgl\opengl\WindowsContextImplementation.class"
$legacyBridgeClass = Join-Path $legacyLwjglPackageDir "banditvault\legacyforge\LegacyUwpGlfwBridge.class"
$legacyLaunchClassLoaderClass = Join-Path $legacyLwjglPackageDir "net\minecraft\launchwrapper\LaunchClassLoader.class"
Ensure-Dir $legacyLwjglPackageDir

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
$stamp = New-BuildStamp -Values $stampValues -ContentFiles (@($PSCommandPath) + $sources + $resourceFiles) -DependencyFiles @($forgeJar, $launchwrapperJar, $asmJar, $jnaJar, $lwjglJar)

if (-not (Test-BuildStampCurrent -StampPath $stampPath -Stamp $stamp -RequiredOutputs @($jarPath))) {
    Remove-Item -Recurse -Force $buildRoot -ErrorAction SilentlyContinue
    Ensure-Dir $classesDir

    $cp = @($forgeJar, $launchwrapperJar, $asmJar, $jnaJar, $lwjglJar) -join ";"
    & $javac --release 8 -cp $cp -d $classesDir $sources
    if ($LASTEXITCODE -ne 0) { throw "Legacy Forge compat coremod compile failed." }

    Copy-Item -Recurse "$resourceDir\*" $classesDir -Force
    $manifestPath = Join-Path $resourceDir "META-INF\MANIFEST.MF"
    Remove-Item -LiteralPath (Join-Path $classesDir "META-INF\MANIFEST.MF") -Force -ErrorAction SilentlyContinue
    & $jarExe cfm $jarPath $manifestPath -C $classesDir .
    if ($LASTEXITCODE -ne 0) { throw "Legacy Forge compat coremod JAR creation failed." }
    Set-BuildStamp -StampPath $stampPath -Stamp $stamp
}

# Patch LaunchClassLoader itself so the org.lwjgl. class-loader exclusion is gone
# from the moment the loader is constructed. This runs before Forge coremod
# injection and avoids racing any early LWJGL reference.
& $javaExe -cp "$classesDir;$asmJar;$launchwrapperJar" banditvault.legacyforge.LegacyLaunchClassLoaderPatcher $launchwrapperJar $legacyLaunchClassLoaderClass
if ($LASTEXITCODE -ne 0) { throw "Legacy LaunchClassLoader patch failed." }

# The legacy UWP implementation replaces LWJGL's Win32 display/context classes
# from launcher-overrides. Keep the stock Maven LWJGL artifact untouched.
$overrideClasses = @(
    @{ Source = (Join-Path $classesDir "org\lwjgl\opengl\WindowsDisplay.class"); Destination = $legacyWindowsDisplayClass },
    @{ Source = (Join-Path $classesDir "org\lwjgl\opengl\WindowsDisplayPeerInfo.class"); Destination = $legacyWindowsDisplayPeerInfoClass },
    @{ Source = (Join-Path $classesDir "org\lwjgl\opengl\WindowsContextImplementation.class"); Destination = $legacyWindowsContextImplClass },
    @{ Source = (Join-Path $classesDir "banditvault\legacyforge\LegacyUwpGlfwBridge.class"); Destination = $legacyBridgeClass }
)
foreach ($item in $overrideClasses) {
    if (-not (Test-Path $item.Source)) {
        throw "Legacy UWP override class missing after compile: $($item.Source)"
    }
    Ensure-Dir (Split-Path $item.Destination -Parent)
    Copy-Item -LiteralPath $item.Source -Destination $item.Destination -Force
    Write-Host "Packaged legacy override class: $($item.Destination)"
}
$bridgeInnerClasses = @(Get-ChildItem $classesDir -Filter 'LegacyUwpGlfwBridge*.class' -Recurse -ErrorAction SilentlyContinue)
foreach ($inner in $bridgeInnerClasses) {
    $relative = $inner.FullName.Substring($classesDir.Length).TrimStart('\','/')
    $destination = Join-Path $legacyLwjglPackageDir $relative
    Ensure-Dir (Split-Path $destination -Parent)
    Copy-Item -LiteralPath $inner.FullName -Destination $destination -Force
    Write-Host "Packaged legacy bridge helper: $destination"
}


# Patch LaunchClassLoader itself so the org.lwjgl. class-loader exclusion is gone
# from the moment the loader is constructed. This runs before Forge coremod
# injection and avoids racing any early LWJGL reference.
& $javaExe -cp "$classesDir;$asmJar;$launchwrapperJar" banditvault.legacyforge.LegacyLaunchClassLoaderPatcher $launchwrapperJar $legacyLaunchClassLoaderClass
if ($LASTEXITCODE -ne 0) { throw "Legacy LaunchClassLoader patch failed." }

# Keep an unmodified copy of the stock LWJGL artifact for diagnostics/rollback.
# The class-level UWP overrides above are what the legacy runtime actually uses.
Write-Host "Packaged legacy WindowsDisplay class: $legacyWindowsDisplayClass"
Write-Host "Packaged legacy WindowsDisplayPeerInfo class: $legacyWindowsDisplayPeerInfoClass"
Write-Host "Packaged legacy WindowsContextImplementation class: $legacyWindowsContextImplClass"
Write-Host "Packaged legacy UWP GLFW bridge: $legacyBridgeClass"
Write-Host "Packaged patched LaunchClassLoader class: $legacyLaunchClassLoaderClass"

Ensure-Dir $OutputDir
$targetJar = Join-Path $OutputDir "banditvault-legacy-forge-compat.jar"
Copy-Item -LiteralPath $jarPath -Destination $targetJar -Force
Write-Host "Legacy Forge 1.12.2 ZipFS coremod: $targetJar"
