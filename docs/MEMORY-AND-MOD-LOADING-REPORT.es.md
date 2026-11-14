# Informe de optimización: memoria y carga de mods

**Fecha:** 2026-11-14
**Objetivo:** nueva versión estable para Minecraft `1.21.1` / NeoForge
`21.1.250` / Java 21 (Microsoft OpenJDK) en Windows 11 UWP / Xbox, con
modpacks de 100–150 JARs y recursos limitados.
**Versión del paquete:** se anota en `CHANGELOG.md` y en la Release
`nightly-native` tras la compilación de CI (el numerado es
`<major>.<minor>.$GITHUB_RUN_NUMBER.0`).

---

## 1. Análisis de la arquitectura previa

| Punto | Situación encontrada |
| --- | --- |
| Args JVM | Fijos en `MC.Xbox/launch/minecraft_launch.cpp`: `-Xmx3584M -Xms512M -XX:MaxDirectMemorySize=512M -XX:+UseG1GC -XX:+UnlockExperimentalVMOptions -XX:MaxGCPauseMillis=50 -XX:G1NewSizePercent=20 -XX:G1ReservePercent=20 -XX:G1HeapRegionSize=32M -XX:+IgnoreUnrecognizedVMOptions -XX:+PerfDisableSharedMem` |
| Medición de recursos | Ya existía `ReadAppMemoryBudget()` en `common/launcher_common.cpp:327`, usando `Windows.System.MemoryManager::AppMemoryUsageLimit/AppMemoryUsage`, pero **no se usaba para dimensionar el heap**; sólo el watchdog la registraba cada 5 s (y necesita `RoInitialize`). |
| Override | `config/.../jvm_args.txt` se aplica **después** de los built-ins, por lo que gana siempre. `IsJvmMemoryOption()` sólo avisaba de `-Xmx`/`-Xms`/`MaxRAM*`. |
| Carga de mods | `FindModJarByFabricIdOrName()` (`mods/mod_defaults.cpp`) hacía un `FindFirstFileW("*.jar")` **completo** y abría el `fabric.mod.json` de cada JAR **en cada búsqueda**. Se llamaba en ~8 puntos desde `ConfigureKnownModDefaults()` (Sodium, Moonlight, LambdaControls, MidnightControls, Distant Horizons, 4 comprobaciones de controlador) más `FindSodiumFamilyRenderers()` y `DirectoryHasBanditController()`. Con 150 mods ≈ 1200 aperturas ZIP por lanzamiento. |
| Progreso | `reportProgress()` usaba `0.12 / 0.24 / 0.38 / 0.52 / 0.64` en puntos arbitrarios: porcentajes inventados. La UI (`ui/auth_screen.h`) **ya** dibujaba barra pulsante «Loading...» si `progress < 0`. |
| Diagnóstico | `common/crash_report.cpp` ya empaquetaba `java_args.txt`, `java_classpath_final.txt`, `latest.log` y `hs_err_pid*.log`. `telemetry/crash_parse.cpp:194` detectaba `InsufficientMemory`/`NativeOutOfMemory`/`failed to map` pero **no clasificaba el recurso agotado**. |

No se encontró ningún mecanismo de caché de metadatos de mods, ni fases con
tiempos, ni correlación entre el límite UWP y el tamaño del heap.

---

## 2. Diagnóstico del fallo reportado

```
InsufficientMemory
Native memory allocation (mmap) failed to map 301989888 bytes.
Error detail: G1 virtual space
```

- `301989888 bytes = 9 × 32 MiB` → G1 intentaba confirmar **nueve regiones de
  32 MiB** de una vez, tamaño impuesto por `-XX:G1HeapRegionSize=32M`.
- Máximo virtual informado **5120 MiB**, usado **1744 MiB**, heap Java
  **864 / 3584 MiB**. El heap **no estaba lleno**: el fallo es de *espacio
  virtual / compromiso nativo*, no de objetos Java.
