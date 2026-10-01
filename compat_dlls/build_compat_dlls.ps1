# build_compat_dlls.ps1 - Build the system-DLL stand-ins (ole32, oleaut32)
# used by the launcher's mod-compatibility probe when the console refuses an
# explicit LoadLibrary of the desktop system DLL.
param(
    [string]$OutputDir
)

$ErrorActionPreference = "Stop"

. (Join-Path (Split-Path $PSScriptRoot -Parent) "scripts\common.ps1")

$tools = Resolve-VSTools
$sdk = Resolve-WindowsSdk
$sdkRoot = $sdk.Root
$sdkVer = $sdk.Version

if (-not $OutputDir) {
    $OutputDir = Join-Path (Get-ConfigPath "BuildDir") "compat_dlls"
}
$OutputDir = (New-Item -ItemType Directory -Force -Path $OutputDir).FullName

$env:INCLUDE = "$($tools.MsvcRoot)\include;" +
               "${sdkRoot}Include\$sdkVer\ucrt;" +
               "${sdkRoot}Include\$sdkVer\shared;" +
               "${sdkRoot}Include\$sdkVer\um;" +
               "${sdkRoot}Include\$sdkVer\winrt"
$env:LIB = "$($tools.MsvcRoot)\lib\x64;" +
           "${sdkRoot}Lib\$sdkVer\ucrt\x64;" +
           "${sdkRoot}Lib\$sdkVer\um\x64"

$targets = @(
    @{ Name = "ole32";     Source = "ole32.cpp";     Libs = @("bcrypt.lib", "kernel32.lib") },
    @{ Name = "oleaut32";  Source = "oleaut32.cpp";  Libs = @("kernel32.lib") }
)

$sources = @(Get-ChildItem $PSScriptRoot -File -Include *.cpp, *.h, *.def -Recurse | Select-Object -ExpandProperty FullName)
$stampPath = Join-Path $OutputDir "build.stamp"
$outputs = @($targets | ForEach-Object { Join-Path $OutputDir "$($_.Name).dll" })
$stamp = New-BuildStamp `
    -Values @("compat_dlls", $tools.ClExe, $sdkVer) `
    -ContentFiles (@($PSCommandPath) + $sources) `
    -DependencyFiles @()

if (Test-BuildStampCurrent -StampPath $stampPath -Stamp $stamp -RequiredOutputs $outputs) {
    Write-Host "compat dlls up to date -> $OutputDir"
    return
}

Push-Location $PSScriptRoot
try {
    foreach ($t in $targets) {
        $dllPath = Join-Path $OutputDir "$($t.Name).dll"
        $objPath = Join-Path $OutputDir "$($t.Name).obj"
        Write-Host "Building $($t.Name).dll (mod-compat stand-in)..."
        & $tools.ClExe $t.Source /LD /EHsc /std:c++17 $CommonClFlags /O2 /GL /Gw /MT /DNDEBUG /D_UNICODE /DUNICODE /D_WIN32_WINNT=0x0A00 /I"$sdkRoot\Include\$sdkVer\um" /DEF:"$PSScriptRoot\$($t.Name).def" /Fo"$objPath" `
            /link /LTCG /OUT:"$dllPath" /MACHINE:X64 @($t.Libs)
        if ($LASTEXITCODE -ne 0) { throw "$($t.Name) compat build FAILED" }
        Write-Host "$($t.Name).dll built OK -> $dllPath"
    }
} finally {
    Pop-Location
}
Set-BuildStamp -StampPath $stampPath -Stamp $stamp
Write-Host "compat dlls built -> $OutputDir"
