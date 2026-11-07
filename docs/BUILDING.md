# Building

This page covers a clean local build from the repo root.

The build produces a signed UWP package at:

```text
output\BanditLauncherNative_<appx-version>.appx
```

## Requirements

Install these, then run one command. Everything else the build needs is downloaded for you by `scripts\setup.ps1`.

- Windows with PowerShell 5.1 or newer.
- Visual Studio or Visual Studio Build Tools with the MSVC x64 C++ tools.
- Windows 10 or Windows 11 SDK.
- Three JDKs. Set `JAVA_HOME`, `JAVA21_HOME` or `JDK21_HOME`, and `JAVA17_HOME` or `JDK17_HOME` if auto detection does not find them.
  - JDK 25 for the main build runtime and for the `26.2` target, which JDK 21 cannot read.
  - An exact JDK 21 install for the packaged Java 21 runtime.
  - An exact JDK 17 install for the Java 17 catalog targets. A normal complete build requires it.
- Desktop install of Git `https://git-scm.com/install/windows`

Mesa UWP runtime DLLs are already tracked in `mesa-runtime\`. Pass `-MesaRuntimeDir` if you want to build against a different one.

Forge and NeoForge preparation download their matching official installer automatically.

## Controlify bundle

`config\controlify-bundle.json` pins the Controlify JAR and its required YACL dependency for the default target, including Modrinth CDN URL, SHA1 and byte size. `build.ps1` validates all three with `Save-RemoteFile`, copies both JARs into `runtime\bundled-mods\` and into `runtime\version-mods\<default-target>\`, and `scripts\validate-modern-version-matrix.ps1` refuses to package an APPX without them (or with any `banditvault-*-controller-*.jar`). To change the Controlify version, edit only that JSON file with a matching Modrinth version id + hash; the portable validator checks that the pinned `targetId`, `loader` and `minecraftVersion` still match `scripts\config.ps1`.

The `controller_mod\` sources and their build scripts remain in the repository for reference but are no longer called by `build.ps1`. Commands that invoke them directly are legacy/manual only.

## Versions

Default versions live in `scripts/config.ps1`. The multi target catalog lives in `config\versions.tsv`. Recommended Modrinth project slugs live in `config\recommended-mods.json`, keyed by loader and Minecraft version. Every catalog target needs an entry, including an empty array when there are no recommendations yet.

Current defaults in this fork (set in `scripts/config.ps1`):

- Minecraft: `1.21.1`
- Asset index: `17`
- Default loader: `neoforge` `21.1.250` (launch version `neoforge-21.1.250`)
- Default runtime: `java21`, default controller provider: `neoforge`
- Fabric Loader (still built for other catalog targets): `0.19.5`
- Java release: `21`
- JNA: `5.17.0`

The default target is `1.21.1 + NeoForge 21.1.250`; upstream defaults to `1.21.11 + Fabric 0.19.5`. `build.ps1` fails early if the compiled default is not exactly one row of `config\versions.tsv`. See [the Spanish fork guide](FORK-NATIVE-NEOFORGE.es.md) for the full change list. The catalog includes every stable Minecraft version from 1.21 through 26.2 for Fabric and NeoForge. Forge is included for each of those versions except 1.21.2, where no Forge loader was published. These modern targets remain experimental until they have been tested on Xbox hardware.

Build every configured target and retain staged package contents with:

```powershell
.\build.ps1 -StrictTargets -KeepStaging
.\scripts\validate-modern-version-matrix.ps1
```

The validator checks the staged package against every catalog row. It does not build targets.

The main build accepts temporary overrides:

```powershell
.\build.ps1 -McVersion 1.21.11 -FabricLoader 0.19.5 -AssetIndex 29
```

These values are passed into setup helpers and into the generated `runtime_config.h` header used across `MC.Xbox/` host modules.

The `JavaRelease` value is the minimum Java version the scripts search for when compiling Java helpers and the compatibility mod. The package also copies a current JRE into `jre\` and an exact Java 21 runtime into `jre21\` so targets and mods can choose the runtime they need.

For changing the default target, update the `Default*` keys in `scripts/config.ps1` (or pass `-DefaultLoader`, `-DefaultLoaderVersion`, `-DefaultLaunchVersion` to `build.ps1` / set `DEFAULT_LOADER`, `DEFAULT_LOADER_VERSION`, `DEFAULT_LAUNCH_VERSION`, `DEFAULT_JAVA_RUNTIME`, `DEFAULT_CONTROLLER_PROVIDER`):

- `scripts/config.ps1` (`DefaultMinecraftVersion`, `DefaultLoader`, `DefaultLoaderVersion`, `DefaultLaunchVersion`, `DefaultJavaRuntime`, `DefaultControllerProvider`)
- `config/versions.tsv` if the target should appear in the launcher catalog (the row's `javaRuntime` and `controllerProvider` must match the config)
- `MinecraftVersion` / `MinecraftAssetIndex` for `setup.ps1`, natives and asset downloads
- `compat_mod/src/main/resources/fabric.mod.json` only if the compatibility mod metadata needs a new default range

Then recreate the local `gameDir`, natives, remapped jars, and patched loader files as needed. The installed app downloads official runtime libraries, client jars, and assets into UWP `LocalState`; they are not bundled into the APPX.

For adding another playable Fabric target, add it to `config\versions.tsv`, make sure the Fabric loader version can be patched, and let `build.ps1` generate the per target manifest and compatibility mod jar. Update `launch\loaders\fabric.cpp` only if the target needs loader specific classpath or JVM behavior beyond the generated manifest.

For adding another playable NeoForge target, the launcher needs matching NeoForge install metadata in the generated manifest and launch provider logic in `MC.Xbox\launch\loaders\neoforge.cpp` (dispatched through `launch\loaders\loader.cpp`). NeoForge generates patched client artifacts on first launch from downloaded official inputs. Do not commit or redistribute generated NeoForge client jars.

Forge has an experimental launch provider in `launch\loaders\forge.cpp`. Modern Forge targets from 1.21 through 26.2 use the shared modern controller source and thin version adapters under `controller_mod\neoforge\`. Forge 1.21.2 is not cataloged because no matching loader was published.

Forge build inputs that belong in the repo:

```text
config\forge-install-profile.json
```

`scripts\prepare-forge-patched-client.ps1` downloads the matching official Forge installer and generates the patched client in the local cache, including legacy 1.20.1. Controller compilation puts that patched client before the original SRG jar on its classpath. See [PATCHING.md](PATCHING.md) for controller mod details.

For adding another playable Forge target, add it to `config\versions.tsv`, extend `launch\loaders\forge.cpp`, and let `build.ps1` generate the manifest. No `controller_mod` output is produced; install Controlify from the Mods browser for that target.

### Minecraft 26.2

`26.2` is a calendar versioned target. It is cataloged as experimental. Loader versions are recorded in `config/versions.tsv`. Its Java and mapping requirements differ from earlier targets.

**It needs JDK 25.** Mojang declares Java 25 for `26.2` and its client classes are class file version 69, which JDK 21 refuses to read. `Resolve-JavaHomeForMinecraft` in `scripts\common.ps1` picks the higher of the version's declared Java major and the `JavaRelease` default, so a JDK 25 has to be installed to compile against this target or to remap for it. The catalog row records `javaRuntime=current`.

**The client ships unobfuscated.** `26.x` carries real class and method names, so Fabric publishes no intermediary mappings for it and never writes a remapped jar. `Resolve-FabricClientJar` falls through to `staging\cache\gameDir\versions\26.2\26.2.jar` and mods for this target compile straight against the client jar. The remapped jar step below does not apply to `26.2`.

**Mixin targets are written in Mojang names.** The `src\main` compatibility mixins target intermediary names such as `net.minecraft.class_4239` and `method_47525`. The `26.2` variants under `compat_mod\src\variants\26.2` target `net.minecraft.util.FileUtil` and `createDirectoriesSafe` instead. A mixin copied between the two will not apply.

**LWJGL is 3.4.1 and its natives are listed differently.** Mojang lists each platform's LWJGL natives for `26.2` as its own library entry sharing the base artifact's maven key. `Add-LibraryJar` in `scripts\setup.ps1` drops every `-natives-` entry for that reason: without the filter, `natives-linux` sorts first, wins the maven dedupe, and takes LWJGL's classes off the generated classpath.

## UWP host source layout

The UWP host is no longer a single `App.cpp` monolith. `MC.Xbox\App.cpp` keeps the UWP shell and `Run()` orchestration. Feature code lives in folders compiled by `build.ps1`:

```text
MC.Xbox\
  App.cpp
  common\
  net\
  auth\
  ui\
  mods\
  profiles\
  launch\
    runtime_manager.*
    minecraft_launch.*
    loaders\
      loader.*
      fabric.*
      neoforge.*
      forge.*
