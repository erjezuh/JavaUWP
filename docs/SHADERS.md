# Shaders in the UWP runtime

UWP is not desktop Windows. Denied WMI/Ole32, D3DKMT and HID calls are not proof of
an out-of-date Xbox GPU driver, and running the build shell as administrator does
not make those APIs available to the installed app.

The launcher keeps Iris/Sodium and shader packs enabled. It avoids unsupported
diagnostic/native probes on Fabric and NeoForge 1.21.1, prepares writable
profile-local temporary storage, and leaves native memory headroom for the Mesa/D3D12 rendering runtime.
There is no global OpenGL-version override and no fake native DLL.

## After updating the APPX

1. Fully close the old app and install the newly built APPX, keeping the existing
   app data. There is no need to delete worlds, mod jars or shader packs.
2. Open the same profile and check `logs\current\mc_launch.log` for:
   - `Writable native/Java temporary directory` under that profile's LocalState path.
   - `JVM memory budget` with `shaderLoader=1` when Iris/Oculus/OptiFine is installed.
   - `Effective JVM memory`: at most 3072 MB heap for a shader profile, often less
     after subtracting the existing host usage from the app's limit.
   - JNA/LWJGL arguments pointing at the selected target's native directory, not a
     system temp directory or a different Minecraft version's natives.
3. On NeoForge 1.21.1, check `mc_launch.log` for
   `NeoForge 1.21.1 early UWP native guards enabled`, then Java stderr/output for
   `[banditvault] UWP native guard:` followed by `GraphicsAdapterProbe`,
   `WindowsHardwareAbstractionLayer` or `SystemReport`, and the Controlify classes
   when installed. These class-definition guards also cover the early Sodium
   graphics bootstrap, before Mixin. A `no supported signatures` message means
   that class/version was **not** patched; include it in the bug report.
   On Fabric, `xbox_compat.log` in the profile should report
   `Skipping Sodium desktop adapter probe` if Sodium is installed. This guard
   applies to both the Sodium 0.5 and 0.6+ package names. GPU inventory may be empty;
   actual rendering still obtains its capabilities from Mesa/OpenGL.
4. If using Controlify 2.x, turn **Bandit Controller off** for that profile. Its
   Fabric compatibility messages should mention the UWP GLFW fallback; on NeoForge,
   look for native-guard messages for `Controlify`, `SDLNativesLoader` (or the older
   `SDL3NativesManager`) and `ControllerHIDService`. Neither should try to
   download/load SDL3 or start a hidapi scan. SDL-specific functionality is
   unavailable. Controlify 3.x removed GLFW support; use Bandit Controller instead.
5. Start a world with shaders off, enable the original shader pack, reload it,
   return to the menu and reload the world. Check both keyboard/mouse and gamepad
   input. Also test a profile without Sodium or Controlify: optional guard targets
   must not become required dependencies.

## NeoForge 1.21.1 scope

The focused [build workflow](BUILDING.md#only-minecraft-1211--neoforge) does not
bundle Fabric compatibility mods. Its existing securejarhandler patch now guards
class definition in both the bootstrap and transforming module loaders:

- Sodium 0.6+ `findAdapters`/`getAdapters`: skip D3DKMT desktop adapter inventory.
  `ModuleScanner.listModules` also returns an empty inventory instead of calling
  desktop Kernel32 module enumeration.
- OSHI's Windows HAL GPU accessor: return an empty list before the GPU class's
  native static initializer can run. Minecraft's `SystemReport.putHardware` skips
  the optional CPU/GPU/WMI crash-report inventory, not real GL capabilities.
- Controlify 2.x: skip the SDL download/load entry points, select its existing
  GLFW fallback, and mark HID discovery intentionally disabled. Unknown method
  signatures/field layouts are logged and left unchanged. Controlify 3.x is not
  supported by this fallback.

Both `banditvault.uwp=true` and `banditvault.neoforge.nativeGuards=true` must be set;
the launcher enables the second only for NeoForge Minecraft 1.21.1. Replacement
code calls only `java.base` and the class's own fields. No fake GPU, OpenGL-version
override, native DLL substitution, or auth/entitlement/signature-check change is
involved. This does not intercept every possible native call by arbitrary mods.

The earlier optional native-probe **mixins** remain Fabric-only. Writable storage
and the memory budget are shared across loaders. Do not interpret a Fabric mixin
log as proof that NeoForge's bootstrap was guarded.

## Regression tests and remaining validation

`scripts/test-uwp-launch.ps1 -NeoForgeOnly` runs the native memory/storage policy
suite, package-selection/audit tests, and Java bytecode-verifier tests using
synthetic Java 21 target classes. It does not start Minecraft or fabricate a game
session. Test sources are outside the packaged patch source tree.

The guard/fixture sources were compiled with Eclipse ECJ and ASM 9.7.1 and run on
Java 21 with `-Xverify:all` in the Linux development environment. This verifies
replacement return values, HID state, property gates, unchanged methods/frames,
and unsupported signatures. It is **not** a test of real NeoForge module-layer
startup or a shader pack. The Windows runner additionally compiles with JDK 21.
A signed APPX build, real mod transformation and the Xbox shader checklist above
still require Windows/console validation.

## If it still crashes

Keep the following from the **same launch**, using Remote Files:

- `logs\current\mc_launch.log`, `java_output.log` and `stderr_stream.log`.
- The profile's `logs\latest.log`, `xbox_compat.log`, newest `crash-reports` entry
  and any `hs_err_pid*.log`.
- Minecraft/loader, Iris/Sodium/Controlify versions, shader pack name/version,
  console model, and whether the failure happens at startup, enabling or reloading.

Redact account tokens before sharing raw logs. A shader compilation error,
unsupported GL feature, Mesa native fault and UWP out-of-memory termination are
separate failure modes; a list of startup warnings alone cannot distinguish them.
Leaving memory headroom reduces pressure but does not guarantee that every shader
pack or modpack fits in the console's budget.

Do not copy Ole32/SDL3/hidapi DLLs from random desktop installations, disable
Minecraft authentication, or disable Sodium's entire compatibility-check system
as a workaround.
