# Generates MC.Xbox/runtime_config.h from the template and scripts/config.ps1.
#
# build.ps1 calls this before compiling MC.Xbox.exe and tools/ui-preview/render.ps1
# calls it so preview builds work without a full APPX build. Keeping the token
# substitution in one place stops the launcher, the preview and the packaged
# build from drifting apart.
param(
    [string]$OutputPath,
    [string]$ReleaseChannel = ""
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$root = Resolve-RepoRoot
if (-not $OutputPath) {
    $OutputPath = Join-Path $root "staging\build\MC.Xbox\runtime_config.h"
}

$template = Join-Path $root "MC.Xbox\runtime_config.h.in"
if (-not (Test-Path -LiteralPath $template)) {
    throw "runtime_config.h.in not found at $template"
}

$values = [ordered]@{
    '@@MC_VERSION@@'                 = $ProjectConfig.MinecraftVersion
    '@@FABRIC_LOADER_VERSION@@'      = $ProjectConfig.FabricLoaderVersion
    '@@MC_ASSET_INDEX@@'             = $ProjectConfig.MinecraftAssetIndex
    '@@DEFAULT_LOADER@@'             = $ProjectConfig.DefaultLoader
    '@@DEFAULT_LOADER_VERSION@@'     = $ProjectConfig.DefaultLoaderVersion
    '@@DEFAULT_LAUNCH_VERSION@@'     = $ProjectConfig.DefaultLaunchVersion
    '@@DEFAULT_CONTROLLER_PROVIDER@@' = $ProjectConfig.DefaultControllerProvider
    '@@DEFAULT_JAVA_RUNTIME@@'       = $ProjectConfig.DefaultJavaRuntime
    '@@DEFAULT_SUPPORT_LEVEL@@'      = $ProjectConfig.DefaultSupportLevel
    '@@RELEASE_CHANNEL@@'            = $ReleaseChannel
}

$content = [System.IO.File]::ReadAllText($template)
foreach ($entry in $values.GetEnumerator()) {
    $content = $content.Replace($entry.Key, [string]$entry.Value)
}
if ($content -match '@@[A-Z_]+@@') {
    throw "runtime_config.h still contains unsubstituted tokens after generation: $($Matches[0])"
}

Ensure-Dir (Split-Path $OutputPath -Parent)
[System.IO.File]::WriteAllText($OutputPath, $content)
Write-Host "runtime_config.h written to $OutputPath (default $($ProjectConfig.DefaultLoader)-$($ProjectConfig.DefaultLoaderVersion))"