```

For module responsibilities, launch flow, and loader hook details, read [ARCHITECTURE.md](ARCHITECTURE.md).

## Mesa runtime

The repo includes the Mesa UWP runtime folder:

```text
mesa-runtime\
```

You can use a different runtime with:

```powershell
.\build.ps1 -MesaRuntimeDir "C:\path\to\mesa-runtime"
```

Or:

```powershell
$env:MESA_UWP_DIR = "C:\path\to\mesa-runtime"
```

`RETROARCH_UWP_DIR` is still accepted as a fallback search path. RetroArch is not required.

## Console targets

Builds target Xbox Series S and Series X only. Xbox One support was removed and is planned to return later as its own separate target, so the current build has no Xbox One graphics runtime path.

## Fresh setup

From the repo root:

```powershell
.\scripts\setup.ps1
```

That is the whole setup step. It downloads the Mojang and Fabric metadata, the client libraries, the Windows native DLLs, the Fabric installer, and the asset index, installs and patches the Fabric loader, and generates the local Fabric remapped client jar. Everything lands in ignored cache paths under `staging\cache`, so none of it enters the repository or the release package.

It takes several hundred megabytes on a cold cache, which is why `build.ps1` does not run it for you. Run it once on a clean machine, then run `.\build.ps1`.

To prepare a target other than the default:

```powershell
.\scripts\setup.ps1 -MinecraftVersion 1.20.1 -FabricLoaderVersion 0.19.5
```

`compat_mod\build_compat_mod.ps1` calls it itself when a target's client jar is missing, so per target preparation is usually automatic. `controller_mod\fabric\build_fabric_controller_mod.ps1` does the same but is no longer used by `build.ps1`.

If the setup script cannot run, see the manual fallback at the end of this page.

## Microsoft Sign In

The packaged app signs in dynamically. You no longer need to create or bundle a
local auth JSON file.

On first launch, the app shows a Microsoft device code screen before Minecraft
starts. Go to `https://www.microsoft.com/link`, enter the displayed code, and
sign in with the Microsoft account that owns Minecraft Java Edition. The QR code
on that screen opens the same Microsoft link flow.

