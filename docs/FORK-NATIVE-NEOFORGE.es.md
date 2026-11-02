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