- Con `-Xmx3584M` la JVM reserva casi todo el presupuesto de la app UWP. Al
  salir y volver a entrar en un servidor, Mesa/D3D12, LWJGL, metaspace y los
  hilos ya han crecido; `G1NewSizePercent=20` y `G1ReservePercent=20` fuerzan
  commits grandes justo en ese momento. Ya no queda rango virtual contiguo.
- **Consecuencia:** aumentar `-Xmx` empeora el problema. La corrección es
  reducir el techo, reservar explícitamente memoria nativa, acotar la directa
  y dejar que G1 elija región/young/pausa por sí mismo.

---

## 3. Cambios implementados

### 3.1 Perfiles de memoria — `MC.Xbox/launch/memory_profile.h` (nuevo)

Header puro, sin `windows.h` ni WinRT, para poder compilarlo y testearlo en
Linux. El launcher mide y alimenta los datos; toda la aritmética y la
clasificación de errores viven aquí.

- `DeviceMemory{budgetMb, usedMb, physicalAvailMb, budgetKnown}` y
  `Plan{profile, budgetMb, nativeReserveMb, heapMinMb, heapMaxMb, directMb,
  measured, fixedHeap}`.
- Constantes de seguridad: reserva nativa mínima **1536 MiB**, heap entre
  **1024 y 2816 MiB**, presupuesto mínimo 2048 MiB, fallback 4096 MiB si WinRT
  falla, heap redondeado a múltiplos de 32 MiB.
- `ComputePlan()` **nunca** entrega todo el presupuesto al heap: primero aparta
  la reserva nativa y luego aplica el porcentaje del perfil.

| Perfil | Reserva | Heap sobre el resto | Directa | `Xms` | Uso previsto |
| --- | --- | --- | --- | --- | --- |
| `conservative` | 45 % | 60 % | 256 MiB | `= Xmx` | **Predeterminado estable** |
| `balanced` | 38 % | 75 % | 384 MiB | 512 MiB | Packs grandes con margen medido |
| `performance` | 32 % | 88 % | 512 MiB | 512 MiB | Sólo tras medir que hace falta |

`Xms == Xmx` en el conservador evita el `mmap` de crecimiento del heap a mitad
de partida. `MaxDirectMemorySize` sigue siendo explícito (Java 21 lo iguala a
`-Xmx` por defecto, lo que dejaría a LWJGL/Mesa consumir todo el heap como
buffers directos).

**Selección:** `MC_MEMORY_PROFILE=conservative|balanced|performance` o una
palabra en `config/bandit-memory-profile.txt` (editable desde Remote Files, la
vía práctica en consola). Valores desconocidos, vacíos o con espacios/newline
caen en `conservative`.

### 3.2 Args JVM — `MC.Xbox/launch/minecraft_launch.cpp`

Se sustituye el bloque fijo por `ReadAppMemoryBudget()` → `ComputePlan()` →
`BuildJvmMemoryArgs()`. Se eliminan del arranque:

```
-XX:+UnlockExperimentalVMOptions
-XX:MaxGCPauseMillis=50
-XX:G1NewSizePercent=20
-XX:G1ReservePercent=20
-XX:G1HeapRegionSize=32M
```

Se conservan `-XX:+UseG1GC`, `-XX:+IgnoreUnrecognizedVMOptions` y
`-XX:+PerfDisableSharedMem`. **No se añade ningún flag experimental.**

`jvm_args.txt` sigue aplicándose al final (compatibilidad con instalaciones
existentes), pero ahora se registra un `WARNING` explícito si contiene flags G1
antiguas activas. El launcher **no edita** el archivo del usuario. La plantilla
sembrada se actualizó para explicar el perfil, el override y el riesgo.

`Cleanroom/1.12.2` mantiene `-Xmx2560M` sin cambios.

### 3.3 Índice de mods — `MC.Xbox/mods/mod_index.h` (nuevo) y `mod_defaults.cpp`