After sign in, the app exchanges the Microsoft token through Xbox Live, XSTS,
and Minecraft Services, checks Java Edition ownership, and passes the resolved
Minecraft username, UUID, and access token into the embedded JVM.

## Runtime downloads

The APPX contains only launcher owned runtime pieces and intentionally patched
files:

```text
MC.Xbox.exe
jre\
natives\
graphics\
runtime\libraries\...\fabric-loader-<version>.jar
securejarhandler-uwp-patch.jar
runtime\bundled-mods\banditvault-xbox-compat-1.0.0.jar
runtime\bundled-mods\controlify-3.0.1+lts+1.21.1-neoforge.jar
runtime\bundled-mods\yet_another_config_lib_v3-3.8.2+1.21.1-neoforge.jar
runtime\version_catalog.tsv
runtime\recommended-mods.json
runtime\manifests\
runtime\version-mods\
runtime\log_configs\
download_manifest.tsv
```

It does not contain Mojang libraries, Minecraft client jars, Minecraft version
JSON files, asset indexes, asset objects, or Fabric remapped jars. During build,
`scripts\new-download-manifest.ps1` writes `download_manifest.tsv` from official
Mojang and Fabric metadata. During launch, after ownership verification,
`MC.Xbox.exe` (implemented mainly in `launch\runtime_manager.cpp`) verifies the manifest entries under `LocalState` and downloads any
missing or stale files.

Do not redistribute generated APPX packages without prior written permission. Nightly and pre release APPX packages are for testing only, are unsupported, and are not full game releases.

Do not publish public forks, mirrors, modified public copies, or builds that remove, bypass, disable, stub, fake, or make optional Microsoft/Xbox authentication or Minecraft entitlement checks.

Public video tutorials or other install guides for nightly or pre release APPX packages are not permitted until the full release.

Downloaded runtime files land under:

```text
LocalState\game\libraries
LocalState\game\versions
LocalState\assets
```

The next launch verifies hashes and skips files that are already present.

