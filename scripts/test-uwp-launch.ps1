# Small regression suite; does not start Minecraft, sign in, or package an APPX.
param([switch]$NeoForgeOnly)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$root = Resolve-RepoRoot
$outDir = Join-Path (Get-ConfigPath "BuildDir") "launch_tests"
Ensure-Dir $outDir

$tools = Resolve-VSTools
$sdk = Resolve-WindowsSdk
$env:INCLUDE = "$($tools.MsvcRoot)\include;$($sdk.Root)Include\$($sdk.Version)\ucrt;$($sdk.Root)Include\$($sdk.Version)\shared;$($sdk.Root)Include\$($sdk.Version)\um"
$env:LIB = "$($tools.MsvcRoot)\lib\x64;$($sdk.Root)Lib\$($sdk.Version)\ucrt\x64;$($sdk.Root)Lib\$($sdk.Version)\um\x64"
$exe = Join-Path $outDir "launch_policy_tests.exe"
& $tools.ClExe (Join-Path $root "MC.Xbox\launch\tests\launch_policy_tests.cpp") `
    /std:c++17 /EHsc /W4 /Od /Fo"$outDir\" /Fd"$outDir\" /Fe"$exe"
if ($LASTEXITCODE -ne 0) { throw "Launch policy test compile failed" }
& $exe
if ($LASTEXITCODE -ne 0) { throw "Launch policy tests failed" }

if ($NeoForgeOnly) {
    & (Join-Path $PSScriptRoot "test-neoforge-native-guards.ps1")
    Write-Host "UWP_NEOFORGE_LAUNCH_TESTS_OK"
    return
}

$javaHome = Resolve-JavaHome
$javac = Join-Path $javaHome "bin\javac.exe"
$java = Join-Path $javaHome "bin\java.exe"
$mixinJar = Resolve-SpongeMixinJar -GameDir (Get-ConfigPath "GameDir") `
    -MinecraftVersion $ProjectConfig.MinecraftVersion -LoaderVersion $ProjectConfig.FabricLoaderVersion
$sourceRoot = Join-Path $root "compat_mod\src\main\java\banditvault\xboxcompat"
$guardNames = @(
    "ControlifyGlfwMixin", "ControlifyHidMixin", "ControlifyLegacySdlMixin",
    "ControlifySdlMixin", "OshiGraphicsCardMixin", "SodiumGraphicsAdapterMixin"
)
$sources = @((Join-Path $sourceRoot "UwpRuntime.java"), (Join-Path $sourceRoot "XboxCompatLog.java"))
$sources += @($guardNames | ForEach-Object { Join-Path $sourceRoot "mixin\$_.java" })
$sources += Join-Path $root "compat_mod\src\test\java\banditvault\xboxcompat\NativeGuardTests.java"

# Both resource lists must include every optional native guard (including 26.x builds).
foreach ($path in @(
    "compat_mod\src\main\resources\banditvault-xbox-compat.mixins.json",
    "compat_mod\src\variants\26.2\resources\banditvault-xbox-compat.mixins.json"
)) {
    $mixins = Get-Content -Raw (Join-Path $root $path) | ConvertFrom-Json
    foreach ($guard in $guardNames) {
        if (@($mixins.client | Where-Object { $_ -eq $guard }).Count -ne 1) {
            throw "Native guard missing or duplicated in ${path}: $guard"
        }
    }
}

$classesDir = Join-Path $outDir "classes"
Ensure-Dir $classesDir
& $javac --release 8 -proc:none -cp $mixinJar -d $classesDir $sources
if ($LASTEXITCODE -ne 0) { throw "Native guard test compile failed" }
Push-Location $outDir
try {
    & $java -ea -cp "$classesDir;$mixinJar" banditvault.xboxcompat.NativeGuardTests
    if ($LASTEXITCODE -ne 0) { throw "Native guard tests failed" }
} finally {
    Pop-Location
}
Write-Host "UWP_LAUNCH_TESTS_OK"
