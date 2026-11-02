param([string]$CatalogPath, [string]$PackagePath, [string]$AppxPath)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$root = Resolve-RepoRoot
if (-not $CatalogPath) { $CatalogPath = Join-Path $root $ProjectConfig.VersionCatalog }
if (-not $PackagePath) { $PackagePath = Get-ConfigPath "PackageContentDir" }
$targets = @(Import-Csv $CatalogPath -Delimiter "`t" | Where-Object { $_.loaderVersion -and $_.loaderVersion -ne 'selected' -and $_.loaderVersion -ne 'none' })
$bundleConfigPath = Join-Path $root 'config/controlify-bundle.json'
if (-not (Test-Path -LiteralPath $bundleConfigPath)) {
    throw "Controlify bundle config not found at $bundleConfigPath"
}
$bundleConfig = Get-Content -LiteralPath $bundleConfigPath -Raw | ConvertFrom-Json
$bundleMods = @($bundleConfig.mods)
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = $null
if ($AppxPath) { $zip = [IO.Compression.ZipFile]::OpenRead($AppxPath) }
try {
    function Require-Entry([string]$Relative) {
        if ($zip) {
            $entry = $zip.GetEntry($Relative.Replace('\', '/'))
            if (-not $entry -or $entry.Length -eq 0) { throw "Package entry missing or empty $Relative" }
        } else {
            $path = Join-Path $PackagePath $Relative
            if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -eq 0) {
                throw "Package entry missing or empty $Relative"
            }
            if ($Relative.EndsWith('.jar') -and -not (Test-JarComplete $path)) { throw "Invalid packaged jar $Relative" }
        }
    }
    function Forbid-ControllerJars {
        if ($zip) {
            $bad = @($zip.Entries | Where-Object { $_.FullName -match 'banditvault-(fabric|forge|neoforge|cleanroom)-controller-.*\.jar$' })
            if ($bad.Count -gt 0) { throw "Bandit controller jar still packaged: $($bad[0].FullName)" }
        } else {
            $runtimePath = Join-Path $PackagePath 'runtime'
            $bad = @(Get-ChildItem -LiteralPath $runtimePath -Recurse -File -Filter 'banditvault-*-controller-*.jar' -ErrorAction SilentlyContinue)
            if ($bad.Count -gt 0) { throw "Bandit controller jar still packaged: $($bad[0].FullName)" }
        }
    }
    Require-Entry "runtime\version_catalog.tsv"
    Require-Entry "runtime\recommended-mods.json"
    foreach ($inputFile in @(
        @{ Source = $CatalogPath; Relative = 'runtime/version_catalog.tsv' },
        @{ Source = (Join-Path $root 'config/recommended-mods.json'); Relative = 'runtime/recommended-mods.json' }
    )) {
        if ($zip) {
            $stream = $zip.GetEntry($inputFile.Relative).Open()
            $hasher = [Security.Cryptography.SHA256]::Create()
            try { $packagedHash = [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-', '') }
            finally { $stream.Dispose(); $hasher.Dispose() }
        } else {
            $packagedHash = (Get-FileHash -LiteralPath (Join-Path $PackagePath $inputFile.Relative)).Hash
        }
        if ($packagedHash -ne (Get-FileHash -LiteralPath $inputFile.Source).Hash) {
            throw "Packaged input does not match source $($inputFile.Relative)"
        }
    }
    foreach ($target in $targets) {
        $id = "$($target.minecraftVersion)-$($target.loader)-$($target.loaderVersion)"
        Require-Entry "runtime\manifests\$id.tsv"
        if ($target.loader -eq 'fabric') {
            Require-Entry "runtime\libraries\net\fabricmc\fabric-loader\$($target.loaderVersion)\fabric-loader-$($target.loaderVersion).jar"
            Require-Entry "runtime\version-mods\$id\banditvault-xbox-compat-1.0.0.jar"
        }
        if ($target.controllerProvider -ne 'none') {
            throw "Catalog row $id still advertises a bundled Bandit controller provider '$($target.controllerProvider)'"
        }
        $runtimeDir = switch ($target.javaRuntime) { 'java17' { 'jre17' } 'java21' { 'jre21' } 'legacy' { 'jre8' } default { 'jre' } }
        Require-Entry "$runtimeDir\bin\server\jvm.dll"
        $suffix = switch ($runtimeDir) { 'jre17' { '-17' } 'jre21' { '-21' } 'jre8' { '-8' } default { '' } }
        foreach ($base in @('java-base-uwp-filesystem','java-zipfs-realpath','java-desktop-uwp-awt')) {
            Require-Entry "$base$suffix.jar"
        }
    }
    Forbid-ControllerJars
    foreach ($mod in $bundleMods) {
        Require-Entry "runtime\bundled-mods\$($mod.name)"
        Require-Entry "runtime\version-mods\$($bundleConfig.targetId)\$($mod.name)"
    }
    Write-Host "Package validation passed $($targets.Count) targets and $($bundleMods.Count) bundled Controlify mod(s)"
} finally { if ($zip) { $zip.Dispose() } }
