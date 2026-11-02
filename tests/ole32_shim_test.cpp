// Portable regression test for shims/ole32_uwp.cpp.
//
// The real DLL is built with MSVC for UWP, but the source has no Windows
// dependencies, so it can be compiled and exercised on Linux/macOS. This
// verifies the contract OSHI relies on: every COM entry point returns E_FAIL
// (not S_OK, S_FALSE or RPC_E_CHANGED_MODE), which WmiQueryHandler.queryWMI()
// turns into a caught COMException and an empty WmiResult.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../shims/ole32_uwp.cpp"

namespace {

constexpr std::int32_t kEFail = static_cast<std::int32_t>(0x80004005u);
constexpr std::int32_t kSFalse = 1;
constexpr std::int32_t kSok = 0;
constexpr std::int32_t kRpcEChangedMode = static_cast<std::int32_t>(0x80010106u);

int failures = 0;

void Expect(const char* name, bool condition) {
    if (!condition) {
        std::fprintf(stderr, "FAIL %s\n", name);
        ++failures;
    }
}

bool IsFailedComHresult(std::int32_t value) {
    return value != kSok && value != kSFalse && value != kRpcEChangedMode && value < 0;
}

}  // namespace

int main() {
    Expect("CoInitializeEx returns E_FAIL", CoInitializeEx(nullptr, 0) == kEFail);
    Expect("CoInitializeEx is a failed HRESULT", IsFailedComHresult(CoInitializeEx(nullptr, 2)));
    Expect("CoInitialize returns E_FAIL", CoInitialize(nullptr) == kEFail);
    Expect("CoInitializeSecurity returns E_FAIL",
           IsFailedComHresult(CoInitializeSecurity(nullptr, -1, nullptr, nullptr, 0, 2, nullptr, 0, nullptr)));
    Expect("CoCreateInstance returns E_FAIL",
           IsFailedComHresult(CoCreateInstance(nullptr, nullptr, 0, nullptr, nullptr)));
    void* out = reinterpret_cast<void*>(0x1);
    CoCreateInstance(nullptr, nullptr, 0, nullptr, &out);
    Expect("CoCreateInstance clears the out pointer", out == nullptr);
    Expect("CoCreateInstanceEx returns E_FAIL",
           IsFailedComHresult(CoCreateInstanceEx(nullptr, nullptr, 0, nullptr, 0, nullptr)));
    Expect("CoSetProxyBlanket returns E_FAIL",
           IsFailedComHresult(CoSetProxyBlanket(nullptr, 10, 0, nullptr, 2, 3, nullptr, 0)));
    Expect("CoCreateGuid returns E_FAIL", IsFailedComHresult(CoCreateGuid(nullptr)));
    Expect("IIDFromString returns E_FAIL", IsFailedComHresult(IIDFromString(nullptr, nullptr)));
    Expect("CLSIDFromString returns E_FAIL", IsFailedComHresult(CLSIDFromString(nullptr, nullptr)));
    Expect("OleInitialize returns E_FAIL", IsFailedComHresult(OleInitialize(nullptr)));
    Expect("StringFromGUID2 reports failure", StringFromGUID2(nullptr, nullptr, 0) == 0);
    Expect("SysAllocString returns a null BSTR", SysAllocString(nullptr) == nullptr);
    Expect("SysAllocStringLen returns a null BSTR", SysAllocStringLen(nullptr, 4) == nullptr);
    Expect("SysStringLen reports an empty BSTR", SysStringLen(nullptr) == 0);
    Expect("VariantClear returns E_FAIL", IsFailedComHresult(VariantClear(nullptr)));
    Expect("VariantChangeType returns E_FAIL",
           IsFailedComHresult(VariantChangeType(nullptr, nullptr, 0, 0)));

    // These must not throw or trap: queryWMI calls them on the cleanup path.
    CoUninitialize();
    CoTaskMemFree(nullptr);
    OleUninitialize();
    SysFreeString(nullptr);
    VariantInit(nullptr);

    if (failures) {
        std::fprintf(stderr, "%d ole32 shim check(s) failed\n", failures);
        return 1;
    }
    std::puts("ole32 UWP COM shim checks passed");
    return 0;
}
