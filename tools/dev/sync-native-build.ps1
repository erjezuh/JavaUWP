# sync-native-build.ps1
#
# Builds the native (mouse) package from upstream/main-native, optionally with the
# local fixes that are not merged yet. Lives in the repo on purpose: the bootstrap
# below fetches this very file, so it never has to be copied around by hand.
#
# One line, from the folder that has build.ps1:
#
#   powershell -NoProfile -ExecutionPolicy Bypass -Command "git fetch --force https://github.com/erjezuh/JavaUWP.git 'refs/heads/arena/01a10d2a-javauwp:refs/remotes/sync/fix'; git show refs/remotes/sync/fix:tools/dev/sync-native-build.ps1 | Set-Content .\sync-native-build.ps1 -Encoding UTF8; & .\sync-native-build.ps1"
#
# Options:
#   -NoFixes          build upstream main-native exactly as it is, without any local
#                     change. Use this to tell a local fix from a local build problem.
#   -McVersions 1.21.1  trim the catalog to the NeoForge targets of those versions
#   -AppxVersion ...    package version (default 1.0.0.200, above the published nightly)
#   -SkipBuild        sync and patch only, no compile

param(
    [string[]]$McVersions = @(),
    [string]$AppxVersion = '1.0.0.200',
    [switch]$NoFixes,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

$forkUrl = 'https://github.com/erjezuh/JavaUWP.git'
$upstreamUrl = 'https://github.com/veroxsity/JavaUWP.git'
$fixRef = 'refs/remotes/sync/fix'
$nativeRef = 'refs/remotes/sync/native'

if (-not (Test-Path '.\build.ps1') -or -not (Test-Path '.\MC.Xbox\App.cpp')) {
    throw 'Esta carpeta no es la del repositorio. Abre la carpeta JavaUWP (la que tiene build.ps1) y ejecutalo ahi.'
}

if ($NoFixes) {
    Write-Host '== -NoFixes: se compila el codigo original de main-native, sin ningun cambio local' -ForegroundColor Yellow
}

Write-Host '== Sincronizando con GitHub' -ForegroundColor Cyan
& git fetch --force $upstreamUrl "refs/heads/main-native:$nativeRef"
if ($LASTEXITCODE) { throw 'No pude descargar main-native.' }
& git fetch --force $forkUrl "refs/heads/arena/01a10d2a-javauwp:$fixRef"
if ($LASTEXITCODE) { throw 'No pude descargar la rama de los arreglos.' }

Write-Host '== Preparando la rama native' -ForegroundColor Cyan
& git checkout --force -B arreglo-neoforge $nativeRef
if ($LASTEXITCODE) { throw 'No pude preparar la rama de compilacion.' }

if (-not $NoFixes) {
    Write-Host '== Superponiendo los arreglos' -ForegroundColor Cyan
    # MC.Xbox\common y MC.Xbox\launch son identicos entre main y main-native, asi que los
    # archivos de los arreglos se pueden superponer sin conflictos
    & git checkout $fixRef -- MC.Xbox/common MC.Xbox/launch CHANGELOG.md
    if ($LASTEXITCODE) { throw 'No pude traer los archivos de los arreglos.' }
}

Write-Host '== Parcheando build.ps1' -ForegroundColor Cyan
$p = (Resolve-Path '.\build.ps1').Path
$t = [System.IO.File]::ReadAllText($p)
$changed = $false

if ($NoFixes) {
    Write-Host '   -NoFixes: build.ps1 se queda como esta' -ForegroundColor DarkGray
} else {
    # 1) compilar el helper de rutas largas
    if ($t -notmatch 'common\\long_path\.cpp') {
        $t = $t -replace '(common\\launcher_common\.cpp)', '$1 common\long_path.cpp'
        $changed = $true
        Write-Host '   long_path.cpp anyadido a las fuentes' -ForegroundColor Green
    } else {
        Write-Host '   long_path.cpp ya estaba' -ForegroundColor DarkGray
    }

    # 2) el uso extendido de clave "Code Signing" tiene nombre traducido en Windows no ingles,
    #    asi que el certificado recien creado no se encontraba y el build moria en Packaging
    $oldEku = "`$_.EnhancedKeyUsageList | Where-Object { `$_.FriendlyName -eq 'Code Signing' }"
    $newEku = "`$_.EnhancedKeyUsageList | Where-Object { `$_.ObjectId.Value -eq '1.3.6.1.5.5.7.3.3' }"
    if ($t.Contains($oldEku)) {
        $t = $t.Replace($oldEku, $newEku)
        $changed = $true
        Write-Host '   filtro del certificado por OID en vez de por nombre traducido' -ForegroundColor Green
    } else {
        Write-Host '   el filtro del certificado ya era correcto' -ForegroundColor DarkGray
    }
}

# 3) exportar el certificado junto a la APPX (lo hace el nightly y hace falta para confiar
#    en el paquete). Se aplica siempre, tambien con -NoFixes, porque no cambia el codigo.
$oldAnchor = 'if (-not $KeepStaging) {'
$cerBlock = @'
# A self signed package cannot be launched until its certificate is trusted on the
# console, so export the public certificate next to the package, the same way the
# nightly workflow does.
try {
    $appxSignature = Get-AuthenticodeSignature -FilePath $appx
    if ($appxSignature.SignerCertificate) {
        $cerPath = [System.IO.Path]::ChangeExtension($appx, '.cer')
        Export-Certificate -Cert $appxSignature.SignerCertificate -FilePath $cerPath -Force | Out-Null
        Write-Host "Signing certificate: $cerPath"
    }
} catch {
    Write-Warning "Could not export the signing certificate: $($_.Exception.Message)"
}

if (-not $KeepStaging) {
'@
if ($t.Contains($oldAnchor) -and -not $t.Contains('Get-AuthenticodeSignature -FilePath $appx')) {
    $t = $t.Replace($oldAnchor, $cerBlock, 1)
    $changed = $true
    Write-Host '   exporta el .cer junto a la APPX' -ForegroundColor Green
} elseif ($t.Contains('Get-AuthenticodeSignature -FilePath $appx')) {
    Write-Host '   el .cer ya se exportaba' -ForegroundColor DarkGray
} else {
    Write-Host '   aviso: no encontre donde insertar la exportacion del .cer' -ForegroundColor Yellow
}

if ($changed) {
    [System.IO.File]::WriteAllText($p, $t)
    Write-Host '   build.ps1 guardado' -ForegroundColor Green
}

Write-Host '== Filtrando el catalogo de versiones' -ForegroundColor Cyan
if ($McVersions.Count -eq 0) {
    Write-Host '   catalogo completo, igual que el paquete oficial' -ForegroundColor DarkGray
} else {
    $catalogPath = (Resolve-Path '.\config\versions.tsv').Path
    $lines = @([System.IO.File]::ReadAllText($catalogPath) -split "`n")
    if ($lines.Count -lt 2) { throw 'El catalogo de versiones esta vacio o es ilegible.' }

    $keepLines = @($lines[0].TrimEnd("`r"))
    $keptIds = @()
    foreach ($line in $lines[1..($lines.Count - 1)]) {
        $row = $line.TrimEnd("`r")
        if (-not $row.Trim()) { continue }
        $cols = $row -split "`t"
        if ($cols.Count -lt 8) { $keepLines += $row; continue }
        if ($cols[2] -eq 'neoforge' -and ($McVersions -contains $cols[0])) {
            $keepLines += $row
            $keptIds += "$($cols[0])-$($cols[2])-$($cols[3])"
        }
    }

    if ($keptIds.Count -eq 0) {
        throw "El filtro no dejo ningun target. Revisa -McVersions ($($McVersions -join ', '))."
    }

    [System.IO.File]::WriteAllText(
        $catalogPath,
        (($keepLines -join "`n") + "`n"),
        (New-Object System.Text.UTF8Encoding($false)))
    Write-Host ("   se compilaran: " + ($keptIds -join ', ')) -ForegroundColor Green
    Write-Host '   el catalogo del paquete solo listara esos targets' -ForegroundColor DarkGray
}

Write-Host '== Cache de compilacion' -ForegroundColor Cyan
$mixin = Get-ChildItem '.\staging\cache\gameDir\libraries\net\fabricmc\sponge-mixin' -Recurse -Filter 'sponge-mixin-*.jar' -ErrorAction SilentlyContinue | Select-Object -First 1
$remap = Test-Path '.\staging\cache\gameDir\.fabric\remappedJars'
if (-not $mixin -or -not $remap) {
    Write-Host '   falta la cache; ejecutando scripts\setup.ps1 (descarga y puede tardar)' -ForegroundColor Yellow
    & powershell -NoProfile -ExecutionPolicy Bypass -File '.\scripts\setup.ps1'
    if ($LASTEXITCODE) { throw "setup.ps1 fallo ($LASTEXITCODE). Copia el error de arriba y mandamelo." }
    Write-Host '   cache lista' -ForegroundColor Green
} else {
    Write-Host '   cache ya presente' -ForegroundColor DarkGray
}

if ($SkipBuild) {
    Write-Host '== -SkipBuild: no compilo' -ForegroundColor Cyan
    return
}

Write-Host '== Compilando (esto tarda; lo ya compilado se salta)' -ForegroundColor Cyan
# La version importa: la consola no reemplaza un paquete instalado por otro de version
# igual o mayor. Un build local sin .local\app_build.txt pondria 1.0.0.0 y no se
# instalaria encima del nightly, asi que se fija una version superior a proposito.
Write-Host "   version del paquete: $AppxVersion" -ForegroundColor DarkGray
& powershell -NoProfile -ExecutionPolicy Bypass -File '.\build.ps1' -AppxVersion $AppxVersion
if ($LASTEXITCODE) { throw "El build fallo con codigo $LASTEXITCODE. Copia el error de arriba y mandamelo." }

$appx = Get-ChildItem '.\output' -Filter '*.appx' -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $appx) { throw 'El build termino pero no encuentro ninguna APPX en output\.' }

Write-Host ''
Write-Host '== Comprobando el paquete' -ForegroundColor Cyan
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::OpenRead($appx.FullName)
try {
    $entries = @($zip.Entries | ForEach-Object { $_.FullName })
} finally {
    $zip.Dispose()
}
Write-Host ("   entradas en el paquete: " + $entries.Count)
foreach ($needle in @('MC.Xbox.exe', 'opengl32.dll', 'libgallium_wgl.dll', 'dxil.dll',
                      'spirv_to_dxil.dll', 'z-1.dll', 'vulkan_dzn.dll', 'glfw.dll',
                      'jnidispatch.dll', 'runtime/version_catalog.tsv',
                      'xbox_security.properties', 'securejarhandler-uwp-patch.jar')) {
    $hit = $entries | Where-Object { $_ -like "*$needle*" } | Select-Object -First 1
    if ($hit) {
        Write-Host ("   OK      " + $needle) -ForegroundColor DarkGray
    } else {
        Write-Host ("   FALTA   " + $needle) -ForegroundColor Red
    }
}

Write-Host ''
Write-Host '== Certificado' -ForegroundColor Cyan
$signature = Get-AuthenticodeSignature -FilePath $appx.FullName
$cerPath = [System.IO.Path]::ChangeExtension($appx.FullName, '.cer')
if ($signature.SignerCertificate) {
    Export-Certificate -Cert $signature.SignerCertificate -FilePath $cerPath -Force | Out-Null
    Write-Host ('   firmado con: ' + $signature.SignerCertificate.Subject)
    Write-Host ('   huella:      ' + $signature.SignerCertificate.Thumbprint)
    Write-Host ('   .cer:        ' + $cerPath) -ForegroundColor Green
    Write-Host ''
    Write-Host '   Si la consola no confia en este certificado, la app se instala pero no abre.' -ForegroundColor Gray
    Write-Host '   Para confiar en el en el PC:' -ForegroundColor Gray
    Write-Host ('     Import-Certificate -FilePath "' + $cerPath + '" -CertStoreLocation Cert:\LocalMachine\TrustedPeople') -ForegroundColor Gray
} else {
    Write-Warning 'El paquete no tiene certificado de firma.'
}

Write-Host ''
Write-Host ('APPX lista: ' + $appx.FullName) -ForegroundColor Green
Write-Host ('Version:    ' + $AppxVersion + '  (el nightly publicado va por 1.0.0.99)') -ForegroundColor Green
