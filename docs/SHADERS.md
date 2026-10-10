# Shaders in the UWP runtime

UWP is not desktop Windows. Denied WMI/Ole32, D3DKMT and HID calls are not proof of
an out-of-date Xbox GPU driver, and running the build shell as administrator does
not make those APIs available to the installed app.

The launcher keeps Iris/Sodium and shader packs enabled. It avoids unsupported
diagnostic/native probes on Fabric, prepares writable profile-local temporary
storage, and leaves native memory headroom for the Mesa/D3D12 rendering runtime.
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
3. On Fabric, `xbox_compat.log` in the profile should report
   `Skipping Sodium desktop adapter probe` if Sodium is installed. This guard
   applies to both the Sodium 0.5 and 0.6+ package names. GPU inventory may be empty;
   actual rendering still obtains its capabilities from Mesa/OpenGL.
4. If using Controlify 2.x, turn **Bandit Controller off** for that profile. Its
   compatibility messages should mention the UWP GLFW fallback, without trying
   to download/load SDL3 or start a hidapi scan. SDL-specific functionality is
   unavailable. Controlify 3.x removed GLFW support; use Bandit Controller instead.
5. Start a world with shaders off, enable the original shader pack, reload it,
   return to the menu and reload the world. Check both keyboard/mouse and gamepad
   input. Also test a profile without Sodium or Controlify: optional mixin targets
   must not become required dependencies.

The native-probe mixins are part of the Fabric compatibility mod. The writable
storage and memory budget changes are shared by Fabric, Forge and NeoForge, but
these mixins do not intercept NeoForge's early graphics bootstrap.

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
