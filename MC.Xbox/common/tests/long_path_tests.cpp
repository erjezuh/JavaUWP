#include "../long_path.h"

#include <cstdio>
#include <cwchar>
#include <string>

namespace {

int g_failures = 0;
int g_checks = 0;

std::string Narrow(const std::wstring& value) {
    std::string out;
    for (wchar_t ch : value) out.push_back(ch > 0 && ch < 128 ? static_cast<char>(ch) : '?');
    return out;
}

void CheckEq(const std::wstring& actual, const std::wstring& expected, const char* what) {
    ++g_checks;
    if (actual == expected) return;
    ++g_failures;
    printf("FAIL %s\n  got:  %s\n  want: %s\n", what, Narrow(actual).c_str(), Narrow(expected).c_str());
}

void CheckTrue(bool condition, const char* what) {
    ++g_checks;
    if (condition) return;
    ++g_failures;
    printf("FAIL %s\n", what);
}

// the library that could never be downloaded on the native (mouse) launcher:
// NeoForge depends on guava, whose listenablefuture artifact has the longest
// library path in the manifest
const wchar_t* kNativeLocalState =
    L"Q:\\Users\\UserMgr0\\AppData\\Local\\Packages"
    L"\\Microsoft.MicrosoftEdge.BanditLauncher_8wekyb3d8bbwe\\LocalState";
const wchar_t* kRelayLocalState =
    L"Q:\\Users\\UserMgr0\\AppData\\Local\\Packages\\BanditVault.Launcher_h1j2k3l4m5n6p\\LocalState";
const wchar_t* kListenableFuture =
    L"\\game\\libraries\\com\\google\\guava\\listenablefuture"
    L"\\9999.0-empty-to-avoid-conflict-with-guava"
    L"\\listenablefuture-9999.0-empty-to-avoid-conflict-with-guava.jar";

void TestLongDownloadPath() {
    const std::wstring staging = std::wstring(kNativeLocalState) + kListenableFuture + L".download";
    const std::wstring extended = ExtendedLengthPath(staging);

    // 267 characters: the shipped launcher could not open this path, so the one
    // library never downloaded and the retry loop ran forever
    CheckTrue(staging.size() > 260, "native staging path is over MAX_PATH");
    CheckTrue(extended.size() == staging.size() + 4, "extended path keeps the path and adds the marker");
    CheckTrue(extended.compare(0, 4, L"\\\\?\\") == 0, "native staging path is extended");
    CheckEq(ExtendedLengthPath(extended), extended, "extending twice changes nothing");

    // the installed file itself fits; only the staging suffix went over
    const std::wstring installed = std::wstring(kNativeLocalState) + kListenableFuture;
    CheckTrue(installed.size() <= 260, "native installed library path stays under MAX_PATH");

    // the relay package family name is shorter, so it never reached the limit
    const std::wstring relayStaging = std::wstring(kRelayLocalState) + kListenableFuture + L".download";
    CheckTrue(relayStaging.size() <= 260, "relay staging path stays under MAX_PATH");
}

void TestDrivePaths() {
    CheckEq(ExtendedLengthPath(L"C:\\temp\\a.txt"), L"\\\\?\\C:\\temp\\a.txt", "drive path is extended");
    CheckEq(ExtendedLengthPath(L"C:/temp/a.txt"), L"\\\\?\\C:\\temp\\a.txt", "forward slashes are normalized when extending");
    CheckEq(ExtendedLengthPath(L"C:\\a"), L"\\\\?\\C:\\a", "short drive path is extended");
    CheckEq(ExtendedLengthPath(L"\\\\?\\C:\\temp\\a.txt"), L"\\\\?\\C:\\temp\\a.txt", "extended path stays untouched");
}

void TestSharePaths() {
    CheckEq(ExtendedLengthPath(L"\\\\server\\share\\a.txt"), L"\\\\?\\UNC\\server\\share\\a.txt", "share path is extended");
    CheckEq(ExtendedLengthPath(L"\\\\server\\share"), L"\\\\?\\UNC\\server\\share", "share root is extended");
}

void TestPathsLeftAlone() {
    CheckEq(ExtendedLengthPath(std::wstring()), L"", "empty path stays empty");
    CheckEq(ExtendedLengthPath(L"relative\\a.txt"), L"relative\\a.txt", "relative path stays relative");
    CheckEq(ExtendedLengthPath(L"C:"), L"C:", "bare drive stays untouched");
    CheckEq(ExtendedLengthPath(L"C:temp"), L"C:temp", "drive relative path stays untouched");
    CheckEq(ExtendedLengthPath(L"C:\\"), L"C:\\", "drive root stays untouched");
    CheckEq(ExtendedLengthPath(L"C:\\temp\\"), L"C:\\temp\\", "trailing separator stays untouched");
    CheckEq(ExtendedLengthPath(L"C:\\temp\\\\a.txt"), L"C:\\temp\\\\a.txt", "doubled separator stays untouched");
    CheckEq(ExtendedLengthPath(L"C:\\temp\\.\\a.txt"), L"C:\\temp\\.\\a.txt", "dot segment stays untouched");
    CheckEq(ExtendedLengthPath(L"C:\\temp\\..\\a.txt"), L"C:\\temp\\..\\a.txt", "dot dot segment stays untouched");
    CheckEq(ExtendedLengthPath(L"\\\\.\\C:"), L"\\\\.\\C:", "device path stays untouched");
    CheckEq(ExtendedLengthPath(L"\\\\server"), L"\\\\server", "share path without a name stays untouched");
}

void TestRealWorldPaths() {
    // paths that come out of JoinRuntimeRelativePath and the profile helpers
    CheckEq(
        ExtendedLengthPath(L"Q:\\Users\\UserMgr0\\AppData\\Local\\Packages\\App_abc\\LocalState\\assets\\objects\\ab\\cdef"),
        L"\\\\?\\Q:\\Users\\UserMgr0\\AppData\\Local\\Packages\\App_abc\\LocalState\\assets\\objects\\ab\\cdef",
        "asset object path is extended");
    CheckEq(
        ExtendedLengthPath(L"Q:\\Users\\UserMgr0\\AppData\\Local\\Packages\\App_abc\\LocalState\\game\\versions\\1.21.1\\1.21.1.jar"),
        L"\\\\?\\Q:\\Users\\UserMgr0\\AppData\\Local\\Packages\\App_abc\\LocalState\\game\\versions\\1.21.1\\1.21.1.jar",
        "client jar path is extended");
}

}  // namespace

int main() {
    TestLongDownloadPath();
    TestDrivePaths();
    TestSharePaths();
    TestPathsLeftAlone();
    TestRealWorldPaths();

    if (g_failures != 0) {
        printf("\n%d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }

    printf("all %d long path checks passed\n", g_checks);
    return 0;
}
