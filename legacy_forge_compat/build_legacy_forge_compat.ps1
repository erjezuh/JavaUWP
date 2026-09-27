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

if (-not (Test-Path $forgeJar)) { throw "Forge universal jar missing: $forgeJar. Run the legacy Forge setup first." }
if (-not (Test-Path $launchwrapperJar)) { throw "LaunchWrapper 1.12 missing: $launchwrapperJar." }
if (-not (Test-Path $asmJar)) { throw "ASM 5.2 missing: $asmJar." }

$javaHome = Resolve-JavaHomeForMinecraft -MinecraftVersion $MinecraftVersion
$javac = Join-Path $javaHome "bin\javac.exe"
$jarExe = Join-Path $javaHome "bin\jar.exe"
if (-not (Test-Path $javac)) { throw "javac missing from Java home: $javaHome" }
if (-not (Test-Path $jarExe)) { throw "jar.exe missing from Java home: $javaHome" }

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
$stamp = New-BuildStamp -Values $stampValues -ContentFiles (@($PSCommandPath) + $sources + $resourceFiles) -DependencyFiles @($forgeJar, $launchwrapperJar, $asmJar)

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

Ensure-Dir $OutputDir
$targetJar = Join-Path $OutputDir "banditvault-legacy-forge-compat.jar"
Copy-Item -LiteralPath $jarPath -Destination $targetJar -Force
Write-Host "Legacy Forge 1.12.2 ZipFS coremod: $targetJar"
