# Bytecode verifier and selected-build regression tests; never starts Minecraft.
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$root = Resolve-RepoRoot
$outDir = Join-Path (Get-ConfigPath "BuildDir") "neoforge_native_guard_tests"
$classes = Join-Path $outDir "classes"
Ensure-Dir $classes

function Assert-Rejected {
    param([scriptblock]$Action)
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    if (-not $rejected) { throw "Invalid selected-build input was accepted" }
}

$catalog = @(Import-Csv (Join-Path $root $ProjectConfig.VersionCatalog) -Delimiter "`t")
$target = Select-NeoForge1211Target -Catalog $catalog
Assert-Rejected { Select-NeoForge1211Target -Catalog @() }
Assert-Rejected { Select-NeoForge1211Target -Catalog @($target, $target) }
Assert-Rejected { Select-NeoForge1211Target -Catalog @($catalog | Where-Object { $_.loader -eq "fabric" }) }
$wrongRuntime = [pscustomobject]@{
    minecraftVersion = "1.21.1"; loader = "neoforge"; loaderVersion = $target.loaderVersion
    javaRuntime = "current"; controllerProvider = "neoforge"
}
Assert-Rejected { Select-NeoForge1211Target -Catalog @($wrongRuntime) }

# Check the final package audit against a synthetic directory, not the real staging
# package. These placeholder files are never packaged or executed.
$fixture = Join-Path $outDir ("package-test-" + [Guid]::NewGuid().ToString("N"))
$targetId = "1.21.1-neoforge-$($target.loaderVersion)"
try {
    Ensure-Dir (Join-Path $fixture "runtime\bundled-mods")
    foreach ($relative in @(
        "runtime\manifests\$targetId.tsv", "jre21\bin\server\jvm.dll", "jre21\conf\security\java.security",
        "java-base-uwp-filesystem-21.jar", "java-zipfs-realpath-21.jar", "java-desktop-uwp-awt-21.jar",
        "securejarhandler-uwp-patch.jar", "natives\lwjgl.dll", "natives\glfw.dll", "natives\jnidispatch.dll",
        "runtime\version-mods\$targetId\banditvault-neoforge-controller-1.0.0.jar"
    )) {
        $file = Join-Path $fixture $relative
        Ensure-Dir (Split-Path $file -Parent)
        [IO.File]::WriteAllText($file, "test fixture - not an executable")
    }
    $target | Export-Csv -Path (Join-Path $fixture "runtime\version_catalog.tsv") -Delimiter "`t" -NoTypeInformation
    Assert-NeoForge1211Package -PackageDir $fixture -Target $target
    $stale = Join-Path $fixture "runtime\bundled-mods\old-fabric.jar"
    [IO.File]::WriteAllText($stale, "test fixture")
    Assert-Rejected { Assert-NeoForge1211Package -PackageDir $fixture -Target $target }
    Remove-Item -LiteralPath $stale
    Ensure-Dir (Join-Path $fixture "jre17")
    Assert-Rejected { Assert-NeoForge1211Package -PackageDir $fixture -Target $target }
    Remove-Item -LiteralPath (Join-Path $fixture "jre17")
    Remove-Item -LiteralPath (Join-Path $fixture "natives\jnidispatch.dll")
    Assert-Rejected { Assert-NeoForge1211Package -PackageDir $fixture -Target $target }
} finally {
    if (Test-Path $fixture) { Remove-Item -LiteralPath $fixture -Recurse -Force }
}

$javaHome = Resolve-JavaHomeExact -MajorVersion 21
$asm = Resolve-UwpGuardAsmJar
$sources = @(
    (Join-Path $root "patch\securejarhandler\cpw\mods\cl\UwpNativeGuards.java"),
    (Join-Path $root "patch\tests\cpw\mods\cl\UwpNativeGuardsTests.java")
)
& (Join-Path $javaHome "bin\javac.exe") --release 21 -proc:none -cp $asm -d $classes $sources
if ($LASTEXITCODE -ne 0) { throw "NeoForge native guard test compile failed" }
& (Join-Path $javaHome "bin\java.exe") -Xverify:all -ea -cp "$classes;$asm" cpw.mods.cl.UwpNativeGuardsTests
if ($LASTEXITCODE -ne 0) { throw "NeoForge native guard bytecode tests failed" }
Write-Host "NEOFORGE_1211_BUILD_TESTS_OK"
