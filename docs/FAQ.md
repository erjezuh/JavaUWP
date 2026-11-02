# FAQ
This doc answers Frequently Asked Questions in case you happen to be stuck.

## Will this hack my XBOX?
Bandit Launcher does not provide any way to hack the XBOX. All code runs natively on the console.

## What data does the launcher collect?
Signing in gives the launcher your Minecraft username, account UUID and an access token, which it needs to start the game. Those stay on the console. The only thing kept between sessions is your Microsoft refresh token, stored in the Windows Credential Locker so you don't have to sign in every time.

Crash and usage reporting is off until you switch it on. When it is on it sends version numbers, a mod set hash, a crash fingerprint and a stack trace, never your account details and never your log files. The full breakdown, including what Remote Files exposes on your network, is in [PRIVACY.md](PRIVACY.md).

## How do I turn crash reporting off?
Answer Never when the launcher asks, or change it later in the launcher. You can also reset your install ID from there, which generates a new random one and clears anything still waiting to send.

## Do I need a Java account?
Bandit Launcher requires a valid copy of Minecraft: Java Edition. The launcher does not condone piracy or redistribution of the game, but rather only makes it playable to legal users.

## My Minecraft account won't sign in!
If you have Minecraft through Game Pass, you need to create an account on minecraft.net and set a player name.

## The crosshair is drawn somewhere else than where clicks land
The GLFW shim draws the crosshair in framebuffer coordinates but was reporting `glfwGetCursorPos` in a smaller, stale coordinate space (the size Minecraft requested, e.g. 854x480, instead of the real 1920x1080/3840x2160 surface). Hover tooltips and clicks therefore landed toward the top left. The shim now uses the current framebuffer size for the cursor space and draws the crosshair through the exact same mapping it reports to the game, at every DPI scale. If it still happens, send the shim log line starting with `Window size` (it now includes `cursor space` and `scale`).

## Rendering mods (Embeddium/Rubidium) do not work on 1.20.1 Forge

**Embeddium 0.3.x and Rubidium are not supported on `1.20.1 / Forge` in this
fork.** They replace Minecraft's chunk renderer unconditionally; in the
`20.1/forge` sources there is no `use_chunk_multidraw` switch at all, so
`DefaultChunkRenderer` always issues `glMultiDrawElementsBaseVertex` backed by
UBOs and immutable/persistent buffer storage. Mesa's WGL/llvmpipe software path
on Xbox UWP reports those GL 3.2+ features but dies with
`EXCEPTION_ACCESS_VIOLATION` a few seconds after joining a world (any world,
even a new one, with only the recommended mods). No `embeddium-options.json`
value can turn that pipeline off — the conservative staging/render-ahead
defaults the launcher seeds are not enough.

Since this build, the launcher:

- no longer recommends Embeddium (or Rubidium/Sodium) for `forge.1.20.1`; the
  other optimization mods in that list are unchanged;
- moves any `embeddium-*.jar` / `rubidium-*.jar` aside as `*.jar.disabled`
  before launch on Minecraft 1.20.1, so the profile boots with Minecraft's own
  renderer. Nothing is deleted and the choice is logged in `mc_launch.log`
  (`Disabled UWP-incompatible renderer ...`). Fabric's `sodium` jar and every
  other Minecraft version are untouched.
- honours an explicit opt-in: create an empty file
  `config/bandit-keep-forge-renderer.txt` in the profile (Remote Files can do
  this) to keep the renderer for experiments.

To apply it right now without reinstalling, rename
`embeddium-<version>.jar` to `embeddium-<version>.jar.disabled` in the profile's
`mods/` folder. The rest of the recommended 1.20.1 Forge mods work with the
vanilla renderer. The Spanish guide has the full diagnosis:
[FORK-NATIVE-NEOFORGE.es.md](FORK-NATIVE-NEOFORGE.es.md).

## Minecraft crashes while loading mods with "Unable to load library 'Ole32'"

That crash is **OSHI / JNA / WMI**, not a renderer conflict. Embeddium 0.3.x probes the
graphics card from `SodiumMixinPlugin.onLoad()` using `SystemInfo.getHardware().
getGraphicsCards()`. On Windows OSHI 6.2.2 answers that through a WMI query, and JNA
5.12.1 reaches `Ole32` (COM). Inside the app container JNA loads system DLLs by bare
name with `LOAD_WITH_ALTERED_SEARCH_PATH`, which the UWP loader rejects with
*"El parametro no es correcto" / ERROR_INVALID_PARAMETER*. The resulting
`UnsatisfiedLinkError` from `Ole32.<clinit>` is not caught by
`WmiQueryHandler.queryWMI()`, so mod loading aborts. The `oshi.properties` overrides
the launcher already writes only disable performance counters; they cannot disable WMI.