- `Stamp{name, size, writeHigh, writeLow}`; `Compare()` produce
  `added / removed / changed / unchanged`.
- `Index::Rebuild(snapshot, readId)` **sólo invoca `readId` en JARs nuevos o
  con tamaño/fecha cambiados**. Caché `BANDIT-MOD-INDEX-v1` serializada en TSV
  con escape de `%`, tabuladores y saltos de línea.
- `EnsureModIndex()` enumera `mods/` una sola vez por lanzamiento, carga la
  caché de `config/bandit-mod-index-<hash>.tsv` y escribe el fichero **sólo si
  algo cambió** (o si la caché no existía / era ilegible). El hash se deriva de
  la ruta, así que el directorio de mods empaquetados tiene su propia caché.
- `FindByIdOrName()` prioriza el id exacto de `fabric.mod.json` sobre el token
  del nombre de archivo; `NameMatchesAny()` detecta duplicados por nombre.
- `FindSodiumFamilyRenderers()` reutiliza el índice. Queda un *fallback* al
  comportamiento anterior para llamadas fuera de
  `ConfigureKnownModDefaults()`.

**Límites respetados:** no se paraleliza carga de clases, mixins ni
inicialización de mods; no se altera el orden ni los mecanismos oficiales de
NeoForge; no se elimina, modifica ni actualiza ningún JAR ni versión; la caché
vive **fuera de `mods/`**; no se salta ninguna inicialización.

### 3.4 Fases y tiempos — `MC.Xbox/launch/launch_phase.h` (nuevo)

Seis fases con etiquetas traducidas (EN + es-419): Comprobando archivos,
Analizando mods, Preparando dependencias, Preparando Java, Iniciando NeoForge,
Cargando Minecraft.

- `kIndeterminate = -1.0f`: la UI existente pinta «Loading...» sin porcentaje.
  Se eliminan los `0.12/0.24/0.38/0.52/0.64` y el `0.12f` inicial de
  `RunLaunchTaskWithLiveUi()`.
- **Único progreso numérico real:** el callback de `ConfigureKnownModDefaults()`
  informa `JARs contabilizados / JARs encontrados` mediante
  `MeasuredProgress(done, total)`.
- `Timeline` registra inicio/fin de cada fase, tolera el *wrap* de
  `GetTickCount64()` y reentradas, y `FormatLog()` emite una línea con los ms
  por fase y el total. `ScopeExit` garantiza el volcado también en fallos
  tempranos; en la ruta con `ExitProcess()` se llama explícitamente antes.
- `mc_launch.log` añade `Launcher preparation: ... ms; time to first presented
  frame: ... ms`. El primer fotograma lo marca el watchdog cuando
  `ShimPresentedFrames()` supera el valor de partida: **resolución 1 s**, es una
  aproximación medida, no una estimación.

### 3.5 Registro y diagnóstico

`logs/current/memory_diagnostic.txt` (nuevo, incluido en el ZIP de crash por
`common/crash_report.cpp`) contiene: perfil, si la medición es real o fallback,
límite de app, uso al arrancar, presupuesto efectivo, reserva nativa, heap
mínimo/máximo, directa, `Xms==Xmx` y **la lista completa de opciones JVM**
(incluidas las de `jvm_args.txt`).

`telemetry/crash_parse.cpp` usa `memory::ClassifyFailure()` para distinguir
`Java heap`, `native/metaspace`, `direct buffers`, `virtual address space`,
`UWP app memory limit`, `G1 virtual space` y `mod/dependency`. Un
`NoClassDefFoundError`, `Mixin apply failed` o `ModLoadingException` **nunca**
se presenta como falta de memoria. `ui/launcher_ui.cpp` muestra esa
clasificación en la pantalla de crash con la pista de Remote Files.

---

## 4. JVM recomendada

Para el presupuesto UWP observado de **5120 MiB** (Series S en modo dev), con
Java 21 y NeoForge `21.1.x`:

