// Portable tests for the GLFW shim's per-frame caching policy.
// Builds on Linux/macOS with plain g++: perf_cache.h has no Windows includes.
#include "../glfw_shim/perf_cache.h"

#include <cstdio>
#include <initializer_list>

static int g_failures = 0;

static void Expect(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL %s\n", what);
        ++g_failures;
    }
}

int main() {
    using bandit_perf::EnvFlagEnabledValue;
    using bandit_perf::EnvFlagDisabledValue;
    using bandit_perf::CacheExpired;

    // Enabled spellings are the ones the launcher writes to the environment.
    for (const wchar_t* value : { L"1", L"true", L"TRUE", L"True", L"yes", L"YES", L"on", L"ON" }) {
        Expect(EnvFlagEnabledValue(value), "enabled spelling must be accepted");
        Expect(!EnvFlagDisabledValue(value), "enabled spelling must not disable");
    }
    for (const wchar_t* value : { L"0", L"false", L"FALSE", L"False", L"no", L"NO", L"off", L"OFF" }) {
        Expect(EnvFlagDisabledValue(value), "disabled spelling must be accepted");
        Expect(!EnvFlagEnabledValue(value), "disabled spelling must not enable");
    }
    // Unknown, empty and null values keep the previous conservative defaults.
    for (const wchar_t* value : { L"", L"2", L"trueish", L"offline", L" true", L"true ", L"nil" }) {
        Expect(!EnvFlagEnabledValue(value), "unknown value must not enable");
        Expect(!EnvFlagDisabledValue(value), "unknown value must not disable");
    }
    Expect(!EnvFlagEnabledValue(nullptr), "null must not enable");
    Expect(!EnvFlagDisabledValue(nullptr), "null must not disable");

    // Cache policy: first query, interval boundary, wrap-around and forced
    // invalidation. Dirty must win even inside the interval.
    Expect(CacheExpired(1000, 0, 500, false), "never-queried cache must refresh");
    Expect(CacheExpired(1000, 400, 500, false), "expired cache must refresh");
    Expect(CacheExpired(1000, 500, 500, false), "cache must refresh exactly at the boundary");
    Expect(!CacheExpired(1000, 700, 500, false), "cache must not refresh inside the interval");
    Expect(CacheExpired(1000, 700, 500, true), "dirty cache must refresh immediately");
    Expect(CacheExpired(10, 0xFFFFFFFFFFFFFF00ull, 500, false), "wrapped tick counter must refresh");
    Expect(CacheExpired(0, 0, 500, false), "never-queried cache must refresh at tick zero");

    if (g_failures) {
        std::fprintf(stderr, "%d perf cache test(s) failed\n", g_failures);
        return 1;
    }
    std::printf("GLFW shim perf-cache checks passed (env flags, cache expiry, wrap, dirty)\n");
    return 0;
}