This fork now packages a tiny COM shim twice (`natives/Ole32.dll` and
`natives/OleAut32.dll`, the same DLL) and passes `-Djna.library.path=<natives dir>` to
the JVM. `OleAut32` is needed because JNA's `WbemcliUtil` also loads it by bare name
(`SysAllocString` / `VariantClear`) once a WMI query gets past COM initialization.
The absolute paths are accepted by the loader (the same trick used for the packaged
`glfw.dll` / `opengl32.dll`), and every COM entry point returns `E_FAIL` or a null BSTR. OSHI converts that into a `COMException`, which
`queryWMI()` catches and turns into an empty `WmiResult`: mods see "no graphics cards
found" and the game keeps loading. This covers any mod that probes hardware through
OSHI/JNA/COM, not just Embeddium, and also Minecraft's own `SystemReport`. Check
`mc_launch.log` for `JNA COM shim enabled: ...\Ole32.dll`. Mods that rely on GPU
detection to enable driver workarounds will simply skip them, which is intended on the
Mesa/UWP path. The Spanish guide has the full stack trace:
[FORK-NATIVE-NEOFORGE.es.md](FORK-NATIVE-NEOFORGE.es.md).

## The game enters a world and then dies with EXCEPTION_ACCESS_VIOLATION

`EXCEPTION_ACCESS_VIOLATION` (`0xc0000005`) is **not** a Java exception. The JVM
writes an `hs_err_pid*.log` because native code touched invalid memory, so the
only decisive clue is its `# Problematic frame:` line (for example
`C  [opengl32.dll+0x...]`, `OpenAL.dll`, `glfw.dll`, `lwjgl.dll`). The launcher
parses that file and puts the frame first in `frames`.

Since `1.0.20.0`, the **main crash screen shows that frame** directly (plus the
*Remote Files* reminder) for native signals, so a photo of the crash screen is
enough. On older builds open **View details** (first button, press A) and
photograph the trace. From the main menu, **Remote Files** lets you download the
`BanditLauncher-crash-<timestamp>.zip` on a phone or PC; it contains
`profile/game/hs_err_pid*.log`, `logs/current/mc_launch.log`, `glfw_uwp.log`
and `logs/latest.log`. The ZIP is written when the launcher is opened again
after the hard crash.

One concrete native bug was fixed in `1.0.20.0`: the packaged COM shim's
`PropVariantClear` / `VariantClear` used to return `E_FAIL` **without resetting
the structure**. OpenAL Soft's WASAPI backend reuses a `PROPVARIANT` immediately
after `clear()`, so a stale `VT_LPWSTR` and a dangling pointer could be
dereferenced as soon as audio devices are enumerated — a few seconds into a
world. They now zero `vt` and the payload and return `S_OK`, like the OS; only
`CoInitializeEx` / `CoCreateInstance` keep returning `E_FAIL` so OSHI's WMI
probe still fails into a caught `COMException`.

