# Changelog

Notable changes to Bandit Launcher. Nightly packages are numbered by build revision rather than by release, so entries here are dated.

## 2026-11-02 — Arreglado el bloqueo en 4020/4021 archivos

### Fixed

**El lanzamiento de NeoForge se quedaba en `4020/4021 files` y mostraba `game/libraries/com/google/guava/listenablefuture/9999.0-empty-to-avoid-conflict-with-guava/listenablefuture-9999.0-empty-...` con «Retrying in 10 seconds».** No era un problema de red: la URL responde `HTTP 200`, 2199 bytes y su SHA1 (`b421526c5f297295adef1c886e5246c39d4ac629`) coincide con el manifiesto. El culpable es la longitud de la ruta: ese nombre Maven mide 153 caracteres (el siguiente más largo, 123), y al anteponerle la carpeta `LocalState` de Xbox se supera el límite `MAX_PATH` de Win32, por lo que crear el `.download` y mover el archivo falla siempre.

- `build.ps1` empaqueta ese JAR vacío de 2 KB en `runtime\libraries\...` con SHA1 verificado.
- `MC.Xbox\launch\runtime_manager.cpp` ya no intenta descargar librerías que vienen en el paquete (`PackagedLibraryOverride`), usa un fichero temporal corto en `.download\<n>.part` y aplica rutas extendidas cuando la ruta completa se acerca a `MAX_PATH`.
- `MC.Xbox\common\win32_long_path.h` aporta `ToExtendedPath` / `ToSafeFilePath`; `EnsureDirectoryTree` y `CollectManifestLibraryJars` también entienden rutas extendidas.
- Test portátil nuevo: `tests\win32_long_path_test.cpp` (7 casos) en `tests\run-tests.sh`.

## 2026-11-02 — Release garantizada y recuperable desde el artefacto

### Fixed

**El primer build completo terminó el APPX pero falló al publicarlo.** El log del paso mostró `HTTP 403: Resource not accessible by integration` al llamar `gh release create` con `GITHUB_TOKEN` para un tag que aún no existía. `scripts\publish-release.ps1` ya no pasa `--target` al editar una Release existente (mover el tag no es necesario y el token de Actions no puede hacerlo), y reintenta hasta tres veces la creación en frío de un tag nuevo. La Release y el tag `nightly-native` ya existen, así que los builds siguientes usan la ruta `edit` + `upload --clobber`, verificada en CI.

### Added

**Red de seguridad para «siempre hay un APPX descargable».** Si el paso de publicación falla, el propio build escribe su `run id` en `docs/ci/.publish-trigger`; eso dispara `.github/workflows/publish-appx-artifact.yml`, que descarga el artefacto `.appx` ya subido por ese mismo run y lo adjunta a la Release en ~40 s, sin recompilar nada. Se puede usar también a mano poniendo cualquier `run id` en ese archivo. El workflow guarda su transcripción en `docs/ci/publish-log.md` para poder diagnosticar fallos sin descargar logs desde el exterior.

**Resultado:** <https://github.com/erjezuh/JavaUWP/releases/tag/nightly-native> con `BanditLauncherNative-neoforge.appx` (437 MB), su `.sha256` y el `.cer` correspondiente.

## 2026-11-02 — Build de APPX más rápido en CI

### Changed

**`native-neoforge.yml` now persists the incremental build cache.** `build.ps1` and every per-target mod script already write content-hash stamps for their outputs, but a fresh Actions runner started with an empty `staging/` and `clean.ps1 -Apply` removed `staging/build` — so every run regenerated 56 download manifests and recompiled roughly fifty Java mods, which is where the ~20 minutes went. The workflow now restores `staging/cache`, `staging/build`, `staging/certs` and `staging/notes` from `actions/cache` after the clean, and saves a new cache after the build. `staging/package` is not cached. The first build seeds the cache; subsequent builds are mostly stamp hits.

