// Portable checks for the extended-length Win32 path helper. The UWP build
// needs MSVC, but this header is pure string manipulation, so it can be tested
// on any host with a C++17 compiler.
#include "../MC.Xbox/common/win32_long_path.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using javauwp::ToExtendedPath;
using javauwp::ToSafeFilePath;

static int failures = 0;

static void Check(const char* name, const std::wstring& actual, const std::wstring& expected) {
    if (actual == expected) {
        std::printf("  ok   %s\n", name);
    } else {
        std::printf("  FAIL %s\n", name);
        std::wprintf(L"       got: %ls\n  expected: %ls\n", actual.c_str(), expected.c_str());
        ++failures;
    }
}

int main() {
    std::printf("Win32 long path helper:\n");

    Check("drive path gets the extended prefix",
        ToExtendedPath(L"C:\\LocalState\\game\\libraries\\a.jar"),
        L"\\\\?\\C:\\LocalState\\game\\libraries\\a.jar");

    Check("forward slashes are normalized",
        ToExtendedPath(L"C:/LocalState/a/b.jar"),
        L"\\\\?\\C:\\LocalState\\a\\b.jar");

    Check("already extended is left alone",
        ToExtendedPath(L"\\\\?\\C:\\LocalState\\a.jar"),
        L"\\\\?\\C:\\LocalState\\a.jar");

    Check("UNC path uses the UNC device form",
        ToExtendedPath(L"\\\\server\\share\\a.jar"),
        L"\\\\?\\UNC\\server\\share\\a.jar");

    Check("relative path is left alone",
        ToExtendedPath(L"game/libraries/a.jar"),
        L"game\\libraries\\a.jar");

    Check("short path keeps the plain form",
        ToSafeFilePath(std::wstring(80, L'a')),
        std::wstring(80, L'a'));

    const std::wstring belowLimit = L"C:\\" + std::wstring(245, L'a');
    Check("path that still fits MAX_PATH stays plain",
        ToSafeFilePath(belowLimit),
        belowLimit);

    const std::wstring longDrive = L"C:\\" + std::wstring(260, L'a');
    Check("path over MAX_PATH is extended",
        ToSafeFilePath(longDrive),
        L"\\\\?\\" + longDrive);

    if (failures) {
        std::printf("%d Win32 long path test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("All Win32 long path tests passed\n");
    return EXIT_SUCCESS;
}
