# build.ps1 - Build and package MC Java UWP
param(
    [string]$MesaRuntimeDir = $env:MESA_UWP_DIR,
    [string]$McVersion,
    [string]$FabricLoader,
    [string]$AssetIndex,
    # Default launch target for this package. Anything in config/versions.tsv can
    # be selected; this fork ships with NeoForge 1.21.1 as the default.
    [string]$DefaultLoader,
    [string]$DefaultLoaderVersion,
    [string]$DefaultLaunchVersion,
    [string]$AppxVersion = $env:APPX_VERSION,
    [ValidateSet("", "nightly", "stable", "nightly-native")]
    [string]$ReleaseChannel = "",
    [switch]$KeepStaging,
    [switch]$SkipStopAppProcesses,
    [switch]$StopFileLockers,
    [switch]$SkipVersionManifests,
    [switch]$SkipVersionCompat,
    [switch]$IncludePrebuiltNeoForgeArtifacts,
    [switch]$StrictTargets
)

$ErrorActionPreference = "Stop"

$buildLockDirectory = Join-Path $PSScriptRoot ".local"
New-Item -ItemType Directory -Path $buildLockDirectory -Force | Out-Null
try {
    $buildLock = [System.IO.File]::Open(
        (Join-Path $buildLockDirectory "build.lock"),
        [System.IO.FileMode]::OpenOrCreate,
        [System.IO.FileAccess]::ReadWrite,
        [System.IO.FileShare]::None)
} catch [System.IO.IOException] {
    throw "Another Bandit Launcher build is already using this checkout."
}