**Diagnostics.** `build.ps1` prints per-phase elapsed times via `Write-Phase`, so a slow build can be attributed instead of being one opaque step. `concurrency.cancel-in-progress` is now `false`, so a build about to publish a Release is no longer cancelled by the next push.

### Added

- `workflow_dispatch` input `fast_build` passes `-SkipVersionManifests -SkipVersionCompat` for a quick smoke build; such builds are never published as a Release.
- A commit message containing `[skip appx]` skips the APPX job for docs/preview-only pushes.

See `docs/BUILDING.md` for the full explanation.

## 2026-11-02 — APPX descargable en cada build

### Changed

**Every `native-neoforge` workflow run now publishes a downloadable Release.** Previously the build only attached the package to the workflow run and publishing a Release was opt-in (`publish_release=false`); workflow artifacts are not reachable from every client. The Release at tag `nightly-native` is created/updated automatically on every push to this branch and on manual dispatches, with `BanditLauncherNative-neoforge.appx`, its `.sha256` and the matching signing `.cer`. `publish_release=false` on a manual dispatch still skips it. The run summary prints the download URL. README and `docs/FORK-NATIVE-NEOFORGE.es.md` now point at the fork's own Release.

Note: the upstream license requires prior written permission from veroxsity / BanditVault to redistribute APPX packages. Publishing is done at the fork owner's explicit request and their responsibility.

## 2026-11-02 — Ergonomía del dashboard (UI)

### Changed

