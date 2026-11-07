# Fork «Native + NeoForge» — guía en español

Este repositorio (`erjezuh/JavaUWP`) es un fork de
[veroxsity/JavaUWP](https://github.com/veroxsity/JavaUWP) (Bandit Launcher).
Esta edición junta dos cosas:

1. **La versión «native» de teclado y ratón.** Es la rama `main-native` del
   proyecto original: en lugar del *mouse relay* (ratón reenviado desde el
   móvil/PC por red), el launcher recibe directamente el ratón USB y el teclado
   en los menús y dentro de Minecraft, con controles de sensibilidad
   independientes para menú y juego. Se distribuye como APPX independiente, con
   su propio `LocalState` (no comparte mundos, perfiles ni ajustes con la build
   normal).
2. **NeoForge como objetivo de primera clase**, con **Minecraft 1.21.1 +
   NeoForge 21.1.250** por defecto y arreglos para que NeoForge funcione en
   general (todas las filas del catálogo), no solo en un versión concreta.

> Esto **no** es un port a PC de escritorio: sigue siendo una app UWP para Xbox
> Series S|X en Modo Desarrollador. Lo que cambia es la entrada (teclado/ratón
> USB directos en vez de mando + relay) y el loader por defecto. La
> autenticación Microsoft/Xbox y la verificación de propiedad de Minecraft
> Java Edition se mantienen intactas; no hay modo offline ni cuentas falsas.


> **Controlify (2026-11-02).** Este fork ya no compila el mod *Bandit Controller*:
> generaba 51 variantes y consumía 27 de los 39 minutos de un build frío, además
> de ser el único paso que fallaba por errores transitorios del installer de
> NeoForge. En su lugar se empaquetan Controlify `3.0.1+lts` y su dependencia
> YACL `3.8.2` (con URL, SHA1 y tamaño fijados en
> `config/controlify-bundle.json`) y se colocan en `mods/` de las instancias
> nuevas del target por defecto `1.21.1 / neoforge / 21.1.250`. El campo
> `controllerProvider` del catálogo es `none` en las 56 filas. Se conserva el
> mod `banditvault-xbox-compat`, que parchea UWP y no es un mod de control.

## De dónde viene cada cosa

| Parte | Origen |
| --- | --- |
| Entrada nativa (ratón USB, teclado, sensibilidad, icono, manifiesto APPX propio, workflow `nightly-native`) | Rama `main-native` del GitHub original, traida tal cual al fork (fast-forward limpio desde `c866ca9`, la cabeza de `main`). |
| Soporte de loaders, catálogo, mod de compatibilidad, mods de controlador, GLFW shim, Mesa | Proyecto original, sin cambios funcionales salvo los descritos abajo. |
| Objetivo por defecto NeoForge 1.21.1 y robustez de NeoForge | Trabajo de este fork (esta rama). |

La incidencia [#109](https://github.com/veroxsity/JavaUWP/issues/109)
(«1.21.1 NeoForge cant be installed on bandit launcher Native») está cerrada
en el original como corregida; el arreglo que añadió el archivo NeoForm a los
manifiestos ya viene en la base que usamos. Aquí se añaden además las
comprobaciones y los fallos explícitos para que un problema parecido no vuelva
a dejar un objetivo NeoForge «instalable pero roto».

## Qué se ha cambiado exactamente

### Objetivo por defecto configurable (antes estaba fijado a Fabric en 4 sitios)

- `scripts/config.ps1`: nuevos valores `DefaultLoader`, `DefaultLoaderVersion`,
  `DefaultLaunchVersion`, `DefaultControllerProvider`, `DefaultJavaRuntime` y
  `DefaultSupportLevel`, con variables de entorno equivalentes. El fork trae
  `1.21.1 / neoforge / 21.1.250 / java21` y el índice de assets `17`.
  `MinecraftVersion` y `MinecraftAssetIndex` también pasan a 1.21.1 / 17.
- `MC.Xbox/runtime_config.h.in` + `build.ps1`: nuevos tokens `@@DEFAULT_*@@`.
  El build valida que el objetivo por defecto exista **exactamente una vez**
  en `config/versions.tsv` y avisa si el `javaRuntime` no coincide.
- `build.ps1`: `download_manifest.tsv` y `runtime/manifests/<id>.tsv` se generan
  ahora con el loader por defecto configurado (antes siempre Fabric, aunque el
  launcher arrancase en otro sitio). Los mods empaquetados de Fabric solo se
  copian al directorio del objetivo por defecto cuando éste es Fabric; para
  NeoForge lo rellena el bucle de mods por versión con
  `banditvault-neoforge-controller-1.0.0.jar`.
- `MC.Xbox/profiles/profiles.cpp`: `DefaultLaunchTarget()` lee esos valores en
  vez de `fabric` + `javaRuntime=current` (Java 25) hardcodeados.
- `MC.Xbox/App.cpp` y `MC.Xbox/launch/runtime_manager.cpp`: el *fallback* de
  `launchVersion` pasa de `fabric-loader-...` a `kDefaultLaunchVersion`.

Para elegir cualquier otra fila del catálogo como objetivo por defecto:

```powershell
.\build.ps1 -McVersion 1.21.11 -AssetIndex 29 `
            -DefaultLoader neoforge -DefaultLoaderVersion 21.11.45 `
            -DefaultLaunchVersion neoforge-21.11.45
```

o con variables de entorno (`DEFAULT_MC_VERSION`, `DEFAULT_LOADER`,
`DEFAULT_LOADER_VERSION`, `DEFAULT_LAUNCH_VERSION`, `DEFAULT_JAVA_RUNTIME`,
`DEFAULT_CONTROLLER_PROVIDER`, `MC_ASSET_INDEX`). El `javaRuntime` y el
`controllerProvider` deben coincidir con la fila de `config/versions.tsv`.

### Resolución de la versión de NeoForge

- Nuevo `MC.Xbox/launch/loaders/neoforge_version.h` con helpers puros
  (`NeoForgeVersionFromLaunchVersion`, `ResolveNeoForgeLoaderVersion`), sin
  dependencias de WinRT/JNI para poder testearlos en Linux.
- Ahora manda la versión del **catálogo** (`versionInfo.loaderVersion`), que es
  la del instalador y los artefactos descargados. El parseo del `id` del
  `version.json` queda como respaldo para perfiles antiguos, y acepta las tres
  formas conocidas: `neoforge-21.1.250`, `1.21.1-neoforge-21.1.250` y
  `21.1.250`. Los ids de Fabric/Forge ya no producen coordenadas Maven
  absurdas.
- `minecraft_launch.cpp`, `NeoForgeAdjustClasspath`, `NeoForgeBeforeLaunch` y
  la preparación de artefactos usan ese resolver.

### Preparación de artefactos y manifiestos (NeoForge en general)

- `neoforge.cpp`: comprobaciones previas explícitas (client jar, installer jar
  con `data/client.lzma`, versión de installertools, archivo NeoForm). Antes,
  si faltaba algo, fallaba dentro de una excepción de Java sin mensaje útil.
- `scripts/new-download-manifest.ps1`:
  - `Get-RemoteSha1`: si un mirror no publica el `.sha1`, descarga el artefacto
    y calcula el hash localmente; un artefacto suelto del instalador ya no
    aborta el manifiesto completo de ese objetivo.
  - La versión de NeoForm se resuelve desde `arguments.game`,
    `minecraftArguments` o `data` de `install_profile.json`, y se escribe en la
    cabecera `# neoFormVersion` para que el launcher la tenga siempre.
  - Las versiones de installertools / jarsplitter / binarypatcher /
    AutoRenamingTool se buscan en los grupos Maven `net.neoforged.*`,
    `net.minecraftforge.*` y `net.neoforged`, según la época del instalador.
- `scripts/common.ps1`: `Resolve-CatalogLoaderVersion` y
  `Resolve-InstallerNeoFormVersion` compartidos.
- `scripts/gen-neoforge-artifacts.ps1` y
  `controller_mod/neoforge/build_neoforge_controller_mod.ps1` ya no fijan
  `21.1.233` (el catálogo usa `21.1.250`): leen la versión del catálogo y la
  coordenada NeoForm del instalador oficial.
- `build.ps1`: el parche UWP de `securejarhandler` se compila contra la versión
  que resuelve el objetivo por defecto, con fallback automático a `3.0.8` si
  esa versión no compila.

### CI

`.github/workflows/native-neoforge.yml` compila el APPX nativo desde esta rama
(`workflow_dispatch` o push) y **publica siempre un Release descargable** en la
etiqueta `nightly-native`, con `BanditLauncherNative-neoforge.appx`, su
`.sha256` y el `.cer` del certificado firmante. Los tres archivos también
quedan como artefacto de la ejecución. En un `workflow_dispatch` se puede
omitir la publicación con `publish_release=false`.

Descarga: <https://github.com/erjezuh/JavaUWP/releases/tag/nightly-native>.
Instala el `.appx` y el `.cer` desde el Portal de Dispositivos Xbox; el `.cer`
debe coincidir con el que firma ese mismo APPX. Ten en cuenta que la licencia
original exige permiso por escrito de veroxsity / BanditVault para
redistribuir APPX: esta publicación se hace bajo la responsabilidad del
propietario de este fork. El workflow original `nightly.yml` sigue igual,
referido a `main-native`.

## La cruceta no coincide con donde el juego cree que está el ratón

Síntoma: dentro de Minecraft la cruceta (`+`) aparece en un sitio, pero los *tooltips* y los clics caen hacia la esquina superior izquierda. Afectaba a todas las versiones y no dependía de la sensibilidad.

Causa: el shim de GLFW dibuja la cruceta en píxeles del **framebuffer**, pero entregaba a Minecraft la posición en otro espacio de coordenadas. En `main-native` se quedó `g_menu_window_width = g_framebuffer_width` (el tamaño *anterior*), así que tras `glfwCreateWindow(854, 480)` el juego creía que el ratón se movía en 854×480 sobre una superficie de 1920×1080. La rama `main` ya lo había arreglado (`newFramebufferWidth`), pero el merge de `main-native` no lo incorporó.

Arreglado en este fork: el espacio del cursor es ahora el framebuffer **actual** y la cruceta se dibuja con la misma función (`WindowToMenuInputX/Y`) que genera `glfwGetCursorPos`. Si sigue pasando, abre el log del shim y pásame la línea que empieza por `Window size` (ahora incluye `cursor space` y `scale`); con esos tres tamaños se puede ajustar el mapeo exacto de tu TV.

## Error `0x80070005` al instalar el APPX («Acceso denegado»)

El mensaje completo suele ser: *«Error 0x80070005 en la operación de implementación de Add con el volumen D: en el paquete Microsoft.MicrosoftEdge.BanditLauncher_1.0.0.0_x64_8wekyb3d8bbwe»*. Es un error del **servicio de despliegue de Windows/Xbox**, no del launcher: la consola se niega a registrar el paquete. Haz esto en orden:

1. **Desinstala la versión anterior.** En la Xbox: *Dev Home → Mis juegos y aplicaciones → Bandit Launcher → Desinstalar*. Hasta ahora todos los builds de CI llevaban la versión `1.0.0.0`; intentar añadir un paquete con la misma identidad y versión que ya está registrado produce este `0x80070005`. Desde este cambio el workflow usa `GITHUB_RUN_NUMBER` como tercer campo (`1.0.<n>.0`), así que cada Release es una versión superior y se puede actualizar sin desinstalar.
2. **Instala el certificado ANTES del APPX.** Descarga el `.cer` **de la misma Release** que el `.appx`. En el Portal de Dispositivos Windows (`https://<ip-de-la-xbox>:11443`): *Apps → Deploy apps → Install Certificate*, selecciona el `.cer` y pulsa *Install*. El certificado debe coincidir con el que firma ese APPX; si la caché de GitHub se pierde el build genera uno nuevo, así que no reutilices un `.cer` de otra Release.
3. **Comprueba el Modo desarrollador.** La consola debe estar en Dev Mode con la app *Xbox Dev Mode* activa; en modo comercial el despliegue de paquetes `runFullTrust` se deniega.
4. **Limpia el nombre del archivo.** El navegador suele guardarlo como `BanditLauncherNative-neoforge (1).appx`. Renómbralo a `BanditLauncherNative.appx` (sin espacios ni paréntesis) y vuelve a subirlo.
5. **Reinicia la consola y libera espacio.** El APPX ocupa ~450 MB y se expande a más de 1 GB; si el volumen de aplicaciones está lleno, el *Add* falla.

Si después de esto sigue fallando, pásame el texto completo del error y la versión que figura en las notas de la Release (`Package version: X.Y.Z.R`).

## Los mods de render no funcionan en 1.20.1 Forge (Embeddium / Rubidium)

`1.20.1 / Forge / 47.4.20` es un objetivo `experimental` del catálogo.
**Embeddium 0.3.x y Rubidium no son compatibles con la ruta Mesa/WGL UWP de
este fork**: entran al mundo y a los pocos segundos la JVM muere con
`EXCEPTION_ACCESS_VIOLATION` (`0xc0000005`). Ocurre en cualquier mundo, incluso
uno nuevo, y con sólo los mods recomendados.

La causa está en el propio mod, no en el launcher: en la rama `20.1/forge` de
Embeddium **no existe la opción `use_chunk_multidraw`**. `DefaultChunkRenderer`
llama siempre a `glMultiDrawElementsBaseVertex` (GL 3.2) y usa UBOs y buffers
inmutables/persistentes. Mesa llvmpipe en el contenedor UWP anuncia esas
extensiones pero revienta al ejecutarlas. Las claves que el launcher sembraba
(`use_advanced_staging_buffers=false`, `cpu_render_ahead_limit=1`,
`always_defer_chunk_updates_v2=true`) son útiles para los cuelgues de staging,
pero **no pueden apagar ese renderizador de chunks**: Gson ignora las claves que
no existen en `SodiumGameOptions`. No hay arreglo por configuración.

Qué hace este build:

- `config/recommended-mods.json` **ya no recomienda ningún renderizador de la
  familia Sodium en `forge."1.20.1"`**. El resto de mods recomendados
  (`ferrite-core`, `entityculling`, `canary`, `immediatelyfast`, `saturn`,
  `modernfix`, `clumps`, `memoryleakfix`, `noisiumed`, `fastload`) se mantienen.
- Antes de cada lanzamiento, en Minecraft **1.20.1**, el launcher aparta los
  jars `embeddium-*` y `rubidium-*` como `*.jar.disabled` (**no los borra**) y
  lo escribe en `mc_launch.log`:
  `Disabled UWP-incompatible renderer ... -> ... (Minecraft 1.20.1 / Mesa WGL)`.
  El perfil arranca con el renderizador de Minecraft. El jar de **Sodium de
  Fabric y las demás versiones de Minecraft no se tocan**.
- Para volver a experimentar con el renderer, crea un fichero vacío
  `config/bandit-keep-forge-renderer.txt` en el perfil (se puede hacer desde
  *Archivos Remotos*). Con ese marcador el launcher no aparta ningún jar.

### Cómo aplicarlo ahora mismo, sin reinstalar

Renombra `embeddium-<versión>.jar` a `embeddium-<versión>.jar.disabled` dentro
de `mods/` del perfil (o sácalo de la carpeta). Con el renderizador de vanilla
el mundo debería aguantar. Si aun así crashea, entonces el problema no es
Embeddium y hay que mirar el frame del `hs_err_pid*.log`.

> El historial anterior (dos renderizadores recomendados a la vez, defaults que
> sólo se aplicaban a Fabric, reparación de mixins que excluía 1.20.1) sigue
> siendo válido para otros targets: el launcher continúa detectando las tres
> familias, ignorando complementos, conservando un solo renderer cuando hay
> varios y sembrando las opciones conservadoras en
> `embeddium-options.json` / `rubidium-options.json` / `sodium-options.json`.
> Simplemente, en 1.20.1 esos dos ports de Forge ya no se cargan.
>
> Sin verificación en consola todavía. Además, el parche UWP de
> `securejarhandler` sigue sin aplicarse a Forge 47 (`2.1.10`), porque el
> código incluido está compilado contra NeoForge `3.0.8`; un mod con
> Jar-in-Jar complejo puede seguir dando problemas.

## «Minecraft crasheo mientras cargaba los mods» por OSHI / JNA / WMI (`Ole32`)

Este es **otro fallo distinto** al de los dos renderizadores. Con el arreglo
anterior ya instalado, la captura de `Profile 4 - 1.20.1` /
`forge 1.20.1-47.4.20` mostraba el aviso *«Un mod solicitó detalles del
hardware de Windows, cual está consola bloquea. Mods que comprueben la GPU de
está manera no funcionaran aqui por ahora»* y esta traza:

```
java.lang.RuntimeException --> java.lang.UnsatisfiedLinkError
Unable to load library 'Ole32': El parámetro no es correcto.
Native library (win32-x86-64/Ole32.dll) not found in resource path
  com.sun.jna@5.12.1  NativeLibrary.loadLibrary -> Native.load
  com.sun.jna.platform.win32.Ole32.<clinit>
  com.github.oshi@6.2.2  WmiQueryHandler.initCOM -> queryWMI
```

### Qué pasa realmente

1. **Embeddium 0.3.x** ejecuta `GraphicsAdapterProbe.findAdapters()` desde
   `SodiumMixinPlugin.onLoad()`, o sea **durante la carga de mods**. En Windows
   esa función llama a `new SystemInfo().getHardware().getGraphicsCards()`;
   OSHI 6.2.2 lo resuelve con una consulta WMI a `Win32_VideoController` por
   medio de COM (`Ole32`). Rubidium 0.7.x (basado en Sodium 0.4) **no** hace
   esa consulta: por eso el síntoma aparece al pasarse a Embeddium.
2. El mod de compatibilidad `banditvault-xbox-compat` ya cancela la consulta
   OSHI **propia de Minecraft** (`SystemDetailsOshiBypassMixin`), pero es un mod
   Fabric y además no puede cancelar una consulta que hace otro mod.
3. JNA 5.12.1 carga `Ole32` con
   `LoadLibraryExW("Ole32.dll", NULL, LOAD_WITH_ALTERED_SEARCH_PATH)`, con un
   **nombre suelto**. En el contenedor UWP eso se rechaza con
   `ERROR_INVALID_PARAMETER` («El parámetro no es correcto»). El
   `UnsatisfiedLinkError` sale del inicializador estático de `Ole32` y
   `WmiQueryHandler.queryWMI()` **no lo captura**: sólo captura `COMException`
   y `TimeoutException`. Resultado: el juego muere «mientras cargaba los mods».
4. El `oshi.properties` que el launcher ya escribía
   (`perfos.disabled`, `perfproc.disabled`, …) desactiva los contadores de
   rendimiento, **no** las consultas WMI. No podía evitar este crasheo.

### Qué se ha cambiado

- `shims/ole32_uwp.cpp` es una DLL empaquetada, sin Windows headers ni runtime
  COM, que exporta los puntos de entrada de `Ole32` y devuelve `E_FAIL`
  (`0x80004005`), y los de `OleAut32` (`SysAllocString`, `VariantClear`, …)
  devolviendo un BSTR nulo o fallo. `build.ps1` la compila con MSVC para
  `WINAPI_FAMILY_APP` y la deja **dos veces**: `natives/Ole32.dll` y
  `natives/OleAut32.dll`. El segundo nombre hace falta porque JNA también
  carga `OleAut32` por nombre suelto en cuanto la consulta WMI pasa de la
  inicialización COM.
- `MC.Xbox/launch/minecraft_launch.cpp` añade
  `-Djna.library.path=<carpeta natives>` cuando el shim existe (y conserva
  además la carpeta de natives del target, separada por `;`). Así JNA
  encuentra una **ruta absoluta** en vez de un nombre suelto: es el mismo
  mecanismo que ya permite cargar `glfw.dll` y `opengl32.dll` empaquetados.
- Con todos los `HRESULT` a fallo, `WmiQueryHandler.initCOM()` lanza una
  `COMException`, que `queryWMI()` **sí** captura: devuelve un `WmiResult`
  vacío y recuerda la clase WMI fallida. Embeddium registra «no graphics cards
  were found», no aplica workarounds de driver y **el arranque continúa**.

Es un arreglo general: vale para cualquier mod que consulte GPU, CPU o memoria
por OSHI/JNA/COM y para el `SystemReport` de Minecraft en Forge, NeoForge y
Cleanroom. Antes `Ole32` **nunca** podía cargarse en UWP, así que no se
degrada ninguna ruta que funcionase.

### Cómo comprobarlo

En `mc_launch.log` debe aparecer:

```
JNA COM shim enabled: ...\natives\Ole32.dll
```

Si en su lugar aparece `JNA COM shim missing: OSHI/WMI hardware probes can
still crash mod loading`, el APPX instalado es anterior a este cambio.

> Sigue sin prueba física en Xbox: el DLL lo compila CI con MSVC y el contrato
> de fallo se comprueba con `tests/ole32_shim_test.cpp` en Linux, pero el
> comportamiento de `LoadLibraryExW` dentro del contenedor sólo lo confirma un
> arranque real. El shim **no emula WMI**: los mods que dependan de detectar la
> GPU para activar workarounds simplemente no los activan, que es lo deseable
> en la ruta Mesa/UWP. El aviso de la UI queda como último recurso por si otro
> mod llama a una API nativa distinta.

## `OpenAL.dll (error code = 127)` justo después del arreglo de `Ole32`

Con la `1.0.17.0` el fallo de OSHI ya no aparecía, pero el mismo perfil
`1.20.1 / forge / 47.4.20` murió un poco más adelante:

```
Failed to load library: <pkg>\runtime\natives\1.20.1-forge-47.4.20\OpenAL.dll (error code = 127)
  org.lwjgl.system.windows.WindowsLibrary.loadLibrary
  org.lwjgl.system.Library.loadNativeFromLibraryPath
  org.lwjgl.openal.ALC.create / ALC.<clinit>
```

**127 = `ERROR_PROC_NOT_FOUND`** (falta una exportación), no `126`
(`ERROR_MOD_NOT_FOUND`, falta el archivo). `OpenAL.dll` es OpenAL Soft: su
backend WASAPI se enlaza con `ole32.lib`, de modo que su tabla de importaciones
pide `PropVariantClear`, `CoTaskMemAlloc`, `CoTaskMemFree`, `CoCreateInstance`,
`CoInitializeEx`… por el **nombre suelto** `ole32.dll`. Windows resuelve esas
importaciones por nombre base: como JNA ya había cargado nuestro shim
empaquetado con el nombre `Ole32.dll` durante la consulta OSHI/WMI de
Embeddium (que ocurre **antes** de `ALC.<clinit>`), `OpenAL.dll` puede enlazarse
contra ese shim. La primera versión del shim sólo exportaba lo que JNA llama
desde Java y **no tenía `PropVariantClear`**; `LoadLibrary` abortaba con 127.
En `1.21.1 / NeoForge` no se reproduce porque allí no hay consulta OSHI
temprana y el shim nunca llega a cargarse.

Arreglo, sin tocar `java21`, GL/Mesa, el catálogo de mods ni el resto de
targets:

- El shim (`shims/ole32_uwp.cpp`) exporta también la superficie que usan los
  consumidores **nativos** de `ole32.lib` / `oleaut32.lib`:
  `PropVariantInit`/`PropVariantClear`, `CoTaskMemAlloc`/`Realloc`/`CoGetMalloc`,
  `StringFromIID`/`StringFromCLSID`, `CLSIDFromProgID`/`ProgIDFromCLSID`,
  `CoGetClassObject`, `CoRegisterClassObject`/`CoRevokeClassObject`,
  `CoMarshalInterface`/`CoUnmarshalInterface`/`CoReleaseMarshalData`,
  `CoDisconnectObject`, `CoFreeUnusedLibraries`, `CoGetCurrentProcess`,
  `CreateStreamOnHGlobal`/`GetHGlobalFromStream`, `OleRun`,
  `SysAllocStringByteLen`/`SysStringByteLen`/`SysReAllocString`,
  `VariantCopy`/`VariantCopyInd`, `SafeArrayCreate`/`Destroy`,
  `LoadTypeLib`/`RegisterTypeLib`. Siguen devolviendo `E_FAIL` / valores nulos,
  así que el contrato con OSHI no cambia.
- `MC.Xbox/launch/minecraft_launch.cpp` **precarga los `ole32.dll` y
  `oleaut32.dll` reales del SO antes de crear la JVM**, para que el audio nativo
  enlace contra la implementación del sistema y conserve WASAPI.
- `tests/ole32_shim_test.cpp` enlaza todas esas exportaciones (si falta una, el
  test no compila) y el check 14 de `scripts/validate-fork-targets.py` las exige.

### Cómo comprobarlo

En `mc_launch.log` deben aparecer, además de `JNA COM shim enabled`, dos líneas:

```
System COM DLL preloaded: ole32.dll
System COM DLL preloaded: oleaut32.dll
```

> Sin verificación en consola todavía. Si `ALC.<clinit>` sigue fallando, adjunta
> `mc_launch.log` y el `BanditLauncher-crash-*.zip`: un 127 con las dos líneas
> anteriores presentes indicaría **otra** exportación ausente, y el log dice
> qué bibliotecas se precargaron. No basta con la captura para distinguirlo.

## `EXCEPTION_ACCESS_VIOLATION` a los pocos segundos de entrar al mundo

Con la `1.0.19.0` el juego **ya entra al mundo** (adiós al `OpenAL.dll (error
code = 127)`), pero a los pocos segundos aparece
`EXCEPTION_ACCESS_VIOLATION` (`0xc0000005`). No es una excepción Java: la JVM
deja un `hs_err_pid*.log` porque **código nativo** tocó memoria inválida. Con
sólo los mods recomendados y en cualquier mundo (incluso uno nuevo) se descartan
los datos del mundo y un mod concreto: el problema está en la ruta nativa que se
inicializa al arrancar audio/render.

### Causa encontrada en el shim

En `alc/backends/wasapi.cpp` de OpenAL Soft:

```cpp
auto pvprop = PropVariant{};                       // PropVariantInit
ps->GetValue(DEVPKEY_Device_FriendlyName, pvprop.get());
pvprop.clear();                                    // PropVariantClear
ps->GetValue(PKEY_AudioEndpoint_GUID, pvprop.get());   // MISMA estructura
```

El `PropVariantClear` del sistema libera `pwszVal`, deja `vt = VT_EMPTY` y
devuelve `S_OK`. El shim de la `1.0.19.0` devolvía `E_FAIL` **sin limpiar**:
quedaba un `VT_LPWSTR` con un puntero colgante que el siguiente `GetValue` o el
destructor puede desreferenciar → `EXCEPTION_ACCESS_VIOLATION` justo al enumerar
los dispositivos de audio. Idéntico para `VariantClear` (JNA/`WbemcliUtil`).

Arreglo de la `1.0.20.0`: `PropVariantInit`/`Clear` y `VariantInit`/`Clear`
ponen a cero `vt`, las palabras reservadas y el puntero de la unión (16 bytes,
sin CRT) y devuelven `S_OK`; `VariantCopy*` y `VariantChangeType` dejan el
destino vacío. **El contrato con OSHI no cambia**: `CoInitializeEx` y
`CoCreateInstance` siguen dando `E_FAIL`, y JNA ignora el retorno de las
funciones de limpieza. `tests/ole32_shim_test.cpp` verifica el reset byte a byte.

### Cómo saber en qué DLL falla si persiste

Desde la `1.0.20.0`, la **pantalla principal de crasheo muestra el primer frame
nativo** para señales nativas (`C  [opengl32.dll+0x…]`, `OpenAL.dll`,
`glfw.dll`, …) y el recordatorio de *Archivos Remotos*: basta una foto de esa
pantalla. En builds anteriores, pulsa **«Ver detalles»** (primer botón, A) y
fotografía la traza.

Desde el menú principal, **Archivos Remotos** permite descargar en el móvil/PC
el `BanditLauncher-crash-<fecha>.zip`, que incluye
`profile/game/hs_err_pid*.log`, `logs/current/mc_launch.log`, `glfw_uwp.log` y
`latest.log`. El ZIP se genera al **volver a abrir el launcher** tras el
crasheo. Con el frame se arregla la capa correcta; no conviene tocar GL/Mesa a
ciegas.

**Confirmado en consola:** el `EXCEPTION_ACCESS_VIOLATION` que quedaba en
`1.20.1 / Forge` es el chunk renderer de **Embeddium**, no la ruta de
audio/COM. Mira la sección [Los mods de render no funcionan en 1.20.1
Forge](#los-mods-de-render-no-funcionan-en-1201-forge-embeddium--rubidium):
el launcher aparta esos jars y el perfil usa el renderizador de Minecraft.

Pruebas rápidas si aun así crashea:

1. Comprueba en `mc_launch.log` la línea `Disabled UWP-incompatible renderer
   ...`. Si no aparece, el jar de Embeddium sigue activo: renómbralo a
   `*.jar.disabled` a mano.
2. Revisa que no haya **Xaero's Minimap / Xaero's World Map**: están en la lista
   de bloqueo del launcher por «Exception Access Violation». Ojo, el bloqueo
   busca `xaeros-minimap` con guion y los jars reales suelen llamarse
   `Xaeros_Minimap_...` con guion bajo; si están, quítalos a mano.
3. Con `mods/` vacía: si con vanilla crashea igual, el frame del
   `hs_err_pid*.log` dice en qué DLL y hay que mirar esa capa, no Mesa a ciegas.

> Sin verificación en consola todavía. En `mc_launch.log` deben seguir
> apareciendo `JNA COM shim enabled` y las dos líneas
> `System COM DLL preloaded: ole32.dll` / `oleaut32.dll`. **Actualización:**
> se confirmó que el AV al entrar al mundo era de Embeddium y no de esta ruta
> COM/audio; mira la sección de renderizado más arriba.

## Memoria para perfiles con mods (3,5 GiB)

El heap máximo de la JVM es **3584 MiB = 3,5 GiB** (`-Xmx3584M`), antes era
`-Xmx3G`. **Java no admite decimales en `-Xmx`**: escribir la cifra con punto
haría que la JVM rechazara el arranque, así que hay que expresarla en MiB
enteros. Siguen igual `-Xms512M`, `-XX:MaxDirectMemorySize=512M` y todos los
ajustes G1. No es un ajuste gráfico.

`jvm_args.txt` se aplica **al final** y gana a los valores incorporados: la
plantilla incluye `#-Xmx3G` comentado para volver atrás. En `mc_launch.log`
debe aparecer `JVM heap: -Xmx3584M (3.5 GiB) ...`.

> **Ojo con el límite de la consola**: el código documenta ~5120 MB de
> presupuesto de app en Series S en modo dev. Con 3584 MiB de heap más 512 MiB
> de buffers directos, metaspace y el runtime Mesa/D3D12 se queda cerca. Si con
> un modpack grande la app se cierra por memoria, descomenta `#-Xmx3G` o baja
> `-XX:MaxDirectMemorySize`. Cleanroom / 1.12.2 mantiene su propio
> `-Xmx2560M`; no se han tocado otros targets. Sin verificación en consola
> todavía, y esto alivia la presión de GC, **no sube el techo de FPS**.

## Optimizaciones del camino caliente (sin cambiar ajustes gráficos)

El techo de FPS en esta plataforma lo marcan **VSync, la resolución del
framebuffer y la capa Mesa → D3D12** (traducción de shaders SPIR-V → DXIL y
draw calls). Nada de eso se puede subir desde el launcher sin tocar ajustes
gráficos, y este cambio **no los toca**: `glfwSwapInterval()` sigue aplicando lo
que pide el juego, `MESA_SHADER_CACHE_DIR` y `mesa_glthread` ya estaban
activos, y el servidor de *Archivos Remotos* espera en `select()` con 250 ms
(no hace busy-wait).

Lo que sí se ha quitado es trabajo redundante en el hilo de render, que aquí
compite con la traducción de shaders:

| Antes | Ahora |
|---|---|
| `glfwPollEvents()` **y** los tres getters de tamaño llamaban a `RefreshWindowMetrics()` → 2–4 consultas WinRT `ICoreWindow::get_Bounds` por frame | Los getters devuelven los valores ya publicados; `glfwPollEvents()` es el único refresco, 1 vez por frame |
| `MC_LEGACY_CONTROLLER_MOD` y `MC_USE_RAW_SCALED_FRAMEBUFFER` se releían con `GetEnvironmentVariableW()` 4–8 veces por frame | `static const bool`: una sola lectura por proceso |
| `glfwMakeContextCurrent()` escribía 2 líneas de log sin límite (cada `ShimLog()` abre y cierra el fichero) | Acotadas a 12; los mensajes de error siguen sin límite |

**Sin retardo nuevo**: los callbacks de resize salen de `ProcessEvents()` dentro
del mismo `glfwPollEvents()`, así que el mapeo del cursor y de la cruceta se
actualiza igual que antes. `glfw_shim/perf_cache.h` contiene los helpers
portables (parseo de flags y `CacheExpired`, que sobrevive al wrap de
`GetTickCount64`); `tests/perf_cache_test.cpp` los prueba con `g++` y la
comprobación 17 del validador exige que los getters no vuelvan a consultar
WinRT y que `glfwPollEvents()` conserve el refresco.

**No aplicado** (haría falta perfilado en consola y pondría en riesgo la
cruceta ya verificada): suscribirse a `CoreWindow::SizeChanged` para eliminar
la última consulta `get_Bounds`, coalescer los eventos de puntero de alta
frecuencia, y sustituir los 12 `glGet*` + `glBufferData` por frame del overlay
del cursor por un buffer persistente con `glBufferSubData`.

> No se promete una subida de FPS medible sin medir en consola. Esto libera
> tiempo de CPU del hilo de render; el límite real sigue estando en VSync y en
> la capa gráfica.

## Cómo compilar

Requisitos: Windows, Visual Studio con C++/WinRT, Windows SDK, JDK 17, 21 y
25, PowerShell y red para descargar los archivos oficiales (Mojang, Fabric,
NeoForge, Modrinth…). Ver [BUILDING.md](BUILDING.md) para el detalle.

```powershell
.\scripts\setup.ps1
.\build.ps1 -SkipStopAppProcesses -ReleaseChannel nightly-native
```

Salida: `output\BanditLauncherNative_<version>.appx`. Instala el APPX y el
`.cer` correspondiente desde el Portal de Dispositivos Xbox. El ratón y el
teclado USB funcionan directamente; en Ajustes hay sensibilidades separadas
para menú y juego.

## Comprobaciones que sí se pueden ejecutar fuera de Windows

```bash
bash tests/run-tests.sh
```

Incluye:

- `tests/neoforge_version_test.cpp`: 15 casos sobre el parseo de ids y la
  prioridad del catálogo.
- `scripts/validate-fork-targets.py`: comprueba que el objetivo por defecto
  existe en el catálogo, que sus `javaRuntime`/`controllerProvider` coinciden,
  que todos los tokens de `runtime_config.h.in` se sustituyen, que ningún
  script auxiliar arrastra `21.1.233`, que cada fila NeoForge con controlador
  tiene variante y que el launcher usa el resolver nuevo.

En Windows, además:

```powershell
.\scripts\syntax-check.ps1 -Files @("launch\loaders\neoforge.cpp","profiles\profiles.cpp","App.cpp")
.\scripts\validate-modern-version-matrix.ps1 -PackagePath staging\package
```

## Limitaciones y notas legales

- El APPX solo se ha podido **preparar**, no compilar ni probar en una Xbox
  desde este entorno (no hay Windows/MSVC/Xbox aquí). Los cambios en C++ son
  conservadores y están cubiertos por las comprobaciones estáticas y el test
  C++; la validación real requiere un build en Windows y una consola.
- Forge/NeoForge y las versiones modernas siguen siendo *experimentales* en el
  proyecto original. Que una fila esté en el catálogo no garantiza que todos
  los mods funcionen dentro del sandbox UWP.
- La licencia de `LICENSE` del proyecto original permite modificaciones
  privadas, pero **forks públicos, mirrors, redistribución de código y builds
  empaquetados requieren permiso por escrito** de veroxsity / BanditVault.
  Redistribuir APPX también lo requiere. Este fork conserva íntegros el login
  Microsoft/Xbox y la verificación de propiedad de Java Edition; no se ha
  tocado `MC.Xbox/auth/`.
- Los mundos y perfiles de la build normal y la native **no se comparten**:
  exporta los mundos con Remote Files antes de cambiar de paquete.
