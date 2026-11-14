# Memoria, arranque de mods y diagnóstico (1.0.25.0)

_fecha: 2026-11-14 · rama `arena/5df1a8ef-javauwp`_

Este documento responde al informe `InsufficientMemory` /
`Native memory allocation (mmap) failed to map 301989888 bytes. Error detail: G1
virtual space` y recoge **qué se ha cambiado, qué se ha podido probar y qué
queda pendiente de verificar en consola**.

---

## 1. Qué pasaba (análisis)

El error no era "falta de RAM": la consola tenía presupuesto libre y el heap
Java iba por 864 MB de 3584. Falló un `mmap` de **288 MiB** etiquetado
`G1 virtual space`. Tres causas, todas en la configuración del launcher:

| # | Causa | Detalle |
| --- | --- | --- |
| 1 | **Heap fijo de 3584 MB** | `-Xmx3584M` se escribía a mano. El presupuesto de la app es uno solo y compartido: launcher + Direct2D + Mesa/D3D12 + LWJGL + JNA + JVM. Con 3,5 GB para el heap sólo quedaban ~1,5 GB para todo lo demás, y el heap crece por su cuenta hasta el techo. |
| 2 | **Nativo sin límite** | `MaxMetaspaceSize`, `ReservedCodeCacheSize` y `CompressedClassSpaceSize` iban sin acotar (HotSpot reserva 1 GB de class space por defecto). Nadie los contaba en el presupuesto. |
| 3 | **`-XX:G1HeapRegionSize=32M`** | Con regiones de 32 MB, G1 confirma memoria de nueve en nueve: 9 × 32 MiB = **301989888 bytes**. Es exactamente el tamaño del `mmap` que falló. Regiones pequeñas confirman de una en una y no pueden pedir 288 MiB de golpe. |

`-XX:G1ReservePercent=20` también estaba de más: recorta el heap útil
(716 MB de 3584) sin aportar nada contra un fallo nativo.

## 2. Qué se ha cambiado

### 2.1 Plan de memoria por arranque — `MC.Xbox/launch/jvm_memory.h`

Módulo portable (sin `windows.h`) que calcula la configuración en cada arranque:

```
heap = presupuesto_app
     − uso_actual_del_host        (MemoryManager::AppMemoryUsage)
     − reserva_graficos           (Mesa/D3D12: 320 MB)
     − nativo_de_la_jvm           (metaspace + code cache + class space
                                   + directa + pilas + auxiliares GC)
     − margen_de_seguridad
```

Después se redondea a 64 MB hacia abajo, se limita por perfil y se comprueba que
el **pico esperado** cabe en el presupuesto.

| Perfil | Techo heap | directa | metaspace | code cache | class space | margen |
| --- | --- | --- | --- | --- | --- | --- |
| `conservative` | 2048 | 192 | 384 | 160 | 192 | 256 |
| `balanced` (defecto) | 2816 | 256 | 448 | 192 | 256 | 192 |
| `performance` | 3072 | 256 | 448 | 192 | 256 | 128 |

En un Series S (presupuesto 5120 MB, host usando ~340 MB) el resultado es
**2816 M** en equilibrado y **2880 M** en rendimiento: el presupuesto, no el
nombre del perfil, es el techo real. Si Windows no reporta presupuesto, se usa
5120 MB con la marca `(fallback)` y, en máquinas más pequeñas, la mitad de la
RAM física.

### 2.2 Argumentos JVM que emite

```
-XX:+IgnoreUnrecognizedVMOptions -XX:+UnlockExperimentalVMOptions
-Xmx<plan>M -Xms512M -XX:MaxDirectMemorySize=<plan>M
-XX:MaxMetaspaceSize=<plan>M -XX:ReservedCodeCacheSize=<plan>M
-XX:CompressedClassSpaceSize=<plan>M
-XX:+UseG1GC -XX:MaxGCPauseMillis=50 -XX:G1NewSizePercent=20
-XX:G1ReservePercent=10 -XX:+PerfDisableSharedMem -XX:-AlwaysPreTouch
```

Sin `G1HeapRegionSize`: lo elige HotSpot (2–4 MB con este heap).

### 2.3 Diagnóstico

- `mc_launch.log` registra, antes de crear la JVM: presupuesto, uso del host,
  RAM física, perfil, heap, topes nativos, pico esperado, margen y notas.
- Registra también la lista **completa** de opciones en vigor, después de aplicar
  `jvm_args.txt`, para que un post-mortem no tenga que adivinar.
- El marcador de crash guarda `jvmProfile`, `jvmHeapMb` y `jvmPlan`.
- `jvm_memory.h` clasifica el `hs_err`: heap Java, `G1 virtual space`,
  compressed class space, metaspace, code cache, buffers directos, pilas, GC
  overhead. `crash_parse.cpp` conserva ahora la línea `# Error detail:` (antes se
  perdía, y es la que distingue G1 de metaspace).
- La pantalla de crash, cuando el fallo es nativo, lo dice y **no** sugiere subir
  el heap.

### 2.4 Arranque y carga de mods

Cinco rutas abrían **todos** los JAR en cada arranque para buscar un mod por su
id (`fabric.mod.json`), y sólo después probaban por nombre de archivo:
`PatchMoonlightPoiMixin`, `ConfigureSodiumFamilyDefaults`,
`ConfigureDistantHorizonsDefaults`, `ConfigureLambdaControlsDefaults`,
`ConfigureMidnightControlsDefaults` y `DirectoryHasBanditController`. Con 150
JAR eso son cientos de aperturas de ZIP antes de que exista Java.

