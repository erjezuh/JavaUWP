#pragma once

#include <string>

namespace javauwp {

// Win32 file APIs silently reject paths longer than MAX_PATH (260 characters)
// unless they are given the extended-length form. Minecraft's Maven
// coordinates can be very long: com.google.guava:listenablefuture:
// 9999.0-empty-to-avoid-conflict-with-guava produces a 153 character relative
// path, and the Xbox LocalState prefix pushes the full path over the limit.
// That single entry is the one that never finishes in a 4021 file download.
//
// This helper is pure string manipulation so it can be unit tested on any OS.
inline std::wstring ToExtendedPath(const std::wstring& path) {
    if (path.size() >= 4 && path.compare(0, 4, L"\\\\?\\") == 0) {
        return path;
    }

    std::wstring normalized = path;
    for (wchar_t& ch : normalized) {
        if (ch == L'/') ch = L'\\';
    }

    // Local paths: C:\... -> \\?\C:\...
    if (normalized.size() >= 2 && normalized[1] == L':') {
        return L"\\\\?\\" + normalized;
    }
    // UNC paths: \\server\share -> \\?\UNC\server\share
    if (normalized.size() >= 2 && normalized[0] == L'\\' && normalized[1] == L'\\') {
        return L"\\\\?\\UNC\\" + normalized.substr(2);
    }
    // Relative paths cannot be extended; leave them untouched.
    return normalized;
}

// Only opt into the extended form when a plain Win32 path is about to exceed
// MAX_PATH (260 including the terminating NUL). Paths that still fit keep the
// regular, well-tested API behaviour - which matters on UWP/Xbox where the
// \\?\ device form is not always available.
inline std::wstring ToSafeFilePath(const std::wstring& path) {
    return path.size() >= 256 ? ToExtendedPath(path) : path;
}

}  // namespace javauwp
