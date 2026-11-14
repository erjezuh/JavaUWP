# Shared project settings. Keep version-sensitive values here so build and
# setup scripts do not drift apart.
#
# Version-bound values (MinecraftVersion, MinecraftAssetIndex,
# FabricLoaderVersion) can be overridden via environment variables so that
# build.ps1 can target a different MC version without anyone editing this file.
# The env var names match the config keys, uppercased and snake_cased:
#   MC_VERSION              -> MinecraftVersion
#   MC_ASSET_INDEX          -> MinecraftAssetIndex
#   FABRIC_LOADER_VERSION   -> FabricLoaderVersion
#
# This fork's default launch target is NeoForge 1.21.1 instead of Fabric. The
# Default* keys below are compiled into the launcher (runtime_config.h) and are
# also used to generate download_manifest.tsv and runtime/version-mods/<id>.
# Every one of them can be overridden from build.ps1 or the environment, so any
# other catalog row (including any other NeoForge version) can be the default:
#   DEFAULT_MC_VERSION, DEFAULT_LOADER, DEFAULT_LOADER_VERSION,
#   DEFAULT_LAUNCH_VERSION, DEFAULT_CONTROLLER_PROVIDER,
#   DEFAULT_JAVA_RUNTIME, DEFAULT_SUPPORT_LEVEL

$defaultMcVersion = if ($env:DEFAULT_MC_VERSION) { $env:DEFAULT_MC_VERSION } elseif ($env:MC_VERSION) { $env:MC_VERSION } else { "1.21.1" }
$buildMcVersion   = if ($env:MC_VERSION) { $env:MC_VERSION } elseif ($env:DEFAULT_MC_VERSION) { $env:DEFAULT_MC_VERSION } else { "1.21.1" }

$ProjectConfig = [ordered]@{
    DefaultMinecraftVersion  = $defaultMcVersion
    DefaultLoader            = if ($env:DEFAULT_LOADER)        { $env:DEFAULT_LOADER.ToLowerInvariant() } else { "neoforge" }
    DefaultLoaderVersion     = if ($env:DEFAULT_LOADER_VERSION) { $env:DEFAULT_LOADER_VERSION } else { "21.1.250" }
    DefaultLaunchVersion     = if ($env:DEFAULT_LAUNCH_VERSION) { $env:DEFAULT_LAUNCH_VERSION } else { "neoforge-21.1.250" }
    DefaultControllerProvider = if ($env:DEFAULT_CONTROLLER_PROVIDER) { $env:DEFAULT_CONTROLLER_PROVIDER } else { "none" }
    DefaultJavaRuntime       = if ($env:DEFAULT_JAVA_RUNTIME)  { $env:DEFAULT_JAVA_RUNTIME }  else { "java21" }
    DefaultSupportLevel      = if ($env:DEFAULT_SUPPORT_LEVEL) { $env:DEFAULT_SUPPORT_LEVEL } else { "experimental" }
    VersionCatalog           = if ($env:VERSION_CATALOG)       { $env:VERSION_CATALOG }       else { "config/versions.tsv" }
    MinecraftVersion         = $buildMcVersion
    MinecraftAssetIndex      = if ($env:MC_ASSET_INDEX)        { $env:MC_ASSET_INDEX }        else { "17" }
    FabricLoaderVersion      = if ($env:FABRIC_LOADER_VERSION) { $env:FABRIC_LOADER_VERSION } else { "0.19.5" }
    MixinVersion             = "0.17.4+mixin.0.8.7"
    JnaVersion               = "5.17.0"
    JavaRelease              = 21
    CompatModId              = "banditvault-xbox-compat"
    CompatModVersion         = "1.0.0"
    StagingDir               = "staging"
    CacheDir                 = "staging/cache"
    BuildDir                 = "staging/build"
    OutputDir                = "output"
    GameDir                  = "staging/cache/gameDir"
    AssetsDir                = "staging/cache/assets"
    NativesDir               = "staging/cache/natives-" + $buildMcVersion
    MesaRuntimeDir           = "mesa-runtime"
    ToolsDir                 = "staging/cache/tools"
    NotesDir                 = "staging/notes"
    MetadataCacheDir         = "staging/cache/metadata"
    PackageContentDir        = "staging/package"
    CertificateDir           = "staging/certs"
    CertificateFileName      = "MC_DevMode_Edge_BanditLauncher.pfx"
    # throwaway password for a local dev-mode self-signed cert, the pfx itself is gitignored
    CertificatePassword      = if ($env:APPX_CERT_PASSWORD) { $env:APPX_CERT_PASSWORD } else { "devmode" }
}