The launcher also writes a small manifest marker in `LocalState`. If the
packaged manifest changes, the app removes old downloaded official runtime
folders before verifying the new manifest. The signed in menu includes
`Repair downloads`, which forces the same cleanup and re download path for
corrupt or suspect local files.

Missing or stale files download with limited parallelism. The first stable
setting is six workers so the UI stays responsive and the Xbox storage path is
not flooded with thousands of simultaneous asset writes.

## Generate Fabric remapped jars

Setup remaps the client with TinyRemapper and waits for a successful process exit. It never starts Minecraft. Run `scripts\setup.ps1` for the selected target to prepare the compiler input.

After setup, these local build cache paths should exist:

```text
staging\cache\gameDir\libraries\net\fabricmc\fabric-loader\0.19.5\fabric-loader-0.19.5.jar
staging\cache\gameDir\.fabric\remappedJars\minecraft-1.21.1-0.19.5\client-intermediary.jar
staging\cache\natives-1.21.1\
```

If you ran `download-assets.ps1`, `staging\cache\assets\indexes\17.json` should also exist. Runtime assets are still downloaded by the installed app, not copied into the APPX.

If all of those are there, put launcher owned or explicitly allowed mod jars into `staging\cache\gameDir\mods`. The compatibility mod is generated there automatically by the build.

For non default Fabric targets, `compat_mod\build_compat_mod.ps1` will call `scripts\setup.ps1` for the requested Minecraft and loader version if the matching remapped client jar is missing.

`1.20.1` Fabric controller sources live under `controller_mod\fabric\src\variants\1.20.1\` because that target uses different intermediary mappings than the default controller sources.

Build the Forge controller mod directly with:

```powershell
.\controller_mod\forge\build_forge_controller_mod.ps1
```

Or build a specific Forge target into `runtime\version-mods\`:

```powershell
.\controller_mod\forge\build_forge_controller_mod.ps1 `
  -MinecraftVersion 1.20.1 `
  -ForgeVersion 1.20.1-47.4.20 `
  -OutputDir .\runtime\version-mods\1.20.1-forge-47.4.20
```

## Patch Fabric Loader

The top level build runs this step automatically. You can also run it directly:

```powershell
.\scripts\patch-fabric.ps1
```

The script overlays patched Fabric Loader classes into the local ignored loader JAR under `staging\cache\gameDir`.
The package step patches and copies each distinct Fabric loader version required by `config\versions.tsv`.

## Patch securejarhandler for NeoForge

The top level build also builds `securejarhandler-uwp-patch.jar` from sources under `patch\securejarhandler`. NeoForge uses securejarhandler and Java module layers to discover and load mods. The patch keeps that path working inside UWP by avoiding sandbox hostile file handling and by preserving access between the NeoForge and Minecraft modules.

This patch is packaged as launcher owned runtime content and applied with `--patch-module` during NeoForge launches.

## Build package

Run:

```powershell
.\build.ps1
```

## Nightly Workflow

The GitHub Actions workflow in `.github/workflows/nightly.yml` builds and publishes the moving `nightly-native` release when relevant source, build, runtime, or workflow files change on `main-native`. Documentation only changes such as README updates do not trigger a package. This fork additionally ships `.github/workflows/native-neoforge.yml`, which builds the native package from the fork branch, attaches it to the run and **publishes the `nightly-native` Release automatically on every build** with `BanditLauncherNative-neoforge.appx`, its `.sha256` and the matching `.cer`. The upstream license requires written permission from veroxsity / BanditVault to redistribute APPX packages; this fork publishes at its owner's request.

### Why a cold build was slow, and how CI avoids it

Measured on run `41446496020`: the build took 2,317.2 s (39 min), of which 1,633.7 s (27 min, 70.5%) was the "Per-version compat and controller mods" phase. That phase compiled a separate Bandit controller mod for 51 catalog targets, running the full NeoForge installer once per version. This fork no longer compiles any Bandit controller: Controlify is downloaded once from Modrinth with pinned hashes. A cold build now spends its time on manifests, the UWP compat mods and the Java patches (~11 min), and a warm cached build is faster still.