- **Índice de mods** (`MC.Xbox/mods/mod_index.h` + `mod-index.tsv` junto al
  directorio del juego): una sola pasada por directorio recoge nombre, tamaño y
  fecha de modificación. El id leído de cada JAR se guarda y se reutiliza
  **mientras tamaño y fecha no cambien**; añadir, borrar o sustituir un JAR
  invalida sólo ese JAR.
- **Orden de resolución**: nombre de archivo → id cacheado → sondear un JAR no
  leído todavía. En un perfil normal (nombres estándar) no se abre **ningún**
  JAR para estas búsquedas. Un sondeo se paga una vez y queda cacheado.
- Se avisa en el log si dos JAR declaran el mismo id (nunca se borra nada).
- `ReadZipTextFile` ahora **mapea** el archivo (`CreateFileMappingW` +
  `MapViewOfFile`) en vez de leerlo entero a un `std::vector`. Antes cada sondeo
  metía el JAR completo en el *commit* de la app, que es el mismo presupuesto del
  heap.
- Fases de arranque medidas con `LaunchClock` y volcadas a
  `logs/current/launch_timings.txt` (`mods and defaults`, `jvm arguments`,
  `jvm preload`, `jvm create`, `loader artifacts`, `handing over to main`), más
  una línea `first frame presented after N s`.

### 2.5 Ajuste persistido

Perfil en `Settings → Memory profile` (Conservador / Equilibrado / Rendimiento),
guardado en `LocalState\jvm-memory\profile.txt` y aplicado en el siguiente
arranque. `jvm_args.txt` sigue mandando si el usuario lo edita.

## 3. Qué se ha probado

### Automático (Linux, este repositorio)

```
bash tests/run-tests.sh
```

- `tests/jvm_memory_test.cpp` — perfiles, aritmética del plan (5120 MB, 4096 MB
  apretado, presupuesto desconocido, máquina pequeña), topes, orden de las flags,
  ausencia de `-Xmx` decimal y de `G1HeapRegionSize`, clasificación del `hs_err`
  real del informe.
- `tests/mod_index_test.cpp` — formato del índice, rechazo de ficheros
  corruptos, invalidación por tamaño y por fecha, orden de resolución, ids
  duplicados.
- `scripts/validate-fork-targets.py` — comprobaciones 18–22: el heap ya no está
  fijado en el código, todos los consumidores nativos están acotados, no vuelve
  la lectura completa de JAR, el índice está en uso y los tests están cableados.

Todo verde en la rama (`All portable checks passed`).

### Pendiente (requiere Windows/consola)

No hay UWP, MSVC ni Series S en este entorno, así que **no** se ha podido:

1. Compilar el APPX aquí (lo hace el workflow `native-neoforge`; ver §5).
2. Arrancar Minecraft 1.21.1 + NeoForge 21.1.250 en consola.
3. Medir tiempos reales de carga con 100–150 mods.
4. Repetir entradas y salidas de servidor (el escenario del crash original).
5. Confirmar que la caché se invalida al añadir/actualizar mods en consola.

## 4. Comparativa de tiempos

No hay una medición "antes" en consola, así que no se inventa ninguna. Lo que sí
queda es el instrumento para medirla: cada arranque escribe
`logs/current/launch_timings.txt` con una fila por fase y las líneas
`launch phase <fase>: N ms (total M ms)` en `mc_launch.log`.

Trabajo eliminado, contable en operaciones de E/S (no en segundos):

| Operación | Antes (150 JAR) | Ahora |
| --- | --- | --- |
| Aperturas de ZIP para resolver mods conocidos | hasta 5 × 150 = 750 | 0 con nombres estándar; 1 por JAR nuevo la primera vez |
| Bytes leídos por sondeo | JAR completo, copiado a memoria | páginas del mapeado que miniz toca |
| Pasadas por el directorio de mods | 6 | 1 (+1 para contar) |
| Invocaciones `get_Bounds` de WinRT por frame | 2–4 | 1 (ya estaba en `bbdb421`) |

Para comparar antes/después en consola: instalar el build anterior, jugar una
partida, guardar `launch_timings.txt`, instalar 1.0.25.0 y repetir con el mismo
perfil.

## 5. Build y entrega

`tests/run-tests.sh` pasa y el workflow `.github/workflows/native-neoforge.yml`
compila el APPX en Windows. La entrega incluye el APPX firmado, su `.sha256` y el
`.cer`, publicados como artefacto de la ejecución y Release.

Versión: **1.0.25.0** (`MC.Xbox/Package.appxmanifest`), que sustituye a
1.0.24.0.

## 6. Limitaciones conocidas

- **El presupuesto de 5120 MB no es negociable.** Con 100–150 mods, ~2,8 GB de
  heap es lo máximo razonable en Series S. Un pack que pida 4 GB de heap no
  puede funcionar aquí; con el perfil Conservador fallará por heap Java, no por
  memoria nativa, y el mensaje lo dirá.
- **Metaspace acotado.** 448 MB cubre packs de 100–150 JAR; con varios cientos
  puede faltar. Se sube con `-XX:MaxMetaspaceSize=512M` en `jvm_args.txt`.
- **`G1ReservePercent` a 10** cambia el comportamiento de GC respecto al build
  anterior. No debería notarse; si aparecen pausas largas, se sube desde
  `jvm_args.txt`.
- **El perfil se aplica al siguiente arranque**, no en caliente.
- **La caché de mods** vive en `<perfil>\game\mod-index.tsv`; si se corrompe, se
  reconstruye sola (y se registra en el log).
- Nada de esto toca la autenticación, la verificación de propiedad, los perfiles
  ni los archivos de los modpacks.
