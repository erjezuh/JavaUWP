// Portable regression test for shims/ole32_uwp.cpp.
//
// The real DLL is built with MSVC for UWP, but the source has no Windows
// dependencies, so it can be compiled and exercised on Linux/macOS. This
// verifies the contract OSHI relies on: every COM entry point returns E_FAIL
// (not S_OK, S_FALSE or RPC_E_CHANGED_MODE), which WmiQueryHandler.queryWMI()
// turns into a caught COMException and an empty WmiResult.

#include <cstddef>
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
    // Cleanup entry points must behave like the OS: reset the structure and
    // return S_OK. OpenAL Soft reuses a PROPVARIANT right after clear(), and a
    // stub that only returned E_FAIL left a dangling VT_LPWSTR pointer there.
    unsigned char propVariant[24];
    std::memset(propVariant, 0xAA, sizeof(propVariant));
    PropVariantInit(propVariant);
    Expect("PropVariantInit clears vt and payload", propVariant[0] == 0 && propVariant[8] == 0);
    std::memset(propVariant, 0xAA, sizeof(propVariant));
    Expect("PropVariantClear returns S_OK", PropVariantClear(propVariant) == kSok);
    bool propVariantCleared = true;
    for (std::size_t i = 0; i < 16; ++i) {
        if (propVariant[i] != 0) propVariantCleared = false;
    }
    Expect("PropVariantClear resets the whole PROPVARIANT", propVariantCleared);
    Expect("PropVariantClear tolerates a null pointer", PropVariantClear(nullptr) == kSok);

    unsigned char variant[24];
    std::memset(variant, 0xAA, sizeof(variant));
    Expect("VariantClear returns S_OK", VariantClear(variant) == kSok);
    bool variantCleared = true;
    for (std::size_t i = 0; i < 16; ++i) {
        if (variant[i] != 0) variantCleared = false;
    }
    Expect("VariantClear resets the whole VARIANT", variantCleared);
    Expect("VariantClear tolerates a null pointer", VariantClear(nullptr) == kSok);
    std::memset(variant, 0xAA, sizeof(variant));
    VariantInit(variant);
    Expect("VariantInit clears vt and payload", variant[0] == 0 && variant[8] == 0);
    std::memset(variant, 0xAA, sizeof(variant));
    Expect("VariantChangeType returns E_FAIL",
           IsFailedComHresult(VariantChangeType(variant, nullptr, 0, 0)));
    Expect("VariantChangeType leaves an empty destination", variant[0] == 0 && variant[8] == 0);
    std::memset(variant, 0xAA, sizeof(variant));
    Expect("VariantCopy returns E_FAIL", IsFailedComHresult(VariantCopy(variant, nullptr)));
    Expect("VariantCopy leaves an empty destination", variant[0] == 0 && variant[8] == 0);

    // Native libraries resolve ole32.lib / oleaut32.lib imports by bare DLL
    // name. If this shim is the first Ole32.dll in the process, every symbol
    // OpenAL Soft imports must exist or LoadLibrary fails with error 127.
    // Calling them here proves the export table is complete at link time.
    Expect("PropVariantClear is a safe no-op on null", PropVariantClear(nullptr) == kSok);
    Expect("CoTaskMemAlloc fails safely", CoTaskMemAlloc(32) == nullptr);
    Expect("CoTaskMemRealloc fails safely", CoTaskMemRealloc(nullptr, 32) == nullptr);
    void* allocator = reinterpret_cast<void*>(0x1);
    Expect("CoGetMalloc returns E_FAIL",
           IsFailedComHresult(CoGetMalloc(0, &allocator)) && allocator == nullptr);
    Expect("StringFromIID returns E_FAIL", IsFailedComHresult(StringFromIID(nullptr, nullptr)));
    Expect("StringFromCLSID returns E_FAIL", IsFailedComHresult(StringFromCLSID(nullptr, nullptr)));
    Expect("CLSIDFromProgID returns E_FAIL", IsFailedComHresult(CLSIDFromProgID(nullptr, nullptr)));
    Expect("ProgIDFromCLSID returns E_FAIL", IsFailedComHresult(ProgIDFromCLSID(nullptr, nullptr)));
    Expect("CoGetClassObject returns E_FAIL",
           IsFailedComHresult(CoGetClassObject(nullptr, 0, nullptr, nullptr, nullptr)));
    Expect("CoRegisterClassObject returns E_FAIL",
           IsFailedComHresult(CoRegisterClassObject(nullptr, nullptr, 0, 0, nullptr)));
    Expect("CoRevokeClassObject returns E_FAIL", IsFailedComHresult(CoRevokeClassObject(0)));
    Expect("CoMarshalInterface returns E_FAIL",
           IsFailedComHresult(CoMarshalInterface(nullptr, nullptr, nullptr, 0, nullptr, 0)));
    Expect("CoUnmarshalInterface returns E_FAIL",
           IsFailedComHresult(CoUnmarshalInterface(nullptr, nullptr, nullptr)));
    Expect("CoReleaseMarshalData returns E_FAIL", IsFailedComHresult(CoReleaseMarshalData(nullptr)));
    Expect("CoDisconnectObject returns E_FAIL", IsFailedComHresult(CoDisconnectObject(nullptr, 0)));
    Expect("CreateStreamOnHGlobal returns E_FAIL",
           IsFailedComHresult(CreateStreamOnHGlobal(nullptr, 0, nullptr)));
    Expect("GetHGlobalFromStream returns E_FAIL",
           IsFailedComHresult(GetHGlobalFromStream(nullptr, nullptr)));
    Expect("OleRun returns E_FAIL", IsFailedComHresult(OleRun(nullptr)));
    Expect("SysAllocStringByteLen returns a null BSTR", SysAllocStringByteLen(nullptr, 4) == nullptr);
    Expect("SysStringByteLen reports an empty BSTR", SysStringByteLen(nullptr) == 0);
    Expect("VariantCopy returns E_FAIL", IsFailedComHresult(VariantCopy(nullptr, nullptr)));
    Expect("VariantCopyInd returns E_FAIL", IsFailedComHresult(VariantCopyInd(nullptr, nullptr)));
    Expect("SafeArrayCreate fails safely", SafeArrayCreate(0, 0, nullptr) == nullptr);
    Expect("SafeArrayDestroy returns E_FAIL", IsFailedComHresult(SafeArrayDestroy(nullptr)));
    Expect("LoadTypeLib returns E_FAIL", IsFailedComHresult(LoadTypeLib(nullptr, nullptr)));
    Expect("RegisterTypeLib returns E_FAIL",
           IsFailedComHresult(RegisterTypeLib(nullptr, nullptr, nullptr)));

    // These must not throw or trap: queryWMI calls them on the cleanup path,
    // and native consumers call them on their own failure paths.
    CoUninitialize();
    CoTaskMemFree(nullptr);
    PropVariantInit(nullptr);
    CoFreeUnusedLibraries();
    (void)CoGetCurrentProcess();
    OleUninitialize();
    SysFreeString(nullptr);
    SysReAllocString(nullptr, nullptr);
    VariantInit(nullptr);

    if (failures) {
        std::fprintf(stderr, "%d ole32 shim check(s) failed\n", failures);
        return 1;
    }
    std::puts("ole32 UWP COM shim checks passed");
    return 0;
}
