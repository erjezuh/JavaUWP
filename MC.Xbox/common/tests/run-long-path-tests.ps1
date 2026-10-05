# keeps the extended-length path helper testable without a uwp or appx build
param(
    [switch]$KeepExe
)

$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
. (Join-Path $root "scripts\common.ps1")

$tools = Resolve-VSTools
$sdk = Resolve-WindowsSdk
$sdkRoot = $sdk.Root
$sdkVer = $sdk.Version

$outDir = Join-Path $root "staging\build\long_path_tests"
Ensure-Dir $outDir
$exe = Join-Path $outDir "long_path_tests.exe"

$env:INCLUDE = "$($tools.MsvcRoot)\include;${sdkRoot}Include\$sdkVer\ucrt;${sdkRoot}Include\$sdkVer\shared;${sdkRoot}Include\$sdkVer\um"
$env:LIB = "$($tools.MsvcRoot)\lib\x64;${sdkRoot}Lib\$sdkVer\ucrt\x64;${sdkRoot}Lib\$sdkVer\um\x64"

Push-Location (Join-Path $root "MC.Xbox\common")
try {
    & $tools.ClExe tests\long_path_tests.cpp long_path.cpp `
        /std:c++17 /EHsc $CommonClFlags /Od /Zi /D_UNICODE /DUNICODE /D_WIN32_WINNT=0x0A00 `
        /I. /Fo"$outDir\" /Fd"$outDir\" `
        /link /SUBSYSTEM:CONSOLE /MACHINE:X64 /OUT:"$exe"
    if ($LASTEXITCODE -ne 0) { throw "compile failed" }
}
finally {
    Pop-Location
}

Write-Host ""
& $exe
$code = $LASTEXITCODE

if (-not $KeepExe) { Remove-Item $exe -Force -ErrorAction SilentlyContinue }
if ($code -ne 0) { throw "long path tests failed" }
Write-Host "LONG_PATH_TESTS_OK"
