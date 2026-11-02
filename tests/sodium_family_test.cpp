// Portable unit test for the renderer classification used by
// MC.Xbox/mods/mod_defaults.cpp. The launcher runs through Mesa's WGL backend
// inside UWP, so Forge's Sodium ports (Embeddium/Rubidium) need the same
// conservative defaults as Fabric's Sodium -- but their jars must be told apart
// from addons such as Sodium Extra or TexTrue's Embeddium Options.

#include "../MC.Xbox/mods/sodium_family.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void ExpectFamily(const wchar_t* fileName, const wchar_t* expectedToken) {
    const SodiumFamily* family = SodiumFamilyForFileName(fileName);
    const std::wstring got = family ? std::wstring(family->token) : std::wstring(L"(none)");
    const std::wstring want = expectedToken ? std::wstring(expectedToken) : std::wstring(L"(none)");
    if (got != want) {
        std::fwprintf(stderr, L"FAIL %ls: expected %ls, got %ls\n", fileName, want.c_str(), got.c_str());
        ++g_failures;
    }
}

void ExpectConfig(const wchar_t* fileName, const wchar_t* expectedConfig) {
    const SodiumFamily* family = SodiumFamilyForFileName(fileName);
    if (!family || std::wstring(family->configFile) != expectedConfig) {
        std::fwprintf(stderr, L"FAIL %ls: expected config %ls\n", fileName, expectedConfig);
        ++g_failures;
    }
}

SodiumRendererJar Jar(const wchar_t* fileName) {
    SodiumRendererJar jar;
    jar.family = SodiumFamilyForFileName(fileName);
    jar.name = fileName;
    return jar;
}

}  // namespace

int main() {
    // Real renderer jars for Forge 1.20.1 and Fabric.
    ExpectFamily(L"embeddium-0.3.31+mc1.20.1.jar", L"embeddium");
    ExpectFamily(L"Embeddium-0.3.25+mc1.20.1.jar", L"embeddium");
    ExpectFamily(L"rubidium-0.7.1.jar", L"rubidium");
    ExpectFamily(L"sodium-fabric-mc1.20.1-0.5.11.jar", L"sodium");
    ExpectFamily(L"sodium-0.5.8.jar", L"sodium");
    ExpectConfig(L"embeddium-0.3.31+mc1.20.1.jar", L"embeddium-options.json");
    ExpectConfig(L"rubidium-0.7.1.jar", L"rubidium-options.json");
    ExpectConfig(L"sodium-0.5.8.jar", L"sodium-options.json");

    // Addons, shaders and unrelated mods must never be disabled or mistaken
    // for the profile's renderer.
    ExpectFamily(L"sodium-extra-0.5.6-mc1.20.1.jar", nullptr);
    ExpectFamily(L"embeddium-extra-0.4.3+mc1.20.1.jar", nullptr);
    ExpectFamily(L"reeses-sodium-options-1.7.2.jar", nullptr);
    ExpectFamily(L"textrues-embeddium-options-0.1.5+mc1.20.1.jar", nullptr);
    ExpectFamily(L"sodiumdynamiclights-1.0.jar", nullptr);
    ExpectFamily(L"oculus-mc1.20.1-1.8.jar", nullptr);
    ExpectFamily(L"iris-1.7.2.jar", nullptr);
    ExpectFamily(L"indium-1.0.9.jar", nullptr);
    ExpectFamily(L"banditvault-xbox-compat-1.0.0.jar", nullptr);
    ExpectFamily(L"controlify-3.0.1+lts+1.21.1-neoforge.jar", nullptr);
    ExpectFamily(L"modernfix-5.19.0+forge-1.20.1.jar", nullptr);
    ExpectFamily(L"entityculling-1.20.1.jar", nullptr);

    // Embeddium wins over Rubidium and Sodium; Rubidium alone still works and
    // still gets its own config seeded.
    std::vector<SodiumRendererJar> mixed = {
        Jar(L"rubidium-0.7.1.jar"),
        Jar(L"embeddium-0.3.31+mc1.20.1.jar"),
        Jar(L"sodium-0.5.8.jar"),
    };
    const size_t winner = PreferredSodiumFamilyIndex(mixed);
    if (std::wstring(mixed[winner].family->token) != L"embeddium") {
        std::fwprintf(stderr, L"FAIL conflict resolution did not keep Embeddium\n");
        ++g_failures;
    }
    std::vector<SodiumRendererJar> rubidiumOnly = { Jar(L"rubidium-0.7.1.jar") };
    if (PreferredSodiumFamilyIndex(rubidiumOnly) != 0 ||
        std::wstring(rubidiumOnly[0].family->configFile) != L"rubidium-options.json") {
        std::fwprintf(stderr, L"FAIL Rubidium-only profile is not configured\n");
        ++g_failures;
    }

    // Embeddium 0.3.x has no arena allocator option; Sodium/Rubidium do.
    if (SodiumFamilyForFileName(L"embeddium-0.3.31.jar")->hasArenaAllocator ||
        !SodiumFamilyForFileName(L"rubidium-0.7.1.jar")->hasArenaAllocator ||
        !SodiumFamilyForFileName(L"sodium-0.5.8.jar")->hasArenaAllocator) {
        std::fwprintf(stderr, L"FAIL arena allocator capability flags are wrong\n");
        ++g_failures;
    }

    if (g_failures) {
        std::fwprintf(stderr, L"%d sodium family test(s) failed\n", g_failures);
        return 1;
    }
    std::wprintf(L"sodium family checks passed (renderers, addons, conflict winner, configs)\n");
    return 0;
}
