// Portable unit tests for the pure NeoForge version helpers. The UWP build
// cannot run on Linux, but these helpers have no Windows dependencies, so the
// resolution logic that used to break NeoForge 1.21.1 can be tested here.
//
// Build/run with tests/run-tests.sh (g++ -std=c++17).

#include "../MC.Xbox/launch/loaders/neoforge_version.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void ExpectEqual(const std::wstring& actual, const std::wstring& expected, const std::string& label) {
    if (actual == expected) {
        std::cout << "  ok   " << label << "\n";
        return;
    }
    std::cout << "  FAIL " << label << ": expected '" << std::string(expected.begin(), expected.end())
              << "', got '" << std::string(actual.begin(), actual.end()) << "'\n";
    ++failures;
}

}  // namespace

int main() {
    std::cout << "NeoForgeVersionFromLaunchVersion:\n";
    ExpectEqual(NeoForgeVersionFromLaunchVersion(L"neoforge-21.1.250"), L"21.1.250", "classic id");
    ExpectEqual(NeoForgeVersionFromLaunchVersion(L"1.21.1-neoforge-21.1.250"), L"21.1.250", "mc-prefixed id");
    ExpectEqual(NeoForgeVersionFromLaunchVersion(L"21.1.250"), L"21.1.250", "bare version");
    ExpectEqual(NeoForgeVersionFromLaunchVersion(L"neoforge-21.1.1-beta"), L"21.1.1-beta", "beta id");
    ExpectEqual(NeoForgeVersionFromLaunchVersion(L"neoforge-26.2.0.82"), L"26.2.0.82", "modern id");
    ExpectEqual(NeoForgeVersionFromLaunchVersion(L"fabric-loader-0.19.5-1.21.1"), L"", "fabric id rejected");
    ExpectEqual(NeoForgeVersionFromLaunchVersion(L"1.20.1-forge-47.4.20"), L"", "forge id rejected");
    ExpectEqual(NeoForgeVersionFromLaunchVersion(L"neoforge-"), L"", "empty suffix rejected");
    ExpectEqual(NeoForgeVersionFromLaunchVersion(L""), L"", "empty id rejected");

    std::cout << "ResolveNeoForgeLoaderVersion:\n";
    ExpectEqual(ResolveNeoForgeLoaderVersion(L"neoforge-21.1.250", L"21.1.250"), L"21.1.250", "catalog wins");
    ExpectEqual(ResolveNeoForgeLoaderVersion(L"1.21.1-neoforge-21.1.233", L"21.1.250"), L"21.1.250",
                "catalog version overrides stale installer id");
    ExpectEqual(ResolveNeoForgeLoaderVersion(L"neoforge-21.1.233", L"none"), L"21.1.233", "none falls back to id");
    ExpectEqual(ResolveNeoForgeLoaderVersion(L"neoforge-20.4.237", L"selected"), L"20.4.237", "selected falls back to id");
    ExpectEqual(ResolveNeoForgeLoaderVersion(L"fabric-loader-0.19.5-1.21.1", L"selected"), L"",
                "unusable metadata does not produce a bogus version");
    ExpectEqual(ResolveNeoForgeLoaderVersion(L"", L""), L"", "no metadata at all");

    if (failures) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All NeoForge version tests passed\n";
    return 0;
}