```
# Predeterminado estable (conservative)
-Xmx1664M -Xms1664M -XX:MaxDirectMemorySize=256M
-XX:+UseG1GC -XX:+IgnoreUnrecognizedVMOptions -XX:+PerfDisableSharedMem

# balanced
-Xmx2368M -Xms512M -XX:MaxDirectMemorySize=384M
-XX:+UseG1GC -XX:+IgnoreUnrecognizedVMOptions -XX:+PerfDisableSharedMem

# performance
-Xmx2816M -Xms512M -XX:MaxDirectMemorySize=512M
-XX:+UseG1GC -XX:+IgnoreUnrecognizedVMOptions -XX:+PerfDisableSharedMem
```

Los valores son **resultado del cálculo sobre el límite medido**, no cantidades
fijas: en un dispositivo con otro `AppMemoryUsageLimit` cambian
proporcionalmente. Java **no admite decimales** en `-Xmx` (usar MiB enteros).

**No recomendado** (reproduce el fallo): `-Xmx3584M`,
`-XX:G1HeapRegionSize=32M`, `-XX:MaxGCPauseMillis=50`,
`-XX:G1NewSizePercent=20`, `-XX:G1ReservePercent=20`,
`-XX:+UnlockExperimentalVMOptions`, ni cualquier `-Xmx` que deje menos de
~1,5 GiB para nativa/gráficos.

---

## 5. Comparativa de tiempos antes / después

**No se fabrican cifras.** Este entorno de desarrollo no dispone de Windows,
MSVC, UWP, Xbox, Java ni Minecraft, por lo que no puede ejecutar el juego ni
cronometrar un lanzamiento real. La comparativa queda **pendiente de medición
en consola**.

La versión nueva deja el dato listo en `mc_launch.log` para que la medición sea
reproducible:

```
Memory plan: profile=... measured=... appLimit=... appUsed=... heap=... direct=...
Mod index ...: N JAR(s), +A -R ~C, Z ZIP read(s), cache written|unchanged
Launch phase timings (ms): checking_files=... analyzing_mods=... preparing_dependencies=... preparing_java=... starting_neoforge=... loading_minecraft=... total=...
Launcher preparation: ... ms; time to first presented frame: ... ms
```

Procedimiento para rellenar la comparativa (mismo dispositivo, mismo perfil,
mismo modpack, mismas opciones gráficas, 3 lanzamientos de cada versión):

| Métrica | `1.0.24.0` (antes) | Versión nueva (después) | Fuente |
| --- | --- | --- | --- |
| Tiempo de preparación del launcher | pendiente | pendiente | `Launcher preparation:` (la versión anterior no lo registraba: medir con reloj externo o usar el `total=` de la nueva como referencia) |
| `checking_files` | pendiente | pendiente | `Launch phase timings` |
| `analyzing_mods` | pendiente | pendiente | `Launch phase timings` |
| `preparing_dependencies` | pendiente | pendiente | `Launch phase timings` |
| `preparing_java` | pendiente | pendiente | `Launch phase timings` |
| `starting_neoforge` | pendiente | pendiente | `Launch phase timings` |
| `loading_minecraft` | pendiente | pendiente | `Launch phase timings` |
| Hasta el primer fotograma | pendiente | pendiente | `time to first presented frame` (resolución 1 s) |
| Hasta el menú principal | pendiente | pendiente | no medible aún desde el launcher; requiere observación en consola |
| Aperturas ZIP de `fabric.mod.json` | ≈ 8 × N (estimación estructural) | `Z ZIP read(s)` en `Mod index` | log |
| Resultado tras 3 entradas/salidas de servidor | fallo `InsufficientMemory` reportado | pendiente | consola |

La estimación «≈ 8 × N» es un **recuento de llamadas en el código**, no una
medición: se documenta como tal y no se presenta como tiempo.

---

## 6. Pruebas

### Realizadas (portables, en este repositorio)

