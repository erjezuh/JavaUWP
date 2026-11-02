// Packaged Ole32/COM shim for the Xbox UWP sandbox.
//
// Why this exists
// ---------------
// On a PC, JNA resolves Native.load("Ole32", ...) with a bare library name.
// Inside an app container JNA's LoadLibraryExW("Ole32.dll", NULL,
// LOAD_WITH_ALTERED_SEARCH_PATH) is rejected with ERROR_INVALID_PARAMETER
// (Spanish Windows: "El parametro no es correcto"), so the class initializer
// of com.sun.jna.platform.win32.Ole32 throws UnsatisfiedLinkError.
//
// OSHI 6.2.2 (shipped with Minecraft 1.20.1 Forge) reaches that initializer
// from WmiQueryHandler.initCOM() -> queryWMI() -> WindowsGraphicsCard, and
// Embeddium 0.3.x calls SystemInfo.getHardware().getGraphicsCards() from
// SodiumMixinPlugin.onLoad(), i.e. while mods are still loading. Fabric's
// xbox-compat mod cancels Minecraft's own SystemReport probe, but it cannot
// cancel a mod-owned probe. The result is the crash report shown on console:
//
//   java.lang.RuntimeException --> java.lang.UnsatisfiedLinkError
//   Unable to load library 'Ole32': El parametro no es correcto.
//
// The fix has two halves:
//   * build.ps1 packages this DLL as natives/Ole32.dll and the launcher passes
//     -Djna.library.path=<natives dir>, which makes JNA find an ABSOLUTE path.
//     An absolute path is accepted by the UWP loader (the same mechanism the
//     packaged glfw.dll/opengl32.dll shims already use).
//   * Every COM entry point below reports E_FAIL. OSHI turns any HRESULT other
//     than S_OK/S_FALSE/RPC_E_CHANGED_MODE into a COMException, which
//     WmiQueryHandler.queryWMI() catches; it returns an empty WmiResult and
//     caches the failed WMI class. Mods then see "no graphics cards" instead
//     of an uncaught UnsatisfiedLinkError, and loading continues.
//
// build.ps1 also copies this DLL as natives/OleAut32.dll. JNA's WbemcliUtil
// reaches OleAuto.INSTANCE (SysAllocString / VariantClear) once a WMI query
// gets past COM initialization; that is another bare-name system load that the
// app container rejects. Exporting those entry points here too means every
// JNA/COM library gets an absolute path and no second UnsatisfiedLinkError.
//
// This DLL is deliberately dependency-free: no Windows headers, no CRT calls,
// no COM runtime. tests/ole32_shim_test.cpp compiles this exact source on
// Linux to verify the failure contract.

#include <cstdint>

#if defined(_WIN32)
#define BANDIT_OLE32_EXPORT extern "C" __declspec(dllexport)
#else
#define BANDIT_OLE32_EXPORT extern "C"
#endif

namespace {

using HResult = std::int32_t;
using DWord = std::uint32_t;

// E_FAIL, cast through an unsigned value so this stays 0x80004005 on both
// 32-bit and 64-bit compilers.
constexpr HResult kComUnavailable = static_cast<HResult>(0x80004005u);

}  // namespace

BANDIT_OLE32_EXPORT HResult CoInitialize(void* reserved) {
    (void)reserved;
    return kComUnavailable;
}

BANDIT_OLE32_EXPORT HResult CoInitializeEx(void* reserved, DWord coInit) {
    (void)reserved;
    (void)coInit;
    return kComUnavailable;
}

BANDIT_OLE32_EXPORT HResult CoInitializeSecurity(void* secDesc, std::int32_t authSvcCount,
                                                 void* authSvc, void* reserved1,
                                                 DWord authnLevel, DWord impLevel,
                                                 void* reserved2, DWord capabilities,
                                                 void* reserved3) {
    (void)secDesc; (void)authSvcCount; (void)authSvc; (void)reserved1;
    (void)authnLevel; (void)impLevel; (void)reserved2; (void)capabilities;
    (void)reserved3;
    return kComUnavailable;
}

BANDIT_OLE32_EXPORT void CoUninitialize() {}

BANDIT_OLE32_EXPORT HResult CoCreateInstance(void* clsid, void* outer, DWord context,
                                             void* iid, void** out) {
    (void)clsid; (void)outer; (void)context; (void)iid;
    if (out) *out = nullptr;
    return kComUnavailable;
}

BANDIT_OLE32_EXPORT HResult CoCreateInstanceEx(void* clsid, void* outer, DWord context,
                                               void* serverInfo, DWord count, void* results) {
    (void)clsid; (void)outer; (void)context; (void)serverInfo; (void)count; (void)results;
    return kComUnavailable;
}

BANDIT_OLE32_EXPORT HResult CoSetProxyBlanket(void* proxy, DWord authnSvc, DWord authzSvc,
                                              void* serverPrincName, DWord authnLevel,
                                              DWord impLevel, void* authInfo,
                                              DWord capabilities) {
    (void)proxy; (void)authnSvc; (void)authzSvc; (void)serverPrincName;
    (void)authnLevel; (void)impLevel; (void)authInfo; (void)capabilities;
    return kComUnavailable;
}

BANDIT_OLE32_EXPORT void CoTaskMemFree(void* memory) { (void)memory; }

BANDIT_OLE32_EXPORT HResult CoCreateGuid(void* guid) {
    (void)guid;
    return kComUnavailable;
}

BANDIT_OLE32_EXPORT int StringFromGUID2(void* guid, void* buffer, int maxChars) {
    (void)guid; (void)buffer; (void)maxChars;
    return 0;
}

BANDIT_OLE32_EXPORT HResult IIDFromString(void* text, void* iid) {
    (void)text; (void)iid;
    return kComUnavailable;
}

BANDIT_OLE32_EXPORT HResult CLSIDFromString(void* text, void* clsid) {
    (void)text; (void)clsid;
    return kComUnavailable;
}

BANDIT_OLE32_EXPORT HResult OleInitialize(void* reserved) {
    (void)reserved;
    return kComUnavailable;
}

BANDIT_OLE32_EXPORT void OleUninitialize() {}

// ---------------------------------------------------------------------------
// OleAut32 exports (the same DLL is packaged a second time under that name).
// A null BSTR / failed VariantClear makes WbemcliUtil.selectProperties() bail
// out with a caught COMException rather than reaching another native load.
// ---------------------------------------------------------------------------

BANDIT_OLE32_EXPORT void* SysAllocString(const void* text) {
    (void)text;
    return nullptr;
}

BANDIT_OLE32_EXPORT void* SysAllocStringLen(const void* text, DWord length) {
    (void)text; (void)length;
    return nullptr;
}

BANDIT_OLE32_EXPORT void SysFreeString(void* bstr) { (void)bstr; }

BANDIT_OLE32_EXPORT DWord SysStringLen(void* bstr) {
    (void)bstr;
    return 0;
}

BANDIT_OLE32_EXPORT void VariantInit(void* variant) { (void)variant; }

BANDIT_OLE32_EXPORT HResult VariantClear(void* variant) {
    (void)variant;
    return kComUnavailable;
}

BANDIT_OLE32_EXPORT HResult VariantChangeType(void* destination, void* source,
                                              std::uint16_t flags, std::uint16_t type) {
    (void)destination; (void)source; (void)flags; (void)type;
    return kComUnavailable;
}
