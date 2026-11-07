#pragma once

// Portable helpers for the GLFW shim's per-frame hot path. This header has no
// Windows/WinRT includes on purpose: tests/perf_cache_test.cpp compiles it on
// Linux/macOS with plain g++, so the caching policy stays covered by
// tests/run-tests.sh even though the shim itself needs MSVC + the UWP SDK.
//
// On Xbox the rasteriser (Mesa over D3D12) shares CPU time with the render
// thread, so work repeated on every frame in the shim competes directly with
// drawing. None of this changes any graphics setting: it only removes lookups
// whose value cannot change between frames.

#include <cstddef>

namespace bandit_perf {

namespace detail {

inline wchar_t AsciiLower(wchar_t c) {
    return (c >= L'A' && c <= L'Z') ? (wchar_t)(c - L'A' + L'a') : c;
}

inline bool AsciiEqualsIgnoreCase(const wchar_t* a, const wchar_t* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (AsciiLower(*a) != AsciiLower(*b)) return false;
        ++a;
        ++b;
    }
    return *a == L'\0' && *b == L'\0';
}

inline bool MatchesAny(const wchar_t* value, const wchar_t* const* words,
                       std::size_t count) {
    if (!value || !*value) return false;
    for (std::size_t i = 0; i < count; ++i) {
        if (AsciiEqualsIgnoreCase(value, words[i])) return true;
    }
    return false;
}

} // namespace detail

// The launcher writes these environment variables before starting the JVM and
// never changes them afterwards. Parsing is kept separate from the Win32 read
// so it is portable and testable; callers evaluate the read once into a
// function-local static instead of paying for it on every frame.
inline bool EnvFlagEnabledValue(const wchar_t* value) {
    static const wchar_t* const kTrue[] = { L"1", L"true", L"yes", L"on" };
    return detail::MatchesAny(value, kTrue, sizeof(kTrue) / sizeof(kTrue[0]));
}

inline bool EnvFlagDisabledValue(const wchar_t* value) {
    static const wchar_t* const kFalse[] = { L"0", L"false", L"no", L"off" };
    return detail::MatchesAny(value, kFalse, sizeof(kFalse) / sizeof(kFalse[0]));
}

// A time-based cache that also survives a GetTickCount64 wrap and can be
// forced by an explicit invalidation. `lastMs == 0` means "never queried".
inline bool CacheExpired(unsigned long long nowMs,
                         unsigned long long lastMs,
                         unsigned long long intervalMs,
                         bool dirty) {
    if (dirty || lastMs == 0) return true;
    if (nowMs < lastMs) return true; // counter wrapped
    return (nowMs - lastMs) >= intervalMs;
}

} // namespace bandit_perf