| Prueba | Resultado |
| --- | --- |
| `bash tests/run-tests.sh` | **PASS** (8 harness C++ + validador Python) |
| `tests/memory_profile_test.cpp` | PASS: 3 perfiles, escalado con 5120 / 3072 MiB, fallback sin medición, redondeo, `BuildJvmMemoryArgs()` sin flags G1 forzadas, clasificación del texto exacto del crash (`InsufficientMemory` en una palabra), aliases y trim del fichero de perfil |
| `tests/launch_phase_test.cpp` | PASS: 6 fases, duraciones, total, `kIndeterminate`, `MeasuredProgress`, wrap de contador, reentrada |
| `tests/mod_index_test.cpp` | PASS: diff por tamaño **y** fecha, arranque en caliente con **0** lecturas ZIP, un JAR cambiado → **1** lectura, id exacto vs token, escape de `%`/tab, rechazo de caché corrupta |
| `scripts/validate-fork-targets.py` (checks 1–20) | **PASS**: 56 targets, default `1.21.1-neoforge-21.1.250`, 10 tokens, 2 mods Controlify, 0 controladores Bandit, 0 warnings |
| `python3 -m py_compile scripts/validate-fork-targets.py` | PASS |
| JSON de `MC.Xbox/lang/en.json` y `es-419.json` | válidos; `en.json` regenerado con el mismo algoritmo que `scripts/update-translations.ps1` (verificado: reproduce byte a byte el `en.json` de `bbdb421`), 0 desajustes de placeholders |
| `git diff --check` | PASS |

### Pendientes (requieren UWP / Xbox real)

- Compilar y arrancar `1.21.1` + NeoForge `21.1.250` en consola.
- Lanzar con pocos mods y con **>100 mods**.
- Invalidación de caché al **añadir / quitar / actualizar** un JAR en la
  consola (verificar `+A -R ~C` y `Z ZIP read(s)`).
- Perfiles existentes, autenticación, instalación de versiones y cruceta.
- **Varias entradas/salidas de servidor** y comprobar que no reaparece
  `InsufficientMemory` / `mmap ... G1 virtual space`.
- Ausencia de errores de paralelismo o acceso simultáneo (no se ha
  introducido paralelismo; el índice es de un solo hilo).
- Comparativa de tiempos antes/después de la tabla anterior.
- Verificar que `MemoryManager` devuelve un límite real (`measured=1`) en el
  hilo de lanzamiento y que el fallback no se activa por falta de apartment.

**El APPX se compila en GitHub Actions con MSVC, pero CI no ejecuta
Minecraft ni puede validar ninguno de los puntos anteriores.** `1.0.22.0`
(AV de renderizado de Embeddium en `1.20.1`/Forge) sigue **sin verificación en
consola** y no se declara resuelto.

---

## 7. Compatibilidad y limitaciones

- MC `1.21.1`, NeoForge `21.1.x`, Java 21, mods de optimización, mixins y
  librerías externas: sin cambios en mods, versiones, classpath ni orden de
  carga.
- Perfiles, autenticación, instalación de versiones y lanzamiento: sin cambios
  de contrato. `ConfigureKnownModDefaults()` añade un parámetro opcional con
  valor por defecto, por lo que `modpack_io.cpp` sigue compilando sin cambios.
- Instalaciones existentes: la caché se crea en `config/`; si no existe, el
  primer lanzamiento reindexa y escribe. Un `jvm_args.txt` con overrides
  antiguos **puede anular la mejora**: revisar el log.
- El límite que importa es el de la **app UWP**, no la RAM física de la
  consola. `-Xmx` bajo no implica menos rendimiento gráfico; simplemente deja
  sitio a Mesa/D3D12 y a la memoria nativa de la JVM.
- `time to first presented frame` tiene resolución de 1 s (watchdog) y no
  equivale al menú principal; el tiempo hasta el menú principal sigue siendo
  una observación manual.
- No se ha tocado ningún ajuste gráfico, VSync, resolución, `options.txt`,
  `mesa_env.txt` ni defaults de mods.