`build.ps1` generates one download manifest for each of the 56 rows in `config/versions.tsv` and compiles the UWP compatibility mods for the Fabric targets. Every one of those outputs, plus the Java UWP patches and `MC.Xbox.exe`, already has a content-hash stamp (`New-BuildStamp` / `Test-BuildStampCurrent`) in `staging\build` or `staging\cache\manifests`. On a fresh Actions runner those directories are empty, so all of it is rebuilt: that is the long cold build.

**Package versioning.** `build.ps1` derives the APPX version from `-AppxVersion`, then from `GITHUB_RUN_NUMBER` on CI (`1.0.<run>.0`), and finally from `.local\app_build.txt` for local builds. The CI branch exists because a clean runner has no `.local\app_build.txt`: without it every nightly would be stamped `1.0.0.0`, and Windows/Xbox refuses an `Add` operation for an identity+version that is already registered (`0x80070005`). Always install the `.cer` from the same Release as the `.appx`.

`native-neoforge.yml` restores `staging/cache`, `staging/build`, `staging/certs` and `staging/notes` from `actions/cache` **after** `clean.ps1 -Apply` (which deletes `staging/build`, `staging/package`, `staging/certs` and `output`), and saves a fresh cache after the build. `staging/package` is deliberately not cached because it is the final package content. With a warm cache the stamps short-circuit the downloads and the ~50 Java compiles; only files whose inputs actually changed are rebuilt. `Write-Phase` in `build.ps1` prints per-phase elapsed times so a slow build can be attributed.

- The **first** build after enabling the cache is still cold (it creates the cache). Every build after that is warm.
- `workflow_dispatch` input `fast_build=true` passes `-SkipVersionManifests -SkipVersionCompat` for a quick smoke build; such a build is never published as a Release.
- A commit message containing `[skip appx]` skips the APPX job entirely for docs/preview-only pushes.
- The workflow uses `cancel-in-progress: false`, so a build that is about to publish is not killed by the next push. New runs queue and then benefit from the warm cache.

### If the Release step fails

The APPX artifact is uploaded *before* the Release step, so a publish failure never loses the package. `native-neoforge.yml` writes the failing run id to `docs/ci/.publish-trigger`; that push triggers `publish-appx-artifact.yml`, which downloads the artifact from that run, creates or edits `nightly-native` and uploads the `.appx`, `.sha256` and `.cer` with `--clobber` in under a minute. You can invoke the same recovery manually by putting any successful build's run id in that file and pushing it. The workflow commits its `gh` transcript to `docs/ci/publish-log.md`.

The first cold build hit `HTTP 403: Resource not accessible by integration` on `gh release create` for a brand-new tag. The tag and Release now exist, so normal builds take the `edit` path. `scripts\publish-release.ps1` no longer passes `--target` when editing (moving the tag is unnecessary and blocked for the Actions token) and retries cold creation three times.

The workflow publishes `BanditLauncherNative-nightly.appx`, `BanditLauncherNative-nightly.sha256`, and the matching public certificate to the `nightly-native` release. It moves the `nightly-native` tag to the commit that produced the package.

Nightly releases are experimental, unsupported, and not full game releases. The generated release notes also remind users that APPX redistribution, public install tutorials, public mirrors, public modified copies, and auth bypass builds are not permitted before the full release.

Useful options:

- `-KeepStaging` keeps `staging\package` after packaging.
- `-SkipStopAppProcesses` skips the process cleanup that runs before packaging.
- `-StopFileLockers` asks Windows Restart Manager to find processes that lock the output appx.
- `-MesaRuntimeDir` points at another Mesa runtime folder.
- `-McVersion`, `-FabricLoader`, and `-AssetIndex` override version values for this build.
- `-DefaultLoader`, `-DefaultLoaderVersion`, `-DefaultLaunchVersion` choose the compiled default launch target (any catalog row).
- `bash tests/run-tests.sh` runs the portable C++ and catalog/config consistency checks.
- `-AppxVersion` sets the package identity version explicitly.
- `-SkipVersionManifests` skips extra per target manifest generation while testing.
- `-SkipVersionCompat` skips extra per target compatibility mod builds while testing.

The build script:

1. Generates `runtime_config.h` for the selected versions.
2. Builds `MC.Xbox.exe`.
3. Builds the UWP GLFW shim.
4. Builds the compatibility mod.
5. Patches the local Fabric Loader JAR and builds the securejarhandler UWP patch.
6. Assembles `staging\package`.
7. Copies the version catalog.
8. Copies patched Fabric Loader jars, patched TinyRemapper for legacy Fabric, the securejarhandler patch, bundled mods, log config, natives, Mesa/MobileGlues graphics DLLs, and the JREs.
9. Generates `download_manifest.tsv` for the configured default target (NeoForge 1.21.1 in this fork).
10. Generates `runtime\manifests\<target-id>.tsv` for cataloged Fabric, Forge, and NeoForge targets.
11. Builds the per-target UWP compatibility mod jars for Fabric targets under `runtime\version-mods`. No Bandit controller mod is compiled; `config\controlify-bundle.json` pins Controlify + YACL instead.
12. Generates UWP tile and splash assets from `MC.Xbox\Assets\Java_UWP_Icon.png`.
13. Creates and signs `output\BanditLauncherNative_<appx-version>.appx`.
14. Deletes `staging\package` unless `-KeepStaging` is set.

## Clean outputs

Preview cleanup:

```powershell
.\scripts\clean.ps1
```

Apply cleanup:

```powershell
.\scripts\clean.ps1 -Apply
```

Default cleanup removes build outputs only:

```text
staging\build
staging\package
staging\certs
output
```

To include all ignored files, including downloaded cache files:

```powershell
.\scripts\clean.ps1 -Scope AllIgnored
.\scripts\clean.ps1 -Scope AllIgnored -Apply
```

## Troubleshooting

- `No suitable Java installation found`: set `JAVA_HOME` to a JDK 21 or newer install.
- `No exact Java 21 installation found`: set `JAVA21_HOME` or `JDK21_HOME` to a JDK 21 install.
- `vswhere.exe not found`: install Visual Studio Build Tools with C++ tools.
- `Mesa UWP runtime DLLs not found`: pass `-MesaRuntimeDir`, set `MESA_UWP_DIR`, or restore `mesa-runtime\`.
- Missing `client-intermediary.jar`: run `.\scripts\setup.ps1` for that target.
- Forge preparation failure: check the official installer download and Java tool error from `scripts\prepare-forge-patched-client.ps1`. The helper downloads the matching installer automatically.
- Forge controller compile failure: ensure the patched Forge client exists in the local cache and that `config\forge-install-profile.json` is present.
- Missing native DLLs: run `.\scripts\setup.ps1`, which downloads them into `staging\cache\natives-1.21.1`.
- First launch downloads every required official file after sign in. A later launch should verify and skip files that are already downloaded.
- Runtime download failure: check `LocalState\logs\current\mc_launch.log` for the manifest path, URL, HTTP status, or SHA1 mismatch.
- Stuck at `4020/4021 files` on `listenablefuture-9999.0-empty-to-avoid-conflict-with-guava.jar`: this is the Guava placeholder, an empty 2 KB jar whose Maven path is 153 characters (30 longer than any other entry). With the Xbox `LocalState` prefix the full Win32 path exceeds `MAX_PATH`, so creating the temp file and moving the finished download fails and the launcher retries every 10 seconds. The URL itself is fine (HTTP 200, 2199 bytes, SHA1 `b421526c5f297295adef1c886e5246c39d4ac629`). The package now ships this jar under `runtime\libraries\...` and `EnsureRuntimeDownloads` treats packaged libraries as already present; the downloader also stages files in a short `.download` directory and uses extended-length `\\?\` paths when a full path approaches `MAX_PATH`. See `MC.Xbox\common\win32_long_path.h`.
- Modrinth browse/install failure: check `LocalState\logs\current\mc_launch.log` for `Modrinth search`, `Modrinth versions`, HTTP status, download, or SHA1 verification messages.
- Package signing failure: check that the signing certificate subject matches the manifest publisher. The native build uses `MC_DevMode_Edge_BanditLauncher.pfx` under the ignored certificate directory.
- If you can't find your appdata folder, type `%appdata%` into your address bar in your file explorer.

## Local cache setup

Run `scripts\setup.ps1` for the requested Minecraft and Fabric loader versions. It downloads official inputs and remaps the client directly with TinyRemapper. Preparation does not start Minecraft or use account credentials.

The client, libraries, versioned Fabric installer and remapped outputs stay under `staging\cache`. Native DLL caches are separated by Minecraft version. A remapped client is reused only when its ZIP and required client class are valid. Failed preparation keeps the previous output.

An offline build needs complete validated inputs already in the matching cache. Missing official version metadata or missing inputs stop preparation.