try {

$script:BuildFailures = @()
function Add-BuildFailure {
    param(
        [Parameter(Mandatory = $true)][string]$Stage,
        [Parameter(Mandatory = $true)][string]$Target,
        [Parameter(Mandatory = $true)][string]$Reason
    )
    $script:BuildFailures += [pscustomobject]@{ Stage = $Stage; Target = $Target; Reason = $Reason }
    Write-Warning "${Stage} skipped for ${Target}: $Reason"
}

# capture inherited overrides before applying command line values
$inheritedMcVersion     = $env:MC_VERSION
$inheritedFabricLoader  = $env:FABRIC_LOADER_VERSION
$inheritedAssetIndex    = $env:MC_ASSET_INDEX

# Push command-line overrides into the environment before sourcing config.
# scripts/config.ps1 honors these so every downstream script (compat mod,
# patch-fabric, etc.) sees the same chosen version and default target.
if ($McVersion)           { $env:MC_VERSION = $McVersion }
if ($FabricLoader)        { $env:FABRIC_LOADER_VERSION = $FabricLoader }
if ($AssetIndex)          { $env:MC_ASSET_INDEX = $AssetIndex }
if ($DefaultLoader)       { $env:DEFAULT_LOADER = $DefaultLoader.ToLowerInvariant() }
if ($DefaultLoaderVersion) { $env:DEFAULT_LOADER_VERSION = $DefaultLoaderVersion }
if ($DefaultLaunchVersion) { $env:DEFAULT_LAUNCH_VERSION = $DefaultLaunchVersion }

. (Join-Path $PSScriptRoot "scripts\common.ps1")

# The compiled default and the build-time Minecraft version must describe the
# same target, otherwise download_manifest.tsv and the launcher disagree.
if ($ProjectConfig.DefaultMinecraftVersion -ne $ProjectConfig.MinecraftVersion) {
    throw "Default target MC mismatch: DEFAULT_MC_VERSION=$($ProjectConfig.DefaultMinecraftVersion) but MC_VERSION=$($ProjectConfig.MinecraftVersion). Use -McVersion / MC_VERSION for both."
}

# Phase timing: "Build native APPX" used to be one opaque 20-minute CI step.
# The per-target work below is stamp-driven, so knowing which phase is slow is
# enough to tell a cold cache from a real regression.
$script:BuildTotalWatch = [System.Diagnostics.Stopwatch]::StartNew()
$script:BuildPhaseWatch = [System.Diagnostics.Stopwatch]::StartNew()
function Write-Phase {
    param([Parameter(Mandatory = $true)][string]$Name)
    $last = $script:BuildPhaseWatch.Elapsed.TotalSeconds
    $script:BuildPhaseWatch.Restart()
    Write-Host ("=== {0} === [last phase {1:N1}s | elapsed {2:N1}s]" -f $Name, $last, $script:BuildTotalWatch.Elapsed.TotalSeconds)
}

Write-Phase "Build preflight"

$root = Resolve-RepoRoot
$pkg = Get-ConfigPath "PackageContentDir"
$buildDir = Get-ConfigPath "BuildDir"
$outDir = Get-ConfigPath "OutputDir"
$gameDir = Get-ConfigPath "GameDir"
$nativesSourceDir = Get-ConfigPath "NativesDir"
$certDir = Get-ConfigPath "CertificateDir"
$mcBuildDir = Join-Path $buildDir "MC.Xbox"
$glfwBuildDir = Join-Path $buildDir "glfw_shim"
$mcExe = Join-Path $mcBuildDir "MC.Xbox.exe"
$shimDll = Join-Path $glfwBuildDir "glfw.dll"
$currentJavaMajor = (@(Import-Csv (Join-Path $root $ProjectConfig.VersionCatalog) -Delimiter "`t" | Where-Object { $_.javaRuntime -eq "current" } | ForEach-Object { Get-MinecraftJavaMajorVersion $_.minecraftVersion }) | Measure-Object -Maximum).Maximum
$jreSrc = Resolve-JavaHome -MajorVersion ([Math]::Max([int]$currentJavaMajor, [int]$ProjectConfig.JavaRelease))
$jre21Src = Resolve-JavaHomeExact -MajorVersion 21
$jarExe = Join-Path $jreSrc "bin\jar.exe"
if (-not (Test-Path $jarExe)) { $jarExe = "jar" }
$tools = Resolve-VSTools
$sdk = Resolve-WindowsSdk
$sdkRoot = $sdk.Root
$sdkVer = $sdk.Version
# some sdk installs ship makeappx and signtool only under an older bin version
$sdkToolsFallbackVer = "10.0.26100.0"

function Assert-AppxVersion {
    param([Parameter(Mandatory = $true)][string]$Version)

    $parts = $Version -split '\.'
    if ($parts.Count -ne 4) {
        throw "APPX version must have four numeric fields: $Version"
    }
    foreach ($part in $parts) {
        $value = 0
        if (-not [int]::TryParse($part, [ref]$value) -or $value -lt 0 -or $value -gt 65535) {
            throw "APPX version field is out of range 0..65535: $Version"
        }
    }
}

$manifestSourcePath = Join-Path $root "MC.Xbox\Package.appxmanifest"
[xml]$sourceManifest = Get-Content $manifestSourcePath
$baseVersionParts = ([string]$sourceManifest.Package.Identity.Version) -split '\.'
if ($baseVersionParts.Count -ne 4) {
    throw "Package.appxmanifest Identity Version must have four numeric fields."
}
$appVersionBase = "$($baseVersionParts[0]).$($baseVersionParts[1]).$($baseVersionParts[2])"

# Auto-increment local builds from the package manifest base version so installs
# update in place while CI can provide an exact APPX_VERSION for nightlies.
$verFile = Join-Path $root ".local\app_build.txt"
$verFileExisted = Test-Path $verFile
if ($AppxVersion) {
    Assert-AppxVersion $AppxVersion
    $appVersion = $AppxVersion
} else {
    $verRev = [int]$baseVersionParts[3]
    if ($verFileExisted) {
        $prevParts = ((Get-Content $verFile -Raw).Trim()) -split '\.'
        $prevBase = if ($prevParts.Count -eq 4) { "$($prevParts[0]).$($prevParts[1]).$($prevParts[2])" } else { "" }
        if ($prevBase -eq $appVersionBase) { $verRev = [int]$prevParts[3] + 1 }
    }
    if ($verRev -gt 65535) { $verRev = 65535 }
    $appVersion = "$appVersionBase.$verRev"
}
Ensure-Dir (Split-Path $verFile)
Set-Content -Path $verFile -Value $appVersion -NoNewline
$appx = Join-Path $outDir ("BanditLauncherNative_{0}.appx" -f $appVersion)

$mcVersionSource = if ($McVersion) {
    "-McVersion"
} elseif ($inheritedMcVersion) {
    "MC_VERSION inherited from this shell"
} else {
    "config.ps1 default"
}
$fabricLoaderSource = if ($FabricLoader) {
    "-FabricLoader"
} elseif ($inheritedFabricLoader) {
    "FABRIC_LOADER_VERSION inherited from this shell"
} else {
    "config.ps1 default"
}
$appVersionSource = if ($AppxVersion) {
    "-AppxVersion"
} elseif ($verFileExisted) {
    ".local\app_build.txt"
} else {
    "fresh worktree, first build off the manifest base"
}

Write-Host ""
Write-Host "Resolved build target:"
Write-Host ("  MC version      {0}  ({1})" -f $ProjectConfig.MinecraftVersion, $mcVersionSource)
Write-Host ("  Default target  {0}-{1}-{2}" -f $ProjectConfig.DefaultMinecraftVersion, $ProjectConfig.DefaultLoader, $ProjectConfig.DefaultLoaderVersion)
Write-Host ("  Launch version  {0}" -f $ProjectConfig.DefaultLaunchVersion)
Write-Host ("  Fabric loader   {0}  ({1})" -f $ProjectConfig.FabricLoaderVersion, $fabricLoaderSource)
Write-Host ("  Asset index     {0}" -f $ProjectConfig.MinecraftAssetIndex)
Write-Host ("  Appx version    {0}  ({1})" -f $appVersion, $appVersionSource)
Write-Host ""

foreach ($inherited in @(
    @{ Name = "MC_VERSION";            Value = $inheritedMcVersion;    Param = $McVersion },
    @{ Name = "FABRIC_LOADER_VERSION"; Value = $inheritedFabricLoader; Param = $FabricLoader },
    @{ Name = "MC_ASSET_INDEX";        Value = $inheritedAssetIndex;   Param = $AssetIndex })) {
    if ($inherited.Value -and -not $inherited.Param) {
        Write-Warning ("{0}={1} was already set in this shell and is retargeting this build. Clear it with `$env:{0} = `$null, or open a new terminal." -f $inherited.Name, $inherited.Value)
    }
}

if (-not $AppxVersion -and -not $verFileExisted) {
    Write-Warning "No .local\app_build.txt in this worktree, so the appx stamps $appVersion. Anything already installed on the console at a higher version will not be replaced by it."
}

function Stop-BuildBlockingProcesses {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PackageName,

        [Parameter(Mandatory = $true)]
        [string]$RootPath,

        [Parameter(Mandatory = $true)]
        [string]$PackageContentPath,

        [Parameter(Mandatory = $true)]
        [string]$OutputPath,

        [string[]]$LockPaths = @()
    )

    $rootMatch = $RootPath.Replace('\', '\\')
    $pkgMatch = $PackageContentPath.Replace('\', '\\')
    $outMatch = $OutputPath.Replace('\', '\\')

    function Get-RestartManagerLockingProcessIds {
        param(
            [Parameter(Mandatory = $true)]
            [string[]]$Paths
        )

        $existingPaths = @($Paths | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | ForEach-Object { (Resolve-Path -LiteralPath $_).Path })
        if (-not $existingPaths) { return @() }

        if (-not ("BuildRestartManager" -as [type])) {
            Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class BuildRestartManager
{
    const int CCH_RM_SESSION_KEY = 32;
    const int ERROR_MORE_DATA = 234;

    [StructLayout(LayoutKind.Sequential)]
    struct FILETIME
    {
        public uint dwLowDateTime;
        public uint dwHighDateTime;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct RM_UNIQUE_PROCESS
    {
        public int dwProcessId;
        public FILETIME ProcessStartTime;
    }

    enum RM_APP_TYPE
    {
        RmUnknownApp = 0,
        RmMainWindow = 1,
        RmOtherWindow = 2,
        RmService = 3,
        RmExplorer = 4,
        RmConsole = 5,
        RmCritical = 1000
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct RM_PROCESS_INFO
    {
        public RM_UNIQUE_PROCESS Process;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string strAppName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string strServiceShortName;
        public RM_APP_TYPE ApplicationType;
        public uint AppStatus;
        public uint TSSessionId;
        [MarshalAs(UnmanagedType.Bool)]
        public bool bRestartable;
    }

    [DllImport("rstrtmgr.dll", CharSet = CharSet.Unicode)]
    static extern int RmStartSession(out uint pSessionHandle, int dwSessionFlags, StringBuilder strSessionKey);

    [DllImport("rstrtmgr.dll", CharSet = CharSet.Unicode)]
    static extern int RmRegisterResources(uint pSessionHandle, uint nFiles, string[] rgsFilenames, uint nApplications, IntPtr rgApplications, uint nServices, string[] rgsServiceNames);

    [DllImport("rstrtmgr.dll")]
    static extern int RmGetList(uint dwSessionHandle, out uint pnProcInfoNeeded, ref uint pnProcInfo, [In, Out] RM_PROCESS_INFO[] rgAffectedApps, ref uint lpdwRebootReasons);

    [DllImport("rstrtmgr.dll")]
    static extern int RmEndSession(uint pSessionHandle);

    public static int[] GetLockingProcessIds(string[] paths)
    {
        uint handle;
        var key = new StringBuilder(CCH_RM_SESSION_KEY + 1);
        int result = RmStartSession(out handle, 0, key);
        if (result != 0) return new int[0];

        try
        {
            result = RmRegisterResources(handle, (uint)paths.Length, paths, 0, IntPtr.Zero, 0, null);
            if (result != 0) return new int[0];

            uint needed;
            uint count = 0;
            uint reasons = 0;
            result = RmGetList(handle, out needed, ref count, null, ref reasons);
            if (result != ERROR_MORE_DATA || needed == 0) return new int[0];

            count = needed;
            var processes = new RM_PROCESS_INFO[count];
            result = RmGetList(handle, out needed, ref count, processes, ref reasons);
            if (result != 0) return new int[0];

            var ids = new List<int>();
            for (int i = 0; i < count; i++)
            {
                int id = processes[i].Process.dwProcessId;
                if (!ids.Contains(id)) ids.Add(id);
            }
            return ids.ToArray();
        }
        finally
        {
            RmEndSession(handle);
        }
    }
}
"@
        }

        return [BuildRestartManager]::GetLockingProcessIds($existingPaths)
    }

    $allProcesses = @(Get-CimInstance Win32_Process)
    $lockProcessIds = @()
    if ($LockPaths -and $LockPaths.Count -gt 0) {
        Write-Host "Checking file lockers..."
        $lockProcessIds = @(Get-RestartManagerLockingProcessIds -Paths $LockPaths | Where-Object { $_ -and $_ -ne $PID })
    }

    $matches = $allProcesses |
        Where-Object {
            $name = $_.Name
            $path = [string]$_.ExecutablePath
            $cmd = [string]$_.CommandLine
            $matched = $false

            if ($_.ProcessId -ne $PID) {
                if ($lockProcessIds -contains $_.ProcessId) { $matched = $true }
                if ($name -ieq "MC.Xbox.exe") { $matched = $true }
                if ($PackageName -and ($path -like "*$PackageName*" -or $cmd -like "*$PackageName*")) { $matched = $true }

                $isPackagingTool = $name -ieq "makeappx.exe" -or $name -ieq "signtool.exe"
                if ($isPackagingTool -and (
                    $cmd -like "*$RootPath*" -or
                    $cmd -like "*$rootMatch*" -or
                    $cmd -like "*$PackageContentPath*" -or
                    $cmd -like "*$pkgMatch*" -or
                    $cmd -like "*$OutputPath*" -or
                    $cmd -like "*$outMatch*")) {
                    $matched = $true
                }
            }

            $matched
        }

    if (-not $matches) {
        Write-Host "No running app/package processes found."
        return
    }

    Write-Host "Stopping running app/package processes..."
    $restartExplorer = $false
    foreach ($proc in $matches) {
        Write-Host "  Stop PID $($proc.ProcessId) $($proc.Name)"
        if ($proc.Name -ieq "explorer.exe") {
            $restartExplorer = $true
        }
        Stop-Process -Id $proc.ProcessId -Force -ErrorAction SilentlyContinue
    }

    Start-Sleep -Milliseconds 500
    if ($restartExplorer) {
        Start-Process explorer.exe
    }
}

if (-not $SkipStopAppProcesses) {
    $packageName = $sourceManifest.Package.Identity.Name
    $lockPaths = if ($StopFileLockers) { @($appx) } else { @() }
    Stop-BuildBlockingProcesses `
        -PackageName $packageName `
        -RootPath $root `
        -PackageContentPath $pkg `
        -OutputPath $appx `
        -LockPaths $lockPaths
}

Ensure-Dir $buildDir, $outDir, $certDir, $mcBuildDir, $glfwBuildDir

Write-Phase "Generating runtime_config.h"
# Shared generator keeps build.ps1, tools/ui-preview and the packaged header in
# sync (see scripts/gen-runtime-config.ps1).
$runtimeConfigOutput = Join-Path $mcBuildDir "runtime_config.h"
& (Join-Path $root "scripts\gen-runtime-config.ps1") `
    -OutputPath $runtimeConfigOutput `
    -ReleaseChannel $ReleaseChannel

Write-Phase "Building MC.Xbox.exe"
$mcSources = Get-ChildItem (Join-Path $root "MC.Xbox") -Recurse -File -Include *.cpp, *.c, *.h, *.hpp |
    Select-Object -ExpandProperty FullName
$mcStampPath = Join-Path $mcBuildDir "MC.Xbox.stamp"
$mcStamp = New-BuildStamp `
    -Values @("mc_xbox_exe", $tools.ClExe, $sdkVer, $jreSrc, ($CommonClFlags -join " ")) `
    -ContentFiles (@($PSCommandPath, $runtimeConfigOutput) + $mcSources)
$mcNeedsBuild = -not (Test-BuildStampCurrent -StampPath $mcStampPath -Stamp $mcStamp -RequiredOutputs @($mcExe))

if ($mcNeedsBuild) {
    Push-Location (Join-Path $root "MC.Xbox")

    $env:INCLUDE = "$mcBuildDir;$($tools.MsvcRoot)\include;${sdkRoot}Include\$sdkVer\ucrt;${sdkRoot}Include\$sdkVer\shared;${sdkRoot}Include\$sdkVer\um;${sdkRoot}Include\$sdkVer\winrt;${sdkRoot}Include\$sdkVer\cppwinrt;$jreSrc\include;$jreSrc\include\win32"
    $env:LIB = "$($tools.MsvcRoot)\lib\x64;${sdkRoot}Lib\$sdkVer\ucrt\x64;${sdkRoot}Lib\$sdkVer\um\x64"

    & $tools.ClExe App.cpp launch\app_globals.cpp common\launcher_common.cpp common\i18n.cpp common\crash_report.cpp mods\mod_defaults.cpp mods\mod_source.cpp mods\curseforge.cpp mods\manual_downloads.cpp mods\modpack_io.cpp mods\world_io.cpp net\http_client.cpp net\update_check.cpp profiles\profiles.cpp net\html_translate.cpp net\remote_file_server.cpp auth\minecraft_auth.cpp ui\launcher_ui.cpp ui\launcher_mouse.cpp ui\mods_ui_globals.cpp mods\mods_browser.cpp launch\runtime_manager.cpp launch\minecraft_launch.cpp launch\launch_internal.cpp launch\loaders\loader_common.cpp launch\loaders\loader.cpp launch\loaders\fabric.cpp launch\loaders\neoforge.cpp launch\loaders\forge.cpp launch\loaders\cleanroom.cpp telemetry\telemetry.cpp telemetry\crash_fingerprint.cpp telemetry\crash_parse.cpp telemetry\compat_feed.cpp third_party\miniz\miniz.c /std:c++17 /EHsc $CommonClFlags /O2 /GL /Gw /MP /arch:AVX2 /DNDEBUG /D_UNICODE /DUNICODE /D_WIN32_WINNT=0x0A00 /D_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS /DMINIZ_NO_STDIO /DMINIZ_NO_TIME /I. /Icommon /Inet /Iauth /Iui /Imods /Iprofiles /Ilaunch /Ilaunch\loaders /Itelemetry /Fo"$mcBuildDir\" `
        /DWINAPI_FAMILY=WINAPI_FAMILY_APP `
        /link /LTCG /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup /MACHINE:X64 `
        /OUT:"$mcExe" kernel32.lib shell32.lib runtimeobject.lib windowsapp.lib ole32.lib oleaut32.lib d2d1.lib dwrite.lib d3d11.lib dxgi.lib windowscodecs.lib winhttp.lib bcrypt.lib ws2_32.lib
    if ($LASTEXITCODE -ne 0) { throw "Compile failed" }
    Pop-Location
    Set-BuildStamp -StampPath $mcStampPath -Stamp $mcStamp
    Write-Host "MC.Xbox.exe built"
} else {
    Write-Host "MC.Xbox.exe up to date, skipping compile."
}

Write-Phase "Building GLFW CoreWindow shim"
& (Join-Path $root "glfw_shim\build_glfw.ps1") -OutputDir $glfwBuildDir
if (-not (Test-Path $shimDll)) { throw "GLFW shim DLL missing after build: $shimDll" }

Write-Phase "Building Xbox compatibility mod"
& (Join-Path $root "compat_mod\build_compat_mod.ps1")

Write-Phase "Assembling PackageContent"
Remove-Item -Recurse -Force $pkg -ErrorAction SilentlyContinue
Ensure-Dir (Join-Path $pkg "Assets")
Ensure-Dir (Join-Path $pkg "natives")
Ensure-Dir (Join-Path $pkg "graphics\mesa")
# runtime/ holds only launcher-owned or intentionally patched runtime pieces.
# Mojang/Fabric game files are downloaded into LocalState after auth.
Ensure-Dir (Join-Path $pkg "runtime")
Ensure-Dir (Join-Path $pkg "runtime\log_configs")
Ensure-Dir (Join-Path $pkg "runtime\bundled-mods")
Ensure-Dir (Join-Path $pkg "runtime\libraries")

# This fork ships Controlify as its only controller mod. The Bandit controller
# is no longer compiled for any target (51 per-version installer/compile runs
# used ~27 of the 39 minutes of a cold build). Pin the Modrinth version ids,
# SHA1 and byte sizes so every APPX is reproducible and verifiable. YACL is a
# required Controlify dependency and is bundled alongside it.
Write-Phase "Bundling Controlify"
$bundleConfigPath = Join-Path $root "config\controlify-bundle.json"
if (-not (Test-Path -LiteralPath $bundleConfigPath)) {
    throw "Controlify bundle config not found at $bundleConfigPath"
}
$controlifyBundle = Get-Content -LiteralPath $bundleConfigPath -Raw | ConvertFrom-Json
$expectedTargetId = "$($ProjectConfig.DefaultMinecraftVersion)-$($ProjectConfig.DefaultLoader)-$($ProjectConfig.DefaultLoaderVersion)"
if ($controlifyBundle.targetId -ne $expectedTargetId) {
    throw "Controlify bundle targets $($controlifyBundle.targetId) but the default target is $expectedTargetId"
}
$controlifyModNames = @($controlifyBundle.mods | ForEach-Object { $_.name })
foreach ($bundleMod in $controlifyBundle.mods) {
    $cachedMod = Join-Path (Join-Path $gameDir "mods") $bundleMod.name
    Save-RemoteFile -Uri $bundleMod.url -Path $cachedMod -Sha1 $bundleMod.sha1 -Size ([long]$bundleMod.size)
    Copy-Item -LiteralPath $cachedMod -Destination (Join-Path $pkg "runtime\bundled-mods\$($bundleMod.name)") -Force
    Write-Host "Bundled $($bundleMod.name) ($($bundleMod.size) bytes)"
}

# com.google.guava:listenablefuture:9999.0-empty-to-avoid-conflict-with-guava
# is an empty 2 KB placeholder, but its Maven path is 153 characters - thirty
# longer than anything else in the manifest. With the Xbox LocalState prefix it
# crosses Win32 MAX_PATH, so its runtime download never completes (4020/4021
# files and then "Retrying in 10 seconds"). Ship it in the package instead:
# EnsureRuntimeDownloads treats packaged libraries as already present and
# CollectManifestLibraryJars prefers them on the classpath.
$placeholderRelative = "com\google\guava\listenablefuture\9999.0-empty-to-avoid-conflict-with-guava\listenablefuture-9999.0-empty-to-avoid-conflict-with-guava.jar"
$placeholderUrl = "https://libraries.minecraft.net/com/google/guava/listenablefuture/9999.0-empty-to-avoid-conflict-with-guava/listenablefuture-9999.0-empty-to-avoid-conflict-with-guava.jar"
$placeholderSource = Join-Path (Join-Path $gameDir "libraries") $placeholderRelative
$placeholderDest = Join-Path (Join-Path $pkg "runtime\libraries") $placeholderRelative
try {
    Save-RemoteFile -Uri $placeholderUrl -Path $placeholderSource -Sha1 "b421526c5f297295adef1c886e5246c39d4ac629" -Size 2199
    Ensure-Dir (Split-Path $placeholderDest -Parent)
    Copy-Item -LiteralPath $placeholderSource -Destination $placeholderDest -Force
    Write-Host "Packaged Guava listenablefuture placeholder (avoids the MAX_PATH download)"
} catch {
    # The launcher's extended-length download path is the fallback; do not
    # fail the whole package if this 2 KB file could not be cached locally.
    Write-Warning "Could not bundle the Guava listenablefuture placeholder: $($_.Exception.Message)"
}

Copy-Item $mcExe (Join-Path $pkg "MC.Xbox.exe")

$manifestOut = Join-Path $pkg "AppxManifest.xml"
$manifestText = [System.IO.File]::ReadAllText($manifestSourcePath)
$manifestText = [regex]::Replace($manifestText, '(<Identity\b[^>]*\bVersion=")\d+\.\d+\.\d+\.\d+(")', ('${1}' + $appVersion + '${2}'))
[System.IO.File]::WriteAllText($manifestOut, $manifestText)
Write-Host "Package identity: $($sourceManifest.Package.Identity.Name)"
Write-Host "App package version: $appVersion"

Write-Host "Copying launcher-owned runtime files..."
$versionCatalogSource = Join-Path $root $ProjectConfig.VersionCatalog
if (-not (Test-Path $versionCatalogSource)) {
    throw "Version catalog not found at $versionCatalogSource"
}
Copy-Item $versionCatalogSource (Join-Path $pkg "runtime\version_catalog.tsv") -Force
Write-Host "Version catalog: $versionCatalogSource"

$recommendedModsSource = Join-Path $root "config\recommended-mods.json"
if (-not (Test-Path $recommendedModsSource)) {
    throw "Recommended mods file not found at $recommendedModsSource"
}
Copy-Item $recommendedModsSource (Join-Path $pkg "runtime\recommended-mods.json") -Force
Write-Host "Recommended mods: $recommendedModsSource"

# the launcher shows this when it cannot reach GitHub for a newer copy
$changelogSource = Join-Path $root "CHANGELOG.md"
if (-not (Test-Path $changelogSource)) {
    throw "Changelog not found at $changelogSource"
}
Copy-Item $changelogSource (Join-Path $pkg "CHANGELOG.md") -Force

$languageSource = Join-Path $root "MC.Xbox\lang"
Ensure-Dir (Join-Path $pkg "lang")
Copy-Item (Join-Path $languageSource "*.json") (Join-Path $pkg "lang") -Force

# NeoForge client jars are derived from the Minecraft client. Keep them out of normal and
# nightly builds; opt in only for private diagnostics while on-device generation is being fixed.
$prebuiltLibs = Join-Path $root "prebuilt\neoforge\libraries"
if (($IncludePrebuiltNeoForgeArtifacts -or $env:BANDIT_INCLUDE_PREBUILT_NEOFORGE_ARTIFACTS -eq "1") -and (Test-Path $prebuiltLibs)) {
    $prebuiltDst = Join-Path $pkg "runtime\libraries"
    Get-ChildItem $prebuiltLibs -Recurse -File | ForEach-Object {
        $rel = $_.FullName.Substring($prebuiltLibs.Length).TrimStart('\')
        $dst = Join-Path $prebuiltDst $rel
        Ensure-Dir (Split-Path $dst -Parent)
        Copy-Item $_.FullName $dst -Force
    }
    Write-Host "Packaged prebuilt NeoForge client jars from $prebuiltLibs"
} elseif (Test-Path $prebuiltLibs) {
    Write-Host "Skipping prebuilt NeoForge client jars. Use -IncludePrebuiltNeoForgeArtifacts for private diagnostics."
}

function Ensure-FabricLoaderJar {
    param([Parameter(Mandatory = $true)][string]$LoaderVersion)

    $loaderRelative = "net\fabricmc\fabric-loader\$LoaderVersion\fabric-loader-$LoaderVersion.jar"
    $loaderSrc = Join-Path $gameDir "libraries\$loaderRelative"
    $loaderUrl = "https://maven.fabricmc.net/net/fabricmc/fabric-loader/$LoaderVersion/fabric-loader-$LoaderVersion.jar"
    Save-RemoteFile -Uri $loaderUrl -Path $loaderSrc

    & (Join-Path $root "scripts\patch-fabric.ps1") -LoaderVersion $LoaderVersion

    $loaderDst = Join-Path $pkg "runtime\libraries\$loaderRelative"
    Ensure-Dir (Split-Path $loaderDst -Parent)
    Copy-Item $loaderSrc $loaderDst -Force
    Write-Host "Packaged patched Fabric loader $LoaderVersion"
}

function Ensure-TinyRemapperJar {
    param([Parameter(Mandatory = $true)][string]$TinyRemapperVersion)

    $tinyRelative = "net\fabricmc\tiny-remapper\$TinyRemapperVersion\tiny-remapper-$TinyRemapperVersion.jar"
    $tinySrc = Join-Path $gameDir "libraries\$tinyRelative"
    $tinyUrl = "https://maven.fabricmc.net/net/fabricmc/tiny-remapper/$TinyRemapperVersion/tiny-remapper-$TinyRemapperVersion.jar"
    Save-RemoteFile -Uri $tinyUrl -Path $tinySrc

    $java = Resolve-JavaHome
    $jarExe = Join-Path $java "bin\jar.exe"
    $tmp = Join-Path $buildDir "patch-tinyremapper\$TinyRemapperVersion"
    $srcTmp = Join-Path $tmp "src\net\fabricmc\tinyremapper"
    $classesTmp = Join-Path $tmp "classes"
    $jarTmp = Join-Path $tmp "jar"
    $patchedTiny = Join-Path $tmp "tiny-remapper-$TinyRemapperVersion-patched.jar"
    $tinyPatchSources = @("FileSystemReference.java", "FileSystemHandler.java", "OutputConsumerPath.java" | ForEach-Object { Join-Path $root "patch\$_" })
    $tinyStampPath = Join-Path $buildDir "patch-tinyremapper\$TinyRemapperVersion.stamp"
    $tinyStamp = New-BuildStamp `
        -Values @("tinyremapper_patch", $TinyRemapperVersion, $java) `
        -ContentFiles (@($PSCommandPath) + $tinyPatchSources) `
        -DependencyFiles @($tinySrc)

    if (-not (Test-BuildStampCurrent -StampPath $tinyStampPath -Stamp $tinyStamp -RequiredOutputs @($patchedTiny))) {
        Write-Host "Patching TinyRemapper $TinyRemapperVersion for Xbox filesystem..."
        Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
        Ensure-Dir $srcTmp, $classesTmp, $jarTmp

        foreach ($name in @("FileSystemReference.java", "FileSystemHandler.java", "OutputConsumerPath.java")) {
            $sourcePath = Join-Path $root "patch\$name"
            $sourceText = [System.IO.File]::ReadAllText($sourcePath)
            $sourceText = $sourceText.Replace(
                "package net.fabricmc.loader.impl.lib.tinyremapper;",
                "package net.fabricmc.tinyremapper;")
            [System.IO.File]::WriteAllText((Join-Path $srcTmp $name), $sourceText)
        }

        & (Join-Path $java "bin\javac.exe") --release 21 -cp $tinySrc -d $classesTmp `
            (Join-Path $srcTmp "FileSystemReference.java") `
            (Join-Path $srcTmp "FileSystemHandler.java") `
            (Join-Path $srcTmp "OutputConsumerPath.java")
        if ($LASTEXITCODE -ne 0) { throw "TinyRemapper patch compile failed for $TinyRemapperVersion" }

        Push-Location $jarTmp
        & $jarExe xf $tinySrc
        Pop-Location
        if ($LASTEXITCODE -ne 0) { throw "TinyRemapper JAR extract failed for $TinyRemapperVersion" }

        $classFiles = Get-ChildItem -LiteralPath $classesTmp -Recurse -Filter "*.class"
        foreach ($classFile in $classFiles) {
            $relativePath = $classFile.FullName.Substring($classesTmp.Length).TrimStart('\', '/')
            $dst = Join-Path $jarTmp $relativePath
            Ensure-Dir (Split-Path $dst -Parent)
            Copy-Item -LiteralPath $classFile.FullName -Destination $dst -Force
            Write-Host "  injected $($relativePath.Replace('\', '/'))"
        }

        $metaInf = Join-Path $jarTmp "META-INF"
        if (Test-Path $metaInf) {
            Get-ChildItem -LiteralPath $metaInf -File |
                Where-Object { $_.Name -match '\.(SF|RSA|DSA|EC)$' } |
                ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }
        }
        $manifest = Join-Path $jarTmp "META-INF\MANIFEST.MF"
        $manifestCopy = Join-Path $tmp "MANIFEST.MF"
        if (Test-Path $manifest) {
            Copy-Item -LiteralPath $manifest -Destination $manifestCopy -Force
            Remove-Item -LiteralPath $manifest -Force
        }
        if (Test-Path $manifestCopy) {
            & $jarExe cfm $patchedTiny $manifestCopy -C $jarTmp .
        } else {
            & $jarExe cf $patchedTiny -C $jarTmp .
        }
        if ($LASTEXITCODE -ne 0) { throw "TinyRemapper JAR repack failed for $TinyRemapperVersion" }
        Set-BuildStamp -StampPath $tinyStampPath -Stamp $tinyStamp
    }

    $tinyDst = Join-Path $pkg "runtime\libraries\$tinyRelative"
    Ensure-Dir (Split-Path $tinyDst -Parent)
    Copy-Item $patchedTiny $tinyDst -Force
    Write-Host "Packaged patched TinyRemapper $TinyRemapperVersion"
}

$catalog = @(Import-Csv -Path $versionCatalogSource -Delimiter "`t")

# The compiled default must be a real catalog row: the launcher resolves the
# default target through runtime/version_catalog.tsv, and the package must carry
# that row's download manifest and bundled mods.
$defaultTargetId = "$($ProjectConfig.DefaultMinecraftVersion)-$($ProjectConfig.DefaultLoader)-$($ProjectConfig.DefaultLoaderVersion)"
$defaultCatalogRow = @($catalog | Where-Object {
    $_.minecraftVersion -eq $ProjectConfig.DefaultMinecraftVersion -and
    $_.loader -eq $ProjectConfig.DefaultLoader -and
    $_.loaderVersion -eq $ProjectConfig.DefaultLoaderVersion
})
if ($defaultCatalogRow.Count -ne 1) {
    throw "Default target '$defaultTargetId' must appear exactly once in $versionCatalogSource (found $($defaultCatalogRow.Count)). Add the row or override DEFAULT_LOADER / DEFAULT_LOADER_VERSION."
}
if ($defaultCatalogRow[0].javaRuntime -ne $ProjectConfig.DefaultJavaRuntime) {
    Write-Warning "Default target '$defaultTargetId' uses javaRuntime '$($defaultCatalogRow[0].javaRuntime)' in the catalog but '$($ProjectConfig.DefaultJavaRuntime)' is compiled into the launcher. Set DEFAULT_JAVA_RUNTIME to match."
}
Write-Host "Default launch target: $defaultTargetId (javaRuntime=$($defaultCatalogRow[0].javaRuntime))"

$manifestTargets = @(
    $catalog |
        Where-Object { $_.loader -and $_.loaderVersion -and $_.loaderVersion -ne "selected" -and $_.loaderVersion -ne "none" }
)
$fabricTargets = @($manifestTargets | Where-Object { $_.loader -eq "fabric" })
$forgeTargets = @($manifestTargets | Where-Object { $_.loader -eq "forge" })
$neoForgeTargets = @($manifestTargets | Where-Object { $_.loader -eq "neoforge" })
$cleanroomTargets = @($manifestTargets | Where-Object { $_.loader -eq "cleanroom" })
# controllerProvider is "none" for every catalog row: this fork bundles
# Controlify once for the default target instead of compiling a Bandit
# controller variant for each of the 56 targets.
Write-Phase "Patched Fabric loader jars"
$fabricLoaderVersions = @($ProjectConfig.FabricLoaderVersion) + @($fabricTargets | ForEach-Object { $_.loaderVersion }) |
    Where-Object { $_ } |
    Select-Object -Unique
foreach ($loaderVersion in $fabricLoaderVersions) {
    try {
        Ensure-FabricLoaderJar -LoaderVersion $loaderVersion
    } catch {
        if ($loaderVersion -eq $ProjectConfig.FabricLoaderVersion) {
            throw
        }
        Add-BuildFailure -Stage "Patched Fabric loader" -Target $loaderVersion -Reason $_.Exception.Message
    }
}
if ($fabricLoaderVersions -contains "0.14.25") {
    Ensure-TinyRemapperJar -TinyRemapperVersion "0.8.2"
}
# Bundled mods live under runtime\bundled-mods and are seeded into
# LocalState\game\mods on launch: the UWP compat patch plus the Controlify
# bundle. The Bandit controller jar is intentionally absent.
$bundledModNames = @("banditvault-xbox-compat-1.0.0.jar") + $controlifyModNames
foreach ($name in $bundledModNames) {
    $source = Join-Path $gameDir "mods\$name"
    if (-not (Test-JarComplete $source)) { throw "Required bundled mod missing or incomplete $source" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $pkg "runtime\bundled-mods\$name") -Force
}

Write-Host "Copying natives..."
Copy-Item (Join-Path $nativesSourceDir "*.dll") (Join-Path $pkg "natives\")

Write-Host "Extracting JNA native..."
Add-Type -AssemblyName System.IO.Compression.FileSystem
$jnaVersion = $ProjectConfig.JnaVersion
$jnaJar = Join-Path $gameDir "libraries\net\java\dev\jna\jna\$jnaVersion\jna-$jnaVersion.jar"
if (Test-Path $jnaJar) {
    $zip = [System.IO.Compression.ZipFile]::OpenRead($jnaJar)
    try {
        $entry = $zip.Entries | Where-Object { $_.FullName -eq "com/sun/jna/win32-x86-64/jnidispatch.dll" } | Select-Object -First 1
        if ($entry) {
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, (Join-Path $pkg "natives\jnidispatch.dll"), $true)
            Write-Host "JNA: jnidispatch.dll"
        } else {
            Write-Warning "win32-x86-64/jnidispatch.dll not found in $jnaJar"
        }
    } finally {
        $zip.Dispose()
    }
}

Write-Host "Copying GLFW shim..."
Copy-Item $shimDll (Join-Path $pkg "natives\glfw.dll") -Force

Write-Host "Copying Mesa runtime..."
$mesaRuntime = Resolve-MesaRuntimeDir -MesaRuntimeDir $MesaRuntimeDir
Write-Host "Mesa runtime source: $mesaRuntime"
foreach ($dll in Get-MesaRuntimeDllNames) {
    $source = Join-Path $mesaRuntime $dll
    if (Test-Path $source) {
        Copy-Item $source (Join-Path $pkg $dll) -Force
        Copy-Item $source (Join-Path $pkg "natives\$dll") -Force
        Copy-Item $source (Join-Path $pkg "graphics\mesa\$dll") -Force
        Write-Host "Mesa: $dll"
    }
}

$manifestGenerator = Join-Path $root "scripts\new-download-manifest.ps1"
$manifestCacheRoot = Join-Path (Get-ConfigPath "StagingDir") "cache\manifests"
Ensure-Dir $manifestCacheRoot

function Copy-CachedVersionManifest {
    param(
        [Parameter(Mandatory = $true)][string]$MinecraftVersion,
        [Parameter(Mandatory = $true)][string]$Loader,
        [Parameter(Mandatory = $true)][string]$LoaderVersion,
        [Parameter(Mandatory = $true)][string]$OutputPath
    )

    $targetId = "$MinecraftVersion-$Loader-$LoaderVersion"
    $cachePath = Join-Path $manifestCacheRoot "$targetId.tsv"
    $stampPath = "$cachePath.stamp"
    $stamp = New-BuildStamp `
        -Values @("download_manifest", $MinecraftVersion, $Loader, $LoaderVersion) `
        -ContentFiles @($manifestGenerator, (Join-Path $root "scripts\common.ps1"))

    if (Test-BuildStampCurrent -StampPath $stampPath -Stamp $stamp -RequiredOutputs @($cachePath)) {
        Write-Host "Download manifest up to date: $targetId"
    } else {
        Write-Host "Generating download manifest: $targetId"
        & $manifestGenerator `
            -MinecraftVersion $MinecraftVersion `
            -Loader $Loader `
            -LoaderVersion $LoaderVersion `
            -OutputPath $cachePath
        Set-BuildStamp -StampPath $stampPath -Stamp $stamp
    }

    Ensure-Dir (Split-Path $OutputPath -Parent)
    Copy-Item -LiteralPath $cachePath -Destination $OutputPath -Force
}

# download_manifest.tsv is the launcher's fallback manifest for the *default*
# target, so it has to describe the same loader the compiled default uses.
$defaultDownloadManifest = Join-Path $pkg "download_manifest.tsv"
Copy-CachedVersionManifest `
    -MinecraftVersion $ProjectConfig.DefaultMinecraftVersion `
    -Loader $ProjectConfig.DefaultLoader `
    -LoaderVersion $ProjectConfig.DefaultLoaderVersion `
    -OutputPath $defaultDownloadManifest

# Warn (do not fail) when the official installer metadata uses a different id
# than the compiled fallback. The manifest header is authoritative at runtime;
# this only catches stale DEFAULT_LAUNCH_VERSION values in scripts/config.ps1.
$manifestLaunchVersion = (Select-String -LiteralPath $defaultDownloadManifest -Pattern "^# launchVersion`t" | Select-Object -First 1).Line
if ($manifestLaunchVersion) {
    $manifestLaunchVersion = ($manifestLaunchVersion -split "`t", 2)[1]
    if ($manifestLaunchVersion -ne $ProjectConfig.DefaultLaunchVersion) {
        Write-Warning "Default manifest launchVersion is '$manifestLaunchVersion' but DEFAULT_LAUNCH_VERSION is '$($ProjectConfig.DefaultLaunchVersion)'. The manifest wins at runtime; update scripts/config.ps1 to silence this warning."
    }
}

$manifestsDir = Join-Path $pkg "runtime\manifests"
Ensure-Dir $manifestsDir
Ensure-Dir (Join-Path $pkg "runtime\version-mods\$defaultTargetId")
# minecraft_launch.cpp syncs version-mods/<target> into the active profile's
# mods folder for Forge/NeoForge/Cleanroom. Put the loader-matched Controlify
# bundle here so every new default instance starts with Controller support and
# nothing else from the old Bandit controller stack.
foreach ($name in $controlifyModNames) {
    Copy-Item (Join-Path $pkg "runtime\bundled-mods\$name") (Join-Path $pkg "runtime\version-mods\$defaultTargetId\$name") -Force
}
if ($ProjectConfig.DefaultLoader -eq "fabric") {
    # The UWP compat patch is a Fabric mod; only copy it into a Fabric default.
    Copy-Item (Join-Path $pkg "runtime\bundled-mods\banditvault-xbox-compat-1.0.0.jar") `
        (Join-Path $pkg "runtime\version-mods\$defaultTargetId\banditvault-xbox-compat-1.0.0.jar") -Force
}
Copy-Item -Force $defaultDownloadManifest (Join-Path $manifestsDir "$defaultTargetId.tsv")
Write-Host "Default per-version manifest: $defaultTargetId.tsv"

Write-Phase "Per-version download manifests"
if (-not $SkipVersionManifests) {
    foreach ($row in $manifestTargets) {
        $lv = $row.loaderVersion
        $loader = $row.loader.ToLowerInvariant()
        $targetId = "$($row.minecraftVersion)-$loader-$lv"
        if ($targetId -eq $defaultTargetId) { continue }
        $out = Join-Path $manifestsDir "$targetId.tsv"
        try {
            Copy-CachedVersionManifest `
                -MinecraftVersion $row.minecraftVersion `
                -Loader $loader `
                -LoaderVersion $lv `
                -OutputPath $out
        } catch {
            Add-BuildFailure -Stage "Download manifest" -Target $targetId -Reason $_.Exception.Message
            if (Test-Path $out) { Remove-Item -Force $out }
        }
    }
} else {
    Write-Host "Skipping extra per-version manifests (-SkipVersionManifests)"
}

Write-Phase "Per-version compatibility mods"
if (-not $SkipVersionCompat) {
    $versionModsRoot = Join-Path $pkg "runtime\version-mods"
    Ensure-Dir $versionModsRoot
    # The Bandit controller mod is no longer compiled for any catalog target.
    # Building one variant per row meant 51 Fabric/Forge/Cleanroom/NeoForge
    # installer and compile runs: 1,633.7 s (27 min) of the 2,317.2 s (39 min)
    # of a cold build, and the NeoForge installers were the only step that
    # failed on transient network errors. Controlify is bundled instead.
    foreach ($row in $fabricTargets) {
        $lv = $row.loaderVersion
        $targetId = "$($row.minecraftVersion)-fabric-$lv"
        if ($targetId -eq $defaultTargetId) { continue }
        $outDir = Join-Path $versionModsRoot $targetId
        Write-Host "Building per-version compat mod: $targetId"
        try {
            & (Join-Path $root "compat_mod\build_compat_mod.ps1") `
                -MinecraftVersion $row.minecraftVersion `
                -LoaderVersion $lv `
                -OutputDir $outDir
        } catch {
            Add-BuildFailure -Stage "Compat mod" -Target $targetId -Reason $_.Exception.Message
            if (Test-Path $outDir) { Remove-Item -Recurse -Force $outDir }
        }
    }
    foreach ($row in $cleanroomTargets) {
        $lv = $row.loaderVersion
        $targetId = "$($row.minecraftVersion)-cleanroom-$lv"
        try {
            & (Join-Path $root "patch\cleanroom\build_cleanroom_patch.ps1") `
                -CleanroomVersion $lv `
                -OutputJar (Join-Path $pkg "cleanroom-uwp-patch.jar")
        } catch {
            Add-BuildFailure -Stage "Cleanroom UWP patch" -Target $targetId -Reason $_.Exception.Message
        }
    }
    # ResolveVersionInfo marks a target unsupported when its version-mods
    # directory is absent, so always keep one for the default target.
    Ensure-Dir (Join-Path $versionModsRoot $defaultTargetId)
} else {
    Write-Host "Skipping per-version compat mods (-SkipVersionCompat)"
}

Copy-Item -Force (Join-Path $root "log_configs\client-uwp.xml") (Join-Path $pkg "runtime\log_configs\client-uwp.xml")

$screenshotSource = Join-Path $root "MC.Xbox\Assets\screenshots"
if (Test-Path $screenshotSource) {
    $screenshotTarget = Join-Path $pkg "Assets\screenshots"
    Ensure-Dir $screenshotTarget
    Copy-Item -Force (Join-Path $screenshotSource "*.png") $screenshotTarget
    Write-Host "Copied menu screenshot assets from $screenshotSource"
}

function Copy-PackagedJre {
    param(
        [Parameter(Mandatory = $true)][string]$JavaHome,
        [Parameter(Mandatory = $true)][string]$PackageRelativeDir,
        [Parameter(Mandatory = $true)][string]$SecurityPropertiesPath
    )

    $dest = Join-Path $pkg $PackageRelativeDir
    Write-Host "Copying JRE ($PackageRelativeDir)..."
    Write-Host "JRE source: $JavaHome"
    Copy-Item -Recurse $JavaHome $dest
    # jmods is jlink input, nothing at runtime opens it, and it is about 80 MB of every packaged jre
    $jmods = Join-Path $dest "jmods"
    if (Test-Path $jmods) { Remove-Item -Recurse -Force $jmods }
    Copy-Item $SecurityPropertiesPath (Join-Path $dest "conf\security\xbox.properties") -Force
}

function Build-JavaBaseUwpFilesystemPatch {
    param(
        [Parameter(Mandatory = $true)][string]$JavaHome,
        [Parameter(Mandatory = $true)][string]$OutputJar,
        [Parameter(Mandatory = $true)][string]$WorkName
    )

    $javacExe = Join-Path $JavaHome "bin\javac.exe"
    if (-not (Test-Path $javacExe)) { throw "javac.exe not found at $javacExe; Java base UWP filesystem patch requires a JDK, not a JRE." }
    $runtimeJarExe = Join-Path $JavaHome "bin\jar.exe"
    if (-not (Test-Path $runtimeJarExe)) { $runtimeJarExe = $jarExe }
    $srcZip = Join-Path $JavaHome "lib\src.zip"
    if (-not (Test-Path $srcZip)) { throw "JDK source archive not found at $srcZip; Java base UWP filesystem patch cannot be generated." }
    $cacheJar = Join-Path $buildDir "$WorkName.jar"
    $stampPath = Join-Path $buildDir "$WorkName.stamp"
    $stamp = New-BuildStamp `
        -Values @("java_base_uwp_filesystem_patch", $JavaHome) `
        -ContentFiles @($PSCommandPath, (Join-Path $JavaHome "release")) `
        -ImmutableFiles @($srcZip)
    if (Test-BuildStampCurrent -StampPath $stampPath -Stamp $stamp -RequiredOutputs @($cacheJar)) {
        Copy-Item $cacheJar $OutputJar -Force
        Write-Host "Java base UWP filesystem patch up to date: $OutputJar"
        return
    }
    Write-Host "Building Java base UWP filesystem patch: $OutputJar"
    $javaBasePatchDir = Join-Path $buildDir $WorkName
    $javaBasePatchSrcDir = Join-Path $javaBasePatchDir "src"
    $javaBasePatchClassesDir = Join-Path $javaBasePatchDir "classes"
    Remove-Item -Recurse -Force $javaBasePatchDir -ErrorAction SilentlyContinue
    Ensure-Dir `
        (Join-Path $javaBasePatchSrcDir "java\io"), `
        (Join-Path $javaBasePatchSrcDir "sun\nio\fs"), `
        $javaBasePatchClassesDir
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $srcArchive = [System.IO.Compression.ZipFile]::OpenRead($srcZip)
    try {
        $fileEntry = $srcArchive.Entries | Where-Object { $_.FullName -eq "java.base/java/io/File.java" } | Select-Object -First 1
        if (-not $fileEntry) { throw "File.java not found inside $srcZip" }
        $reader = [System.IO.StreamReader]::new($fileEntry.Open())
        try {
            $fileSource = $reader.ReadToEnd()
        } finally {
            $reader.Dispose()
        }

        $windowsPathEntry = $srcArchive.Entries | Where-Object { $_.FullName -eq "java.base/sun/nio/fs/WindowsPath.java" } | Select-Object -First 1
        if (-not $windowsPathEntry) { throw "WindowsPath.java not found inside $srcZip" }
        $reader = [System.IO.StreamReader]::new($windowsPathEntry.Open())
        try {
            $windowsPathSource = $reader.ReadToEnd()
        } finally {
            $reader.Dispose()
        }
    } finally {
        $srcArchive.Dispose()
    }
    $javaBasePatchSources = @()

    # jdk 17 calls the field fs, 21 and later FS
    $fsField = if ($fileSource.Contains("int nameMax = FS.getNameMax(dir.getPath());")) { "FS" }
        elseif ($fileSource.Contains("int nameMax = fs.getNameMax(dir.getPath());")) { "fs" }
        else { throw "File.java temp-file nameMax patch target not found for $JavaHome." }
    $oldFileLine = "            int nameMax = $fsField.getNameMax(dir.getPath());"
    $newFileBlock = @"
            int nameMax;
            try {
                nameMax = $fsField.getNameMax(dir.getPath());
            } catch (Throwable nameMaxFailure) {
                nameMax = 255;
            }
"@
    if (-not $fileSource.Contains($oldFileLine)) {
        throw "File.java temp-file nameMax patch target not found for $JavaHome."
    }
    $fileSource = $fileSource.Replace($oldFileLine, $newFileBlock)
    $fileSourcePath = Join-Path $javaBasePatchSrcDir "java\io\File.java"
    [System.IO.File]::WriteAllText($fileSourcePath, $fileSource)
    $javaBasePatchSources += $fileSourcePath

    $oldWindowsPathBlock = @'
        String rp = WindowsLinkSupport.getRealPath(this, Util.followLinks(options));
        return createFromNormalizedPath(getFileSystem(), rp);
'@
    $newWindowsPathBlock = @'
        try {
            String rp = WindowsLinkSupport.getRealPath(this, Util.followLinks(options));
            return createFromNormalizedPath(getFileSystem(), rp);
        } catch (IOException realPathFailure) {
            return (WindowsPath)toAbsolutePath().normalize();
        }
'@
    if (-not $windowsPathSource.Contains($oldWindowsPathBlock)) {
        throw "WindowsPath.java realpath patch target not found for $JavaHome."
    }
    $windowsPathSource = $windowsPathSource.Replace($oldWindowsPathBlock, $newWindowsPathBlock)
    $windowsPathSourcePath = Join-Path $javaBasePatchSrcDir "sun\nio\fs\WindowsPath.java"
    [System.IO.File]::WriteAllText($windowsPathSourcePath, $windowsPathSource)
    $javaBasePatchSources += $windowsPathSourcePath

    & $javacExe --patch-module "java.base=$javaBasePatchSrcDir" -d $javaBasePatchClassesDir $javaBasePatchSources
    if ($LASTEXITCODE -ne 0) { throw "Java base UWP filesystem patch compile failed" }
    Push-Location $javaBasePatchClassesDir
    & $runtimeJarExe cf $cacheJar .
    Pop-Location
    if ($LASTEXITCODE -ne 0) { throw "Java base UWP filesystem patch jar creation failed" }
    Set-BuildStamp -StampPath $stampPath -Stamp $stamp
    Copy-Item $cacheJar $OutputJar -Force
    Write-Host "Java base UWP filesystem patch: $OutputJar"
}

function Build-JavaZipfsRealpathPatch {
    param(
        [Parameter(Mandatory = $true)][string]$JavaHome,
        [Parameter(Mandatory = $true)][string]$OutputJar,
        [Parameter(Mandatory = $true)][string]$WorkName
    )

    Write-Host "Building Java ZipFS realpath patch: $OutputJar"
    $javacExe = Join-Path $JavaHome "bin\javac.exe"
    if (-not (Test-Path $javacExe)) { throw "javac.exe not found at $javacExe; Java ZipFS patch requires a JDK, not a JRE." }
    $runtimeJarExe = Join-Path $JavaHome "bin\jar.exe"
    if (-not (Test-Path $runtimeJarExe)) { $runtimeJarExe = $jarExe }
    $srcZip = Join-Path $JavaHome "lib\src.zip"
    if (-not (Test-Path $srcZip)) { throw "JDK source archive not found at $srcZip; Java ZipFS patch cannot be generated." }

    $cacheJar = Join-Path $buildDir "$WorkName.jar"
    $stampPath = Join-Path $buildDir "$WorkName.stamp"
    $stamp = New-BuildStamp `
        -Values @("java_zipfs_realpath_patch", $JavaHome) `
        -ContentFiles @($PSCommandPath, (Join-Path $JavaHome "release")) `
        -ImmutableFiles @($srcZip)
    if (Test-BuildStampCurrent -StampPath $stampPath -Stamp $stamp -RequiredOutputs @($cacheJar)) {
        Copy-Item $cacheJar $OutputJar -Force
        Write-Host "Java ZipFS patch up to date: $OutputJar"
        return
    }

    $zipfsPatchDir = Join-Path $buildDir $WorkName
    $zipfsPatchSrcDir = Join-Path $zipfsPatchDir "src"
    $zipfsPatchClassesDir = Join-Path $zipfsPatchDir "classes"
    Remove-Item -Recurse -Force $zipfsPatchDir -ErrorAction SilentlyContinue
    Ensure-Dir (Join-Path $zipfsPatchSrcDir "jdk\nio\zipfs"), $zipfsPatchClassesDir

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $srcArchive = [System.IO.Compression.ZipFile]::OpenRead($srcZip)
    try {
        $providerEntry = $srcArchive.Entries | Where-Object { $_.FullName -eq "jdk.zipfs/jdk/nio/zipfs/ZipFileSystemProvider.java" } | Select-Object -First 1
        if (-not $providerEntry) { throw "ZipFileSystemProvider.java not found inside $srcZip" }
        $reader = [System.IO.StreamReader]::new($providerEntry.Open())
        try {
            $providerSource = $reader.ReadToEnd()
        } finally {
            $reader.Dispose()
        }
    } finally {
        $srcArchive.Dispose()
    }

    $providerSource = $providerSource.Replace("uriToPath(uri).toRealPath()", "safeRealPath(uriToPath(uri))")
    $providerSource = $providerSource.Replace("zfpath.toRealPath()", "safeRealPath(zfpath)")
    $providerSource = $providerSource.Replace("PrivilegedExceptionAction<Path> action = tempPath::toRealPath;", "PrivilegedExceptionAction<Path> action = () -> safeRealPath(tempPath);")
    $providerSource = $providerSource.Replace("path.toRealPath()", "safeRealPath(path)")

    $helper = @'
    private static Path safeRealPath(Path path) throws IOException {
        try {
            return path.toRealPath();
        } catch (IOException realPathFailure) {
            return path.toAbsolutePath().normalize();
        }
    }

'@
    $insertAfter = "    }`r`n`r`n    @Override`r`n    public FileSystem newFileSystem"
    if (-not $providerSource.Contains($insertAfter)) {
        $insertAfter = "    }`n`n    @Override`n    public FileSystem newFileSystem"
    }
    if (-not $providerSource.Contains($insertAfter)) {
        throw "ZipFileSystemProvider.java patch insert point not found."
    }
    $insertIndex = $providerSource.IndexOf($insertAfter)
    if ($insertIndex -lt 0) {
        throw "ZipFileSystemProvider.java patch insert point not found."
    }
    $replacement = "    }`r`n`r`n$helper    @Override`r`n    public FileSystem newFileSystem"
    $providerSource = $providerSource.Substring(0, $insertIndex) + $replacement + $providerSource.Substring($insertIndex + $insertAfter.Length)

    if ($providerSource -notmatch "safeRealPath\(path\)" -or
        $providerSource -match "uriToPath\(uri\)\.toRealPath\(\)|zfpath\.toRealPath\(\)|tempPath::toRealPath") {
        throw "ZipFileSystemProvider.java realpath patch did not apply cleanly."
    }

    $providerSourcePath = Join-Path $zipfsPatchSrcDir "jdk\nio\zipfs\ZipFileSystemProvider.java"
    [System.IO.File]::WriteAllText($providerSourcePath, $providerSource)
    & $javacExe --patch-module "jdk.zipfs=$zipfsPatchSrcDir" -d $zipfsPatchClassesDir $providerSourcePath
    if ($LASTEXITCODE -ne 0) { throw "Java ZipFS patch compile failed" }
    Push-Location $zipfsPatchClassesDir
    & $runtimeJarExe cf $cacheJar .
    Pop-Location
    if ($LASTEXITCODE -ne 0) { throw "Java ZipFS patch jar creation failed" }
    Set-BuildStamp -StampPath $stampPath -Stamp $stamp
    Copy-Item $cacheJar $OutputJar -Force
    Write-Host "Java ZipFS patch: $OutputJar"
}

function Build-JavaDesktopUwpAwtPatch {
    param(
        [Parameter(Mandatory = $true)][string]$JavaHome,
        [Parameter(Mandatory = $true)][string]$OutputJar,
        [Parameter(Mandatory = $true)][string]$WorkName
    )

    Write-Host "Building Java desktop UWP AWT patch: $OutputJar"
    $javacExe = Join-Path $JavaHome "bin\javac.exe"
    if (-not (Test-Path $javacExe)) { throw "javac.exe not found at $javacExe; Java desktop UWP AWT patch requires a JDK, not a JRE." }
    $runtimeJarExe = Join-Path $JavaHome "bin\jar.exe"
    if (-not (Test-Path $runtimeJarExe)) { $runtimeJarExe = $jarExe }
    $srcZip = Join-Path $JavaHome "lib\src.zip"
    if (-not (Test-Path $srcZip)) { throw "JDK source archive not found at $srcZip; Java desktop UWP AWT patch cannot be generated." }

    $cacheJar = Join-Path $buildDir "$WorkName.jar"
    $stampPath = Join-Path $buildDir "$WorkName.stamp"
    $stamp = New-BuildStamp `
        -Values @("java_desktop_uwp_awt_patch", $JavaHome) `
        -ContentFiles @($PSCommandPath, (Join-Path $JavaHome "release")) `
        -ImmutableFiles @($srcZip)
    if (Test-BuildStampCurrent -StampPath $stampPath -Stamp $stamp -RequiredOutputs @($cacheJar)) {
        Copy-Item $cacheJar $OutputJar -Force
        Write-Host "Java desktop UWP AWT patch up to date: $OutputJar"
        return
    }

    $desktopPatchDir = Join-Path $buildDir $WorkName
    $desktopPatchSrcDir = Join-Path $desktopPatchDir "src"
    $desktopPatchClassesDir = Join-Path $desktopPatchDir "classes"
    Remove-Item -Recurse -Force $desktopPatchDir -ErrorAction SilentlyContinue
    Ensure-Dir (Join-Path $desktopPatchSrcDir "sun\awt\windows"), $desktopPatchClassesDir

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $srcArchive = [System.IO.Compression.ZipFile]::OpenRead($srcZip)
    try {
        $desktopEntry = $srcArchive.Entries | Where-Object { $_.FullName -eq "java.desktop/sun/awt/windows/WDesktopProperties.java" } | Select-Object -First 1
        if (-not $desktopEntry) { throw "WDesktopProperties.java not found inside $srcZip" }
        $reader = [System.IO.StreamReader]::new($desktopEntry.Open())
        try {
            $desktopSource = $reader.ReadToEnd()
        } finally {
            $reader.Dispose()
        }
    } finally {
        $srcArchive.Dispose()
    }

    $oldDesktopCall = "        getWindowsParameters();"
    $newDesktopCall = @'
        if (!Boolean.getBoolean("banditvault.awt.skipDesktopProperties")) {
            getWindowsParameters();
        }
'@
    if (-not $desktopSource.Contains($oldDesktopCall)) {
        throw "WDesktopProperties.java native desktop-properties call target not found for $JavaHome."
    }
    $desktopSource = $desktopSource.Replace($oldDesktopCall, $newDesktopCall)

    $desktopSourcePath = Join-Path $desktopPatchSrcDir "sun\awt\windows\WDesktopProperties.java"
    [System.IO.File]::WriteAllText($desktopSourcePath, $desktopSource)
    & $javacExe --patch-module "java.desktop=$desktopPatchSrcDir" -d $desktopPatchClassesDir $desktopSourcePath
    if ($LASTEXITCODE -ne 0) { throw "Java desktop UWP AWT patch compile failed" }
    Push-Location $desktopPatchClassesDir
    & $runtimeJarExe cf $cacheJar .
    Pop-Location
    if ($LASTEXITCODE -ne 0) { throw "Java desktop UWP AWT patch jar creation failed" }
    Set-BuildStamp -StampPath $stampPath -Stamp $stamp
    Copy-Item $cacheJar $OutputJar -Force
    Write-Host "Java desktop UWP AWT patch: $OutputJar"
}

function Resolve-SecureJarHandlerJar {
    param([Parameter(Mandatory = $true)][string]$Version)

    $relative = "cpw\mods\securejarhandler\$Version\securejarhandler-$Version.jar"
    $candidates = @(
        (Join-Path $gameDir "libraries\$relative"),
        (Join-Path $root ".local\pcgen\mc\libraries\$relative")
    )

    foreach ($candidate in $candidates) {
        if (Test-JarComplete $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    $downloadPath = Join-Path $gameDir "libraries\$relative"
    $url = "https://maven.neoforged.net/releases/cpw/mods/securejarhandler/$Version/securejarhandler-$Version.jar"
    Save-RemoteFile -Uri $url -Path $downloadPath
    return (Resolve-Path $downloadPath).Path
}

function Build-SecureJarHandlerUwpPatch {
    param(
        [Parameter(Mandatory = $true)][string]$JavaHome,
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$OutputJar
    )

    Write-Host "Building securejarhandler UWP patch: $OutputJar"
    $javacExe = Join-Path $JavaHome "bin\javac.exe"
    if (-not (Test-Path $javacExe)) { throw "javac.exe not found at $javacExe; securejarhandler patch requires a JDK." }
    $runtimeJarExe = Join-Path $JavaHome "bin\jar.exe"
    if (-not (Test-Path $runtimeJarExe)) { $runtimeJarExe = $jarExe }

    $secureJar = Resolve-SecureJarHandlerJar -Version $Version
    $patchSourceRoot = Join-Path $root "patch\securejarhandler"
    $patchSources = @(Get-ChildItem -LiteralPath $patchSourceRoot -Recurse -Filter "*.java" | ForEach-Object { $_.FullName })
    if ($patchSources.Count -eq 0) {
        throw "securejarhandler patch sources missing: $patchSourceRoot"
    }

    $cacheJar = Join-Path $buildDir "securejarhandler_uwp_patch\$Version.jar"
    $stampPath = Join-Path $buildDir "securejarhandler_uwp_patch\$Version.stamp"
    $stamp = New-BuildStamp `
        -Values @("securejarhandler_uwp_patch", $Version, $JavaHome) `
        -ContentFiles (@($PSCommandPath) + $patchSources) `
        -DependencyFiles @($secureJar)
    if (Test-BuildStampCurrent -StampPath $stampPath -Stamp $stamp -RequiredOutputs @($cacheJar)) {
        Copy-Item $cacheJar $OutputJar -Force
        Write-Host "securejarhandler UWP patch up to date: $OutputJar"
        return
    }

    $patchDir = Join-Path $buildDir "securejarhandler_uwp_patch\$Version"
    $classesDir = Join-Path $patchDir "classes"
    Remove-Item -Recurse -Force $patchDir -ErrorAction SilentlyContinue
    Ensure-Dir $classesDir

    & $javacExe --release 21 -cp $secureJar -d $classesDir $patchSources
    if ($LASTEXITCODE -ne 0) { throw "securejarhandler UWP patch compile failed" }

    & $runtimeJarExe cf $cacheJar -C $classesDir .
    if ($LASTEXITCODE -ne 0) { throw "securejarhandler UWP patch jar creation failed" }
    Set-BuildStamp -StampPath $stampPath -Stamp $stamp
    Copy-Item $cacheJar $OutputJar -Force
    Write-Host "securejarhandler UWP patch: $OutputJar"
}

Write-Phase "Java UWP patches and packaged JREs"
Write-Host "Copying JRE..."
$xboxSecurityProperties = Join-Path $root "xbox_security.properties"
Copy-Item $xboxSecurityProperties (Join-Path $pkg "xbox_security.properties") -Force
Copy-PackagedJre -JavaHome $jreSrc -PackageRelativeDir "jre" -SecurityPropertiesPath $xboxSecurityProperties
Copy-PackagedJre -JavaHome $jre21Src -PackageRelativeDir "jre21" -SecurityPropertiesPath $xboxSecurityProperties
try {
    $jre17Src = Resolve-JavaHomeExact -MajorVersion 17
    Copy-PackagedJre -JavaHome $jre17Src -PackageRelativeDir "jre17" -SecurityPropertiesPath $xboxSecurityProperties
    Build-JavaBaseUwpFilesystemPatch -JavaHome $jre17Src -OutputJar (Join-Path $pkg "java-base-uwp-filesystem-17.jar") -WorkName "java_base_uwp_filesystem_patch_17"
    Build-JavaZipfsRealpathPatch -JavaHome $jre17Src -OutputJar (Join-Path $pkg "java-zipfs-realpath-17.jar") -WorkName "java_zipfs_realpath_patch_17"
    Build-JavaDesktopUwpAwtPatch -JavaHome $jre17Src -OutputJar (Join-Path $pkg "java-desktop-uwp-awt-17.jar") -WorkName "java_desktop_uwp_awt_patch_17"
    Write-Host "Packaged Java 17 runtime for 1.18.x / 1.20.2-1.20.4 targets"
} catch {
    Add-BuildFailure -Stage "Java 17 JRE" -Target "javaRuntime=java17 targets" -Reason "$($_.Exception.Message). Needs JDK 17 on the build machine."
}
Build-JavaBaseUwpFilesystemPatch -JavaHome $jreSrc -OutputJar (Join-Path $pkg "java-base-uwp-filesystem.jar") -WorkName "java_base_uwp_filesystem_patch_current"
Build-JavaBaseUwpFilesystemPatch -JavaHome $jre21Src -OutputJar (Join-Path $pkg "java-base-uwp-filesystem-21.jar") -WorkName "java_base_uwp_filesystem_patch_21"
Build-JavaZipfsRealpathPatch -JavaHome $jreSrc -OutputJar (Join-Path $pkg "java-zipfs-realpath.jar") -WorkName "java_zipfs_realpath_patch_current"
Build-JavaZipfsRealpathPatch -JavaHome $jre21Src -OutputJar (Join-Path $pkg "java-zipfs-realpath-21.jar") -WorkName "java_zipfs_realpath_patch_21"
Build-JavaDesktopUwpAwtPatch -JavaHome $jreSrc -OutputJar (Join-Path $pkg "java-desktop-uwp-awt.jar") -WorkName "java_desktop_uwp_awt_patch_current"
Build-JavaDesktopUwpAwtPatch -JavaHome $jre21Src -OutputJar (Join-Path $pkg "java-desktop-uwp-awt-21.jar") -WorkName "java_desktop_uwp_awt_patch_21"
# Build the UWP patch against the securejarhandler version the default
# NeoForge target actually resolves. Older baseline was pinned to 3.0.8; new
# NeoForge releases ship newer SJH versions, and a patch compiled against the
# wrong one can fail to load. Fall back to 3.0.8 when metadata is unavailable.
$sjhVersion = "3.0.8"
$sjhMatch = Select-String -LiteralPath $defaultDownloadManifest -Pattern 'cpw/mods/securejarhandler/([^/]+)/securejarhandler-' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($sjhMatch -and $sjhMatch.Matches.Count -gt 0) {
    $resolvedSjh = $sjhMatch.Matches[0].Groups[1].Value
    if ($resolvedSjh) { $sjhVersion = $resolvedSjh }
}
$sjhCandidates = @($sjhVersion)
if ($sjhVersion -ne "3.0.8") { $sjhCandidates += "3.0.8" }
$sjhPatchJar = Join-Path $pkg "securejarhandler-uwp-patch.jar"
$sjhBuilt = $false
foreach ($candidate in $sjhCandidates) {
    try {
        Write-Host "Building securejarhandler UWP patch against $candidate"
        Build-SecureJarHandlerUwpPatch -JavaHome $jre21Src -Version $candidate -OutputJar $sjhPatchJar
        $sjhBuilt = $true
        break
    } catch {
        Write-Warning "securejarhandler $candidate patch build failed: $($_.Exception.Message)"
    }
}
if (-not $sjhBuilt) { throw "Could not build the securejarhandler UWP patch against any candidate version: $($sjhCandidates -join ', ')" }

Write-Host "Copying UWP tile assets..."
$appxAssetSource = Join-Path $root "MC.Xbox\Assets\appx"
$appxAssetNames = @("StoreLogo.png", "Square44x44Logo.png", "Square150x150Logo.png", "Wide310x150Logo.png", "SplashScreen.png")
foreach ($name in $appxAssetNames) {
    $source = Join-Path $appxAssetSource $name
    if (-not (Test-Path $source)) { throw "Packaged tile asset missing: $source" }
    Copy-Item -Force $source (Join-Path $pkg "Assets\$name")
}

Write-Phase "Packaging"
$cert = Join-Path $certDir $ProjectConfig.CertificateFileName
$certName = [string]$sourceManifest.Package.Identity.Publisher

if (-not (Test-Path $cert)) {
    $c = New-SelfSignedCertificate -Type CodeSigningCert -Subject $certName `
        -KeyUsage DigitalSignature -CertStoreLocation "Cert:\CurrentUser\My" `
        -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3","2.5.29.19={text}")
    Export-PfxCertificate -Cert $c -FilePath $cert `
        -Password (ConvertTo-SecureString $ProjectConfig.CertificatePassword -AsPlainText -Force) | Out-Null
    Write-Host "Generated cert"
}

$allSigningCertCandidates = Get-ChildItem Cert:\CurrentUser\My |
    Where-Object {
        $_.HasPrivateKey -and
        ($_.EnhancedKeyUsageList | Where-Object { $_.FriendlyName -eq 'Code Signing' })
    }
$exactSigningCertCandidates = $allSigningCertCandidates | Where-Object { $_.Subject -eq $certName } | Sort-Object NotBefore -Descending
$signingCertCandidates = @($exactSigningCertCandidates)
if (-not $signingCertCandidates) {
    throw "No code signing certificate for '$certName' in Cert:\CurrentUser\My."
}

$makeappx = Get-ChildItem "${sdkRoot}bin\$sdkVer\x64\makeappx.exe","${sdkRoot}bin\$sdkToolsFallbackVer\x64\makeappx.exe" -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
if (-not $makeappx) {
    $cmd = Get-Command makeappx -ErrorAction SilentlyContinue
    if ($cmd) { $makeappx = $cmd.Source }
}
if (-not $makeappx) { throw "makeappx.exe not found. Add Windows SDK bin to PATH." }
$signtool = Get-ChildItem "${sdkRoot}bin\$sdkVer\x64\signtool.exe","${sdkRoot}bin\$sdkToolsFallbackVer\x64\signtool.exe" -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
if (-not $signtool) { $signtool = "signtool" }

if (-not $SkipStopAppProcesses) {
    $lockPaths = if ($StopFileLockers) { @($appx) } else { @() }
    Stop-BuildBlockingProcesses `
        -PackageName $packageName `
        -RootPath $root `
        -PackageContentPath $pkg `
        -OutputPath $appx `
        -LockPaths $lockPaths
}

if ($StrictTargets -and ($SkipVersionManifests -or $SkipVersionCompat -or $script:BuildFailures.Count -gt 0)) {
    throw "Strict build requires every configured target before packaging"
}
if (-not $SkipVersionManifests -and -not $SkipVersionCompat) {
    & (Join-Path $root "scripts\validate-modern-version-matrix.ps1") -PackagePath $pkg
}
& $makeappx pack /d $pkg /p $appx /overwrite
if ($LASTEXITCODE -ne 0) { throw "MakeAppx failed" }
if ($StrictTargets) { & (Join-Path $root "scripts\validate-modern-version-matrix.ps1") -AppxPath $appx }

function Invoke-AppxSign {
    param(
        [Parameter(Mandatory = $true)]
        [string]$AppxPath,

        [Parameter(Mandatory = $true)]
        [string]$CertificateThumbprint,

        [Parameter(Mandatory = $true)]
        [string]$SigntoolPath
    )

    & $SigntoolPath sign /fd SHA256 /sha1 $CertificateThumbprint $AppxPath | Write-Host
    return ($LASTEXITCODE -eq 0)
}

$signingSucceeded = $false
foreach ($signingCert in $signingCertCandidates) {
    if (Invoke-AppxSign -AppxPath $appx -CertificateThumbprint $signingCert.Thumbprint -SigntoolPath $signtool) {
        $signingSucceeded = $true
        Write-Host "Signed appx with $($signingCert.Subject)"
        break
    }
}

if (-not $signingSucceeded) {
    throw "Appx signing failed with every certificate matching '$certName'. The certificate is present but signtool rejected it, so check that it has not expired and that its private key is readable. Nothing was deleted."
}
if (-not (Test-Path $appx)) { throw "Appx package was not created" }

if (-not $KeepStaging) {
    Remove-Item -Recurse -Force $pkg -ErrorAction SilentlyContinue
    Write-Host "Removed staging package directory"
}

Write-Host ""
Write-Phase "Done"
Write-Host "Package: $appx"

if ($script:BuildFailures.Count -gt 0) {
    Write-Host ""
    Write-Host "=== $($script:BuildFailures.Count) target(s) skipped ==="
    $script:BuildFailures | Format-Table Stage, Target, Reason -AutoSize -Wrap | Out-String -Width 200 | Write-Host
    if ($StrictTargets) {
        exit 1
    }
}
} finally {
    $buildLock.Dispose()
}
