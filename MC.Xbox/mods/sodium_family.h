#pragma once

// Classification of Sodium-derived renderers for the Mesa/WGL UWP graphics
// path. Deliberately free of Windows and WinRT types so the rules that decide
// which mod jar is a renderer (and which one is just an addon) can be unit
// tested on any host. See tests/sodium_family_test.cpp.

#include <cstddef>
#include <string>
#include <vector>

struct SodiumFamily {
    const wchar_t* token;
    const wchar_t* configFile;
    const wchar_t* mixinFile;
    int preference;          // higher wins when several are installed
    bool hasArenaAllocator;  // Embeddium 0.3.x has no arena allocator option
};

// Embeddium is the maintained Forge port and is preferred over a Fabric Sodium
// jar or an abandoned Rubidium jar when more than one is present in a profile.
inline const SodiumFamily kSodiumFamilies[] = {
    { L"embeddium", L"embeddium-options.json", L"embeddium-mixins.properties", 3, false },
    { L"sodium",    L"sodium-options.json",    L"sodium-mixins.properties",    2, true  },
    { L"rubidium",  L"rubidium-options.json",  L"rubidium-mixins.properties",  1, true  },
};

inline bool IsSodiumFamilyWordChar(wchar_t ch) {
    return (ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9');
}

inline std::wstring SodiumFamilyLowerName(std::wstring value) {
    for (wchar_t& ch : value) {
        if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
    }
    return value;
}

inline bool FileNameHasStandaloneToken(const std::wstring& lower, const wchar_t* token) {
    const std::wstring needle(token);
    size_t pos = lower.find(needle);
    while (pos != std::wstring::npos) {
        const size_t end = pos + needle.size();
        const bool leftOk = pos == 0 || !IsSodiumFamilyWordChar(lower[pos - 1]);
        const bool rightOk = end >= lower.size() || !IsSodiumFamilyWordChar(lower[end]);
        if (leftOk && rightOk) return true;
        pos = lower.find(needle, pos + 1);
    }
    return false;
}

// Addons carry a renderer's name but are not renderers: never disable them and
// never mistake one for the profile's renderer.
inline bool IsSodiumFamilyAddon(const std::wstring& lower) {
    static const wchar_t* kAddonTokens[] = {
        L"extra", L"options", L"reeses", L"textrues", L"dynamiclights",
        L"shadowy", L"nvidium", L"indium", L"oculus", L"iris",
        L"lazurite", L"compat",
    };
    for (const wchar_t* token : kAddonTokens) {
        if (lower.find(token) != std::wstring::npos) return true;
    }
    return false;
}

inline const SodiumFamily* SodiumFamilyForFileName(const std::wstring& fileName) {
    const std::wstring lower = SodiumFamilyLowerName(fileName);
    if (IsSodiumFamilyAddon(lower)) return nullptr;
    for (const SodiumFamily& family : kSodiumFamilies) {
        if (FileNameHasStandaloneToken(lower, family.token)) return &family;
    }
    return nullptr;
}

struct SodiumRendererJar {
    const SodiumFamily* family = nullptr;
    std::wstring name;
    std::wstring path;
};

inline size_t PreferredSodiumFamilyIndex(const std::vector<SodiumRendererJar>& jars) {
    size_t winner = 0;
    for (size_t i = 1; i < jars.size(); ++i) {
        if (jars[i].family->preference > jars[winner].family->preference) winner = i;
    }
    return winner;
}