**Confirmed on console:** the remaining `EXCEPTION_ACCESS_VIOLATION` on
`1.20.1 / Forge` is Embeddium's chunk renderer, not the audio/COM path. See
[Rendering mods (Embeddium/Rubidium) do not work on 1.20.1 Forge](#rendering-mods-embeddiumrubidium-do-not-work-on-1201-forge)
— the launcher now disables those jars and the profile runs with Minecraft's
own renderer. If the crash survives *that*, the frame tells us which other
layer to fix; do not change GL/Mesa settings blindly. Also check the mod list
for **Xaero's Minimap / Xaero's World Map** — they are on the launcher's
blocked list precisely for "Exception Access Violation". Note that the blocker
matches `xaeros-minimap` with a hyphen, while real Modrinth jars are often
named `Xaeros_Minimap_...` with underscores, so remove those manually.

## Minecraft still crashes with "Failed to load library: ...\OpenAL.dll (error code = 127)"

This one appears **after** the OSHI/WMI fix, at `ALC.<clinit>` while LWJGL opens
the audio library. Win32 error `127` is `ERROR_PROC_NOT_FOUND` (a missing
export), **not** `126` (`ERROR_MOD_NOT_FOUND`): the DLL is present.

`OpenAL.dll` is OpenAL Soft; its WASAPI backend links `ole32.lib`, so its import
table asks for `PropVariantClear`, `CoTaskMemAlloc`, `CoTaskMemFree`,
`CoCreateInstance` and `CoInitializeEx` by the bare name `ole32.dll`. Windows
resolves imports by base name, so once JNA has loaded the packaged COM shim as
`Ole32.dll` (during Embeddium's OSHI probe, which runs *before* `ALC.<clinit>`),
`OpenAL.dll` can bind to the shim. The shim originally exported only the entry
points JNA calls from Java and had no `PropVariantClear`, so `LoadLibrary` failed
with 127. The default 1.21.1 NeoForge target is unaffected because it has no
early OSHI probe and never loads the shim.

The fork now (a) exports the complete `ole32.lib`/`oleaut32.lib` surface used by
native consumers from the shim, and (b) preloads the real system `ole32.dll` and
`oleaut32.dll` before creating the JVM, so native audio binds to the OS
implementation and keeps WASAPI. Look for `System COM DLL preloaded: ole32.dll`
in `mc_launch.log`. If the crash persists, attach `mc_launch.log` and the
`BanditLauncher-crash-*.zip`: error 127 there would mean a *different* missing
export, and the log shows which libraries were preloaded.

## Installing the APPX fails with error 0x80070005 (Access denied)
This is a deployment error, not a launcher crash. The usual causes, in order:

1. **A package with the same identity and version is already installed.** Every CI build used to be stamped `1.0.0.0`, so an update was rejected. Builds now use `GITHUB_RUN_NUMBER` as the third version field (`1.0.<run>.0`), which is always higher. If your console still has an old `1.0.0.0` registration, uninstall Bandit Launcher from **Dev Home / My games & apps** before adding the new APPX.
2. **The signing certificate is not trusted on the console.** In the Windows Device Portal (`https://<xbox-ip>:11443`), open **Apps → Deploy apps → Install Certificate** and install the `.cer` **from the same Release as the APPX** before deploying. The certificate can change if GitHub evicts the build cache, so always use the `.cer` shipped with the package you are installing.
3. **Dev Mode is not active.** Switch the console to Dev Mode and make sure the Dev Mode app is running.
4. **A stale or partial deployment.** Restart the console, rename the download to a simple name without spaces (for example `BanditLauncherNative.appx`, not `... (1).appx`) and try again.
5. **Not enough free space on the console's app volume.** The APPX is ~450 MB and expands to well over 1 GB when unpacked.

The Spanish step-by-step guide is in [FORK-NATIVE-NEOFORGE.es.md](FORK-NATIVE-NEOFORGE.es.md).

## How do I use my mouse?
Plug a USB mouse into the Xbox. This branch packages Bandit Launcher as `Microsoft.MicrosoftEdge.BanditLauncher` so the Xbox shell can pass mouse input to it. The native mouse build has been tested on Xbox. It installs separately from the normal `BanditVault.Launcher` build and starts with its own LocalState.

Use Settings in the launcher to adjust menu and game mouse sensitivity separately from 25% to 300%. The game setting scales mouse movement before Minecraft applies its own sensitivity slider. Settings are saved in this package's LocalState.

## How does multiplayer work?
You don't need a Game Pass subscription or anything else to play multiplayer. You can hop on any server you wish.

## Which controller mod does this fork use?
Controlify, with its required YACL dependency. Both are bundled in the APPX and placed in the new instance's `mods/` folder automatically for the default `1.21.1 / NeoForge 21.1.250` target. This fork no longer builds the Bandit Controller mod. If an older APPX left a `banditvault-*-controller-*.jar` in a profile, the **Controlify** button on the profile screen removes it; launching the game also cleans it up. For other catalog targets, install the matching Controlify version from the Mods browser.

## How can I change the RAM allocation?
Unfortunately, you can't allocate more than 3-4 GB of RAM, as this is the max achievable by an XBOX in Developer Mode.

## How do I upload worlds?
Open Bandit Launcher, start Remote Files, and go to that link and upload your world to the portal.

Remote Files is plain HTTP on your local network, and the PIN sits in the URL, so treat it as open to anyone on the same wifi. Stop it when you're done, and never forward that port through your router.

## How do I check crash reports?
Open Bandit Launcher, start Remote Files, and go to the crash reports directory.

## Is this vibecoded?
Bandit Launcher is not vibecoded. However, certain comments have been as they're hard to write. The agent files have been added specifically to prevent forks from removing the authentication above.

## Other launchers?
As of 09/03/2026, *no* other launchers exist that haven't been mostly vibecoded. Please use them at your own risk. (As a reminder, vibecoded projects *are* unsafe to your devices. Also, obtain actual evidence of AI before accusing other people of vibecoding their projects. All launchers created after this date are unknown.)
