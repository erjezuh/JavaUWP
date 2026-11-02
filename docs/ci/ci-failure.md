== token scopes ==
HTTP/2.0 200 OK
X-Accepted-Github-Permissions: metadata=read
== probe: can GITHUB_TOKEN create a release? ==
https://github.com/erjezuh/JavaUWP/releases/tag/untagged-b1ea1c97dc7858607d71
To https://github.com/erjezuh/JavaUWP
 - [deleted]         ci-permission-probe
== gh run view --log-failed ==
Build native NeoForge APPX	Publish nightly-native release	﻿2026-11-02T12:21:15.3011570Z ##[group]Run .\scripts\publish-release.ps1 `
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:15.3011986Z ^[[36;1m.\scripts\publish-release.ps1 `^[[0m
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:15.3012229Z ^[[36;1m  -Channel nightly-native `^[[0m
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:15.3012438Z ^[[36;1m  -Sha $env:GITHUB_SHA `^[[0m
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:15.3012653Z ^[[36;1m  -AppxVersion $env:APPX_VERSION `^[[0m
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:15.3013042Z ^[[36;1m  -RunUrl "$env:GITHUB_SERVER_URL/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID" `^[[0m
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:15.3013673Z ^[[36;1m  -Asset dist\BanditLauncherNative-neoforge.appx, dist\BanditLauncherNative-neoforge.sha256, dist\BanditLauncherNative-neoforge.cer^[[0m
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:15.3079851Z shell: C:\Windows\System32\WindowsPowerShell\v1.0\powershell.EXE -command ". '{0}'"
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:15.3080191Z env:
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:15.3080417Z   JAVA_HOME: C:\hostedtoolcache\windows\Java_Microsoft_jdk\25.0.3\x64
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:15.3080796Z   JAVA_HOME_17_X64: C:\hostedtoolcache\windows\Java_Microsoft_jdk\17.0.19\x64
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:15.3081087Z   MAVEN_ARGS: -ntp
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:15.3081366Z   JDK17_HOME: C:\hostedtoolcache\windows\Java_Microsoft_jdk\17.0.19\x64
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:15.3081707Z   JAVA17_HOME: C:\hostedtoolcache\windows\Java_Microsoft_jdk\17.0.19\x64
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:15.3082055Z   JAVA_HOME_21_X64: C:\hostedtoolcache\windows\Java_Microsoft_jdk\21.0.11\x64
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:15.3082395Z   JAVA21_HOME: C:\hostedtoolcache\windows\Java_Microsoft_jdk\21.0.11\x64
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:15.3082712Z   JDK21_HOME: C:\hostedtoolcache\windows\Java_Microsoft_jdk\21.0.11\x64
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:15.3083048Z   JAVA_HOME_25_X64: C:\hostedtoolcache\windows\Java_Microsoft_jdk\25.0.3\x64
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:15.3083323Z   APPX_VERSION: 1.0.0.0
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:15.3085575Z   GH_TOKEN: ***
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:15.3085971Z ##[endgroup]
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:20.4300741Z HTTP 403: Resource not accessible by integration (https://api.github.com/repos/erjezuh/JavaUWP/releases)
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:20.5935878Z gh release create failed with exit code 1
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:20.5936463Z At D:\a\JavaUWP\JavaUWP\scripts\publish-release.ps1:94 char:24
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:20.5937302Z + ... $code -ne 0) { throw "gh release $verb failed with exit code $code" }
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:20.5937822Z +                    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:20.5938402Z     + CategoryInfo          : OperationStopped: (gh release crea...ith exit code 1:String) [], RuntimeException
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:20.5939066Z     + FullyQualifiedErrorId : gh release create failed with exit code 1
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:20.5939974Z  
Build native NeoForge APPX	Publish nightly-native release	2026-11-02T12:21:20.6133432Z ##[error]Process completed with exit code 1.
== api job logs ==
Job: 125259294094
/tmp/job-logs.zip: ASCII text
the response contains terminal escape sequences; pass --allow-escape-sequences to output it anyway

could not unzip job logs
