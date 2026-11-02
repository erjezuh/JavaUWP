# Failed build log

Total lines: 46

Build native NeoForge APPX	Build native APPX	﻿2026-11-02T14:17:19.7475214Z ##[group]Run # The fork defaults to NeoForge 1.21.1; an explicit workflow input can
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7475909Z ^[[36;1m# The fork defaults to NeoForge 1.21.1; an explicit workflow input can^[[0m
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7476409Z ^[[36;1m# retarget the package to any other version in config/versions.tsv.^[[0m
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7476816Z ^[[36;1mif ($env:DEFAULT_LOADER_VERSION) {^[[0m
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7477224Z ^[[36;1m  $row = @(Import-Csv config\versions.tsv -Delimiter "`t" | Where-Object {^[[0m
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7477767Z ^[[36;1m    $_.loader -eq "neoforge" -and $_.loaderVersion -eq $env:DEFAULT_LOADER_VERSION^[[0m
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7478168Z ^[[36;1m  })^[[0m
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7478540Z ^[[36;1m  if ($row.Count -ne 1) { throw "Unknown NeoForge loader version $env:DEFAULT_LOADER_VERSION" }^[[0m
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7479069Z ^[[36;1m  $env:DEFAULT_MC_VERSION = $row[0].minecraftVersion^[[0m
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7479487Z ^[[36;1m  $env:DEFAULT_LAUNCH_VERSION = "neoforge-$($row[0].loaderVersion)"^[[0m
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7479930Z ^[[36;1m  $env:DEFAULT_JAVA_RUNTIME = $row[0].javaRuntime^[[0m
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7480322Z ^[[36;1m  $env:DEFAULT_CONTROLLER_PROVIDER = $row[0].controllerProvider^[[0m
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7480714Z ^[[36;1m  $env:MC_VERSION = $row[0].minecraftVersion^[[0m
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7480984Z ^[[36;1m}^[[0m
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7481288Z ^[[36;1m# hashtable splat: switch parameters must be bound by name, not passed^[[0m
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7482145Z ^[[36;1m# as positional strings^[[0m
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7482653Z ^[[36;1m$buildArgs = @{ SkipStopAppProcesses = $true; ReleaseChannel = "nightly-native" }^[[0m
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7483098Z ^[[36;1mif ($env:FAST_BUILD -eq "true") {^[[0m
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7483506Z ^[[36;1m  Write-Host "Fast build: skipping per-version manifests and per-version mods"^[[0m
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7483961Z ^[[36;1m  $buildArgs.SkipVersionManifests = $true^[[0m
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7484289Z ^[[36;1m  $buildArgs.SkipVersionCompat = $true^[[0m
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7484555Z ^[[36;1m}^[[0m
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7484745Z ^[[36;1m.\build.ps1 @buildArgs^[[0m
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7553792Z shell: C:\Windows\System32\WindowsPowerShell\v1.0\powershell.EXE -command ". '{0}'"
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7554216Z env:
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7554495Z   JAVA_HOME: C:\hostedtoolcache\windows\Java_Microsoft_jdk\25.0.3\x64
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7554951Z   JAVA_HOME_17_X64: C:\hostedtoolcache\windows\Java_Microsoft_jdk\17.0.19\x64
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7555311Z   MAVEN_ARGS: -ntp
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7555600Z   JDK17_HOME: C:\hostedtoolcache\windows\Java_Microsoft_jdk\17.0.19\x64
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7556028Z   JAVA17_HOME: C:\hostedtoolcache\windows\Java_Microsoft_jdk\17.0.19\x64
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7556472Z   JAVA_HOME_21_X64: C:\hostedtoolcache\windows\Java_Microsoft_jdk\21.0.11\x64
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7556906Z   JAVA21_HOME: C:\hostedtoolcache\windows\Java_Microsoft_jdk\21.0.11\x64
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7557519Z   JDK21_HOME: C:\hostedtoolcache\windows\Java_Microsoft_jdk\21.0.11\x64
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7557953Z   JAVA_HOME_25_X64: C:\hostedtoolcache\windows\Java_Microsoft_jdk\25.0.3\x64
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7558317Z   DEFAULT_LOADER_VERSION: 
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7558532Z   FAST_BUILD: 
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:19.7558718Z ##[endgroup]
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:20.2513922Z === Build preflight === [last phase 0.0s | elapsed 0.0s]
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:21.3099267Z APPX version must have four numeric fields: 1.0.0.13.0
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:21.3100014Z At D:\a\JavaUWP\JavaUWP\build.ps1:116 char:9
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:21.3100613Z +         throw "APPX version must have four numeric fields: $Version"
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:21.3101258Z +         ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:21.3101966Z     + CategoryInfo          : OperationStopped: (APPX version mu...lds: 1.0.0.13.0:String) [], RuntimeException
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:21.3102620Z     + FullyQualifiedErrorId : APPX version must have four numeric fields: 1.0.0.13.0
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:21.3103019Z  
Build native NeoForge APPX	Build native APPX	2026-11-02T14:17:21.3331033Z ##[error]Process completed with exit code 1.
