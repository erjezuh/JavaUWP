#pragma once

#include <string>

// Pure helpers for resolving the NeoForge loader version. Kept free of WinRT,
// JNI and filesystem dependencies so they can be unit tested outside the UWP
// build (see tests/neoforge_version_test.cpp).
//
// NeoForge installer profiles have used several id shapes over time:
//   neoforge-21.1.250                    (1.20.2 .. 1.21.x)
//   1.21.1-neoforge-21.1.250            (some mirrored/third party profiles)
//   21.1.250                            (bare version, older/manual profiles)
//   1.20.1-forge-47.4.20 / fabric-...   (other loaders, must not be accepted)
inline std::wstring NeoForgeVersionFromLaunchVersion(const std::wstring& launchVersion) {
    const std::wstring marker = L"neoforge-";
    std::wstring candidate = launchVersion;

    const size_t markerPos = launchVersion.find(marker);
    if (markerPos != std::wstring::npos) {
        candidate = launchVersion.substr(markerPos + marker.size());
    } else if (launchVersion.find(L"fabric") != std::wstring::npos ||
               launchVersion.find(L"forge-") != std::wstring::npos) {
        // Clearly another loader's id. Returning it whole used to produce
        // nonsense Maven coordinates like net/neoforged/neoforge/1.21.1-forge-...
        return L"";
    }

    // A real loader version always starts with a digit (including betas such
    // as 21.1.1-beta). Anything else is not usable as a Maven version.
    if (candidate.empty() || candidate[0] < L'0' || candidate[0] > L'9') {
        return L"";
    }
    return candidate;
}

// The version catalog row is authoritative: it is the exact version whose
// installer, installertools and patched client artifacts were downloaded.
// Fall back to parsing the installer's version id only for profiles that do not
// come from the catalog (e.g. legacy saved profiles without a target id).
inline std::wstring ResolveNeoForgeLoaderVersion(
    const std::wstring& launchVersion,
    const std::wstring& catalogLoaderVersion) {
    if (!catalogLoaderVersion.empty() &&
        catalogLoaderVersion != L"none" &&
        catalogLoaderVersion != L"selected") {
        return catalogLoaderVersion;
    }
    return NeoForgeVersionFromLaunchVersion(launchVersion);
}
