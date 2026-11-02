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

`1.20.1 / Forge / 47.4.20` es un objetivo `experimental` del catálogo. Los mods
de optimización gráfica de la familia Sodium (Embeddium, Rubidium y similares)
fallaban ahí por tres motivos que se sumaban:

1. **Dos renderizadores recomendados a la vez.** En la pestaña *Recomendados* de
   Forge 1.20.1 aparecían `rubidium` **y** `embeddium`. Modrinth declara
   Rubidium como *incompatible* con todas las versiones 1.20.1 de Embeddium:
   los dos reescriben el mismo renderizador de chunks y aplican los mismos
   mixins. Instalar ambos aborta el arranque. Rubidium está abandonado desde
   2023; Embeddium es su reemplazo mantenido.
2. **Ajustes gráficos que sólo se aplicaban a Fabric.** El launcher siembra
   valores conservadores para la ruta Mesa/UWP (`SWAP` en vez de `ASYNC`,
   staging avanzado desactivado, un solo fotograma en vuelo, actualizaciones de
   chunks diferidas) porque los valores de fábrica de Sodium se cuelgan en la
   consola. Pero esa rutina buscaba `fabric.mod.json` con id `sodium` y
   escribía únicamente `config/sodium-options.json`. Los mods de Forge se
   declaran en `META-INF/mods.toml` y usan `embeddium-options.json` o
   `rubidium-options.json`, así que el perfil 1.20.1 Forge se quedaba siempre
   con los valores peligrosos.
3. **Reparación de mixins que excluía justo 1.20.1.** La limpieza de las
   desactivaciones antiguas de `sodium-mixins.properties` se ejecutaba sólo con
   `minecraftVersion < "1.20.1"`.

Desde este cambio:

- El launcher detecta las tres familias por nombre de jar (`sodium`,
  `embeddium`, `rubidium`) e ignora los complementos (`sodium-extra`,
  `embeddium-extra`, `reeses-sodium-options`,
  `textrues-embeddium-options`, Indium, Oculus, Iris…).
- Si hay más de un renderizador en el perfil, conserva **Embeddium** y aparta
  los otros como `*.jar.disabled`. No se borra nada: para volver a usar
  Rubidium basta con quitar el sufijo `.disabled`. La decisión queda registrada
  en `mc_launch.log`.
- Se siembran las claves seguras en el fichero de opciones correcto, incluida
  `always_defer_chunk_updates_v2` (Embeddium 0.3.x cambió el nombre de esa
  clave), y la reparación de mixins cubre también 1.20.1.
- `config/recommended-mods.json` ya no recomienda Rubidium en Forge 1.18.2 ni
  1.20.1; Embeddium va primero.

Instala **solo un** renderizador por perfil. Para comprobar que el arreglo se
aplicó, busca en `mc_launch.log` la línea `UWP graphics defaults seeded in
...\embeddium-options.json`. Si aparece `Disabled conflicting renderer ...`,
el launcher ha apartado un jar incompatible.

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

Pruebas rápidas para acotar sin recompilar:

1. Mundo nuevo con **sólo Embeddium** (o con `mods/` vacía). Si con vanilla no
   crashea, es el renderer; si crashea igual, es la ruta gráfica/audio base.
2. Revisa que no haya **Xaero's Minimap / Xaero's World Map**: están en la lista
   de bloqueo del launcher por «Exception Access Violation». Ojo, el bloqueo
   busca `xaeros-minimap` con guion y los jars reales suelen llamarse
   `Xaeros_Minimap_...` con guion bajo; si están, quítalos a mano.

> Sin verificación en consola todavía. En `mc_launch.log` deben seguir
> apareciendo `JNA COM shim enabled` y las dos líneas
> `System COM DLL preloaded: ole32.dll` / `oleaut32.dll`.

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