**The main menu now has a hierarchy.** Play is a large accent hero showing the active profile and target; the remaining actions are grouped under `Library` (Mods, Remote Files) and `Launcher` (Settings, What's new, Repair downloads, Sign out). A persistent header shows the account, a dark profile card keeps the active profile/target readable over the screenshot panorama, and a footer bar shows the controls that actually work on that screen. See `docs/UI-ERGONOMICS.es.md` for the analysis and before/after renders in `docs/previews/`.

**Settings is grouped** into Controls, Privacy & diagnostics and Content & updates; the privacy note uses the secondary type size so it no longer competes with the rows. Section labels and footer hints are shared renderer helpers (`DrawSectionLabel`, `DrawFooterBar`) driven by a new `layout` namespace of geometry constants.

**Sign out now takes two presses.** The first `A` shows a red confirmation on the row and in the footer; only a second `A` within four seconds (without moving focus) signs out. This prevents a stray D-pad press from clearing the Microsoft session and its entitlement check.

### Fixed

**The main menu no longer loses its context.** Account, profile name and target are now typed fields in `MainMenuSummary` instead of being squeezed into the generic `status`/`detail` strings; they are refreshed when returning from Mods, where the active profile can change. Settings and Remote Files now restore the previous status on exit instead of leaving `"Crash reporting is off"` / `"Remote file manager stopped"` on the dashboard. Navigation no longer clears the profile summary, and the obsolete `"Browse installed, popular, and latest Fabric mods."` copy (wrong for a NeoForge default) is gone.

**Cursor visibility.** The mouse crosshair now has a dark halo, a black outline and an accent centre dot, so it stays visible over both the dark dashboard and bright screenshots.

### Infrastructure

`scripts/gen-runtime-config.ps1` is the single source of `runtime_config.h` token substitution (`build.ps1` and the UI preview both call it). `.github/workflows/ui-preview.yml` renders every launcher screen to PNG on Windows, regenerates `MC.Xbox/lang/en.json` with the official translation script and commits a curated before/after set. `MC.Xbox/lang/es-419.json` was updated for the nine new strings.

## 2026-11-02 — Fork «Native + NeoForge»

### Added

**This fork builds the native keyboard/mouse package with NeoForge 1.21.1 as the default target.** It is based on upstream's `main-native` branch (direct USB mouse and keyboard, separate app identity), and the compiled default, `download_manifest.tsv`, the default per-version manifest and new profiles now all point at `1.21.1 / neoforge / 21.1.250` with the Java 21 runtime. The default target is configurable from `scripts/config.ps1` or `-DefaultLoader` / `-DefaultLoaderVersion` / `-DefaultLaunchVersion`, so any catalog row can be the default. See `docs/FORK-NATIVE-NEOFORGE.es.md`.

**Portable checks.** `tests/run-tests.sh` runs a C++ unit test for NeoForge version resolution and `scripts/validate-fork-targets.py`, which checks catalog/default consistency, `runtime_config.h` token substitution and NeoForge controller variant coverage. `.github/workflows/native-neoforge.yml` builds the native APPX from this branch and uploads it as a workflow artifact; publishing a Release is opt-in.

### Fixed

**NeoForge version resolution.** The launcher now uses the catalog's loader version (the one whose installer and artifacts were downloaded) and only falls back to parsing the installer's `version.json` id for legacy profiles. All known id shapes are accepted and Fabric/Forge ids are rejected instead of producing invalid Maven coordinates. `neoforge.cpp` now checks for the vanilla client jar, installer jar, installertools metadata and NeoForm archive up front and logs which input is missing.

**NeoForge manifest generation is resilient.** Missing `.sha1` sidecars are computed from the artifact, a single unpublished installer artifact no longer aborts a whole target manifest, the NeoForm coordinate is resolved from `version.json`, `minecraftArguments` or `install_profile.json`, and tool versions are looked up across all Maven groups NeoForge has used. Helper scripts (`gen-neoforge-artifacts.ps1`, the NeoForge controller mod build) no longer hardcode `21.1.233` while the catalog uses `21.1.250`. The securejarhandler UWP patch is built against the version the default target resolves, with a `3.0.8` fallback.

## 2026-11-01

### Fixed

**The mouse cursor should now line up at 1440p and 4K.** After the 1440p and 4K fix, the cursor could be drawn in one place while clicks landed near the top left of the screen. This also happened after switching fullscreen. The game and the cursor were using different screen sizes, and they now match. Tell us if it still happens, and include the line starting `Window size` from the shim log.

**1.12.2 no longer tries to download Linux libraries.** Preparing Minecraft 1.12.2 with Forge could stop on "Download failed after retries" at a Linux-only netty file the console never uses. Those files are no longer in the download list. If you still get a download error on 1.12.2, send the `mc_launch.log` from the launcher's log folder.

## 2026-10-31

### Added

**Minecraft 1.12.2 with Forge.** 1.12.2 is in the version list as an experimental target. It runs through Cleanroom, a version of Forge 1.12.2 that works on newer Java, so most Forge 1.12.2 mods load as they are. The controller has the same features as on newer versions: button guides, menu snapping, the radial menu, the controller settings on Back, and the console keyboard for chat and signs.

**Recommended mods for 1.12.2.** The Recommended tab now lists mods that were tested on Xbox with 1.12.2: Nothirium for faster chunk rendering, VintageFix, Universal Tweaks, Entity Culling, Had Enough Items, Xaero's Minimap and LemonSkin. OptiFine does not work on 1.12.2 in the launcher yet. It shows up in the mod list but does not change anything.

### Fixed

**The game fills the screen at 1440p and 4K.** The console used to report the same scale whatever the TV was set to, so a 1440p or 4K output showed a 1080p picture in one corner unless the scale was set by hand in `mesa_env.txt`. The launcher now reads the TV's output mode and sizes the game to match. A value set in `mesa_env.txt` still takes priority.

## 2026-10-30

### Fixed

**The controller cursor no longer hides behind modded menus.** Mods such as Essential draw their windows and popups after the game draws the menu, and the cursor used to be drawn before them, so it disappeared underneath. It is now drawn last, above the menu, any mod overlays and toasts. This applies to Fabric, Forge and NeoForge on every supported version.

## 2026-10-29

### Added

**Walking is analogue on Forge and NeoForge.** Push the left stick a little to walk slowly and all the way to run, the same as on Fabric.

**Keyboard Like Movement.** A new option in the controller settings makes the stick walk at full speed in set directions, like the keyboard. It is meant for servers that reject analogue movement, and it is available on Fabric, Forge and NeoForge for every Minecraft version.

**Show Button Guides.** A new option in the controller settings hides the button hints in menus and on the HUD.

**Latin American Spanish is included.** Choose Español (Latinoamérica) under Language in Settings. Thanks to Sumer Double P for the translation.

## 2026-10-25

### Added

**Latin American Spanish is picked automatically.** A Spanish console set to Mexico, Argentina, Colombia or another Latin American country uses a language file named `es-419` if there is one, and plain `es` otherwise.

**Translation files are checked when they are sent in.** A file that is not valid, has no name, or changes a `{0}` is flagged on the pull request. The same check runs locally with `scripts/update-translations.ps1 -Check`.

## 2026-10-24

### Added

**The launcher can be translated.** Settings has a new Language option, and each language is a plain text file in the `lang` folder. Anything a file does not translate stays in English. Every screen and message is covered, from sign in errors to pack imports, and the Remote Files page follows the chosen language too. The first time it runs, the launcher picks a language that matches the console's setting if one is available. No translations ship yet. See `docs/TRANSLATING.md` for how to add one, including how to try a file from Remote Files, which has a new Languages folder.

## 2026-10-23

### Added

**The launcher tells you when a new version is out.** When it opens, it checks GitHub for the newest version on the channel you installed. If there is a newer one, a notice shows what changed and a QR code for the release page. You can skip that version, or be reminded next time. Updating is still done from a PC, so nothing installs by itself. The check can be turned off in Settings.

**A What's new page on the main menu.** It shows the changelog without leaving the launcher. It reads the latest copy from GitHub, and falls back to the one that came with your version when you are offline.

## 2026-09-04

### Security

**The remote file server printed its access PIN into the log.** The file browser generates a six digit PIN per session, and that PIN was being written to the launch log, which is collected into crash zips. Anyone holding a crash zip who could also reach the console on the network while that same session was still running could have used it. The PIN is no longer logged, there is now a retry limit and a lockout, and it no longer falls back to a predictable value when the system random source is unavailable. The PIN is generated fresh each session, so there is nothing to change.

**Downloaded files were not always verified.** When a hash lookup failed, the error was swallowed and a blank hash was written into the download manifest, and a blank hash matched anything. Hash failures now stop the build instead.

**JAR signature checking was switched off in every shipped build.** A development setting disabling all signature algorithms had been left in the packaged Java configuration. Signature verification is back on.

**Authentication responses were parsed by scanning for text rather than by reading JSON.** The old scanner had no understanding of nesting or scope, and the entitlement check was a plain substring search. Both now parse properly.

**Raw server responses no longer end up in error messages.** Authentication failures were concatenating whole HTTP bodies into text that reached the log and then the crash zip, which could carry session material.

**The signing certificate password is no longer stored in plaintext**, and the build no longer picks an arbitrary code signing certificate from your store when the expected one is missing.

### Fixed

**1.21.4 could not launch at all.** It failed on startup with a duplicate ASM error, before a single mod loaded. Minecraft declares its own copy of ASM on 1.21.2 through 1.21.10, Fabric Loader declares a different one, and the launcher put both on the classpath. 1.21.4 was the only version in that range the launcher offered, so it was the only one anyone could run into. It now starts. The fix covers the whole range, for whenever the other versions are added.

**The version catalogue no longer lists targets with no controller support.** Sixteen entries were removed. They downloaded their full runtime during a build and had no controller variant behind them. Each will return as it gains real support.

**1.21.4 dropped to the idle framerate while playing with a controller.** Minecraft 1.21.2 added a framerate limiter that watches for keyboard and mouse activity. Controller input never reached it, so the game throttled itself mid-play. Already fixed on 1.21.11, now fixed on 1.21.4.

**The mod browser's target list ran off the bottom of the screen** once the catalogue grew past a handful of versions.

**Version numbers sorted incorrectly.** A prerelease such as `1.0-alpha` ranked above `1.0`, which could pick the wrong Forge artifact.

**A failed sign in and a first run were indistinguishable**, because a real credential store failure was being reported as an empty token.

**Bundled mods could be skipped** for a non default target when no version specific folder existed.

### Controller support on 1.20.1, 1.20.4, 1.21.1 and 1.21.4

These four shipped a reduced version of the controller mod. Everything below already worked on 1.21.11 and now works on all of them.

- **Radial menu.** Hold D-Pad Up, point with the right stick, release to run the bound action. Eight slots, each assignable to any Minecraft key binding.
- **On-screen button guide.** Context hints in menus and in the world, showing what each button does for whatever you are looking at.
- **Creative inventory.** The cursor now works on the creative screen, which it did not before.
- **Controller text entry.** The Xbox keyboard opens by itself for signs, chat, world names and server addresses, and typing updates the field as you go.
- **Full settings screen.** A Radial tab for assigning slots, and the Controls tab now runs past the controller actions into Minecraft's own key bindings, so any key can be placed on a controller input.
- **Swap Hands and the menu secondary action** were implemented but hidden. They can now be seen and rebound.
- **Analog movement.** The left stick was on or off. It is now proportional on every target.
- **Menu navigation** uses Minecraft's own navigation rather than simulated arrow keys, so focus moves correctly and no longer sticks inside a text box.
- Closing the on-screen keyboard no longer takes two button presses, and menu overlays no longer draw underneath the list behind them.

### Performance

**Logging was costing frame time.** Every log line was opening and closing its file, across four separate logger implementations. Those are now one shared logger that keeps the handle open.

**Garbage collection pauses and log churn during play have been cut**, and the watchdog no longer dumps every thread on a routine tick.

**The Mesa runtime folder was being looked up repeatedly at startup** and falling back to a legacy path each time.

### Known issue

Typing into the creative inventory search box with the controller keyboard closes the game on 1.20.1, 1.20.4 and 1.21.1. The on-screen keyboard is disabled for that one box on those three versions until the cause is found. Every other text field works, and 1.21.4 and newer are unaffected.

### Building from source

**Setup is one command.** `scripts\setup.ps1` now fetches the Mojang and Fabric metadata, client libraries, native DLLs, the Fabric installer and the asset index, then installs and patches the loader. Of the twelve prerequisites the docs used to list, only the Forge installer still has to be placed by hand, and only if you are building Forge.

**Builds are incremental.** The compatibility mod, the controller mod and the loader patches no longer wipe their output and rebuild from scratch every run. Mojang's version manifest was being fetched twenty nine times per build and is now fetched once and cached.

**Three duplicated logger classes, two duplicated settings classes and several copy pasted helpers** were replaced with shared modules.

**The GLFW shim compiles at a real warning level** rather than the lowest MSVC offers, and at the C++ standard it was written for.

**The Python asset generation step is gone.** It produced five static images from an icon that does not change, and needed a Python install with a hardcoded path.

**The controller mod build now fails if a target falls behind the newest one**, so features cannot silently go missing from a version again. An incremental build fault that skipped recompiling edited controller sources has also been fixed.

### Documentation

- `BUILDING.md` rewritten around the setup script, with the manual steps kept as an appendix for machines that cannot reach Mojang and Fabric.
- Minecraft `26.2` is now documented. It needs JDK 25, ships unobfuscated with no Fabric intermediary, and its mixins target Mojang names rather than intermediary ones.
- `THIRD-PARTY.md` added, listing every third party component with its upstream, version and licence. Vendored code now carries attribution.
- `COMPATIBLE MODS.md` renamed to `COMPATIBLE-MODS.md` so the filename has no space in it.
- Em-dashes, BOMs and trailing whitespace cleaned out, and `@author` tags corrected on three shipped mixins.

### Removed

Around 4 MB of unreferenced code and 10 MB of unused assets, including a stale patch file that no longer applied, duplicate splash screens, and several functions with no callers.
