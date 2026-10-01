// ole32.cpp - minimal ole32.dll stand-in for the Xbox UWP sandbox.
//
// Mods that use JNA call LoadLibrary("ole32"); the console's AppContainer
// refuses explicit loads of several desktop system DLLs even though our
// packaged DLLs may link them. This stand-in lives in the app package and is
// staged only when the launcher probe finds the system ole32 unavailable, so
// a working system DLL is never shadowed.
//
// Strategy: implement the trivial pieces directly (memory, GUIDs) and forward
// COM activation to combase.dll dynamically when the console provides it;
// otherwise return clean COM failures instead of crashing.

#include <windows.h>
#include <bcrypt.h>

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "bcrypt.lib")

namespace {

HMODULE Combase() {
    static HMODULE mod = LoadLibraryW(L"combase.dll");
    return mod;
}

template <typename Fn>
Fn CombaseProc(const char* name) {
    HMODULE mod = Combase();
    return mod ? reinterpret_cast<Fn>(GetProcAddress(mod, name)) : nullptr;
}

const HRESULT kClassNotRegistered = static_cast<HRESULT>(0x80040154); // REGDB_E_CLASSNOTREG
const HRESULT kInvalidArgument = static_cast<HRESULT>(0x80070057);    // E_INVALIDARG
const HRESULT kOutOfMemory = static_cast<HRESULT>(0x8007000E);        // E_OUTOFMEMORY

} // namespace

extern "C" __declspec(dllexport) void* WINAPI CoTaskMemAlloc(SIZE_T cb) {
    return HeapAlloc(GetProcessHeap(), 0, cb ? cb : 1);
}

extern "C" __declspec(dllexport) void* WINAPI CoTaskMemRealloc(void* pv, SIZE_T cb) {
    if (!pv) return CoTaskMemAlloc(cb);
    if (cb == 0) {
        HeapFree(GetProcessHeap(), 0, pv);
        return nullptr;
    }
    return HeapReAlloc(GetProcessHeap(), 0, pv, cb);
}

extern "C" __declspec(dllexport) void WINAPI CoTaskMemFree(void* pv) {
    if (pv) HeapFree(GetProcessHeap(), 0, pv);
}

extern "C" __declspec(dllexport) HRESULT WINAPI CoCreateGuid(GUID* pguid) {
    if (!pguid) return kInvalidArgument;
    using Fn = HRESULT(WINAPI*)(GUID*);
    if (Fn real = CombaseProc<Fn>("CoCreateGuid")) {
        return real(pguid);
    }
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(pguid), sizeof(GUID),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        return kOutOfMemory;
    }
    // RFC 4122 version 4 / variant 1 bits.
    pguid->Data3 = static_cast<unsigned short>((pguid->Data3 & 0x0FFF) | 0x4000);
    pguid->Data4[0] = static_cast<unsigned char>((pguid->Data4[0] & 0x3F) | 0x80);
    return S_OK;
}

extern "C" __declspec(dllexport) int WINAPI StringFromGUID2(const GUID* pguid, wchar_t* out, int cchMax) {
    if (!pguid || !out || cchMax < 39) return 0;
    swprintf_s(out, cchMax,
        L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        pguid->Data1, pguid->Data2, pguid->Data3,
        pguid->Data4[0], pguid->Data4[1], pguid->Data4[2], pguid->Data4[3],
        pguid->Data4[4], pguid->Data4[5], pguid->Data4[6], pguid->Data4[7]);
    return 39;
}

static bool ParseGuidText(const wchar_t* s, GUID* pguid) {
    if (!s || !pguid) return false;
    unsigned int d1 = 0, d2 = 0, d3 = 0;
    unsigned int b[8] = {};
    wchar_t brace1 = 0, brace2 = 0;
    const int got = swscanf_s(s,
        L"%c%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x%c",
        &brace1, 1, &d1, &d2, &d3,
        &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6], &b[7],
        &brace2, 1);
    if (got != 13) return false;
    if (brace1 != L'{' || brace2 != L'}') return false;
    pguid->Data1 = d1;
    pguid->Data2 = static_cast<unsigned short>(d2);
    pguid->Data3 = static_cast<unsigned short>(d3);
    for (int i = 0; i < 8; ++i) pguid->Data4[i] = static_cast<unsigned char>(b[i]);
    return true;
}

extern "C" __declspec(dllexport) HRESULT WINAPI CLSIDFromString(const wchar_t* str, GUID* pguid) {
    if (!ParseGuidText(str, pguid)) return kInvalidArgument;
    return S_OK;
}

extern "C" __declspec(dllexport) HRESULT WINAPI IIDFromString(const wchar_t* str, GUID* piid) {
    return CLSIDFromString(str, piid);
}

extern "C" __declspec(dllexport) HRESULT WINAPI StringFromCLSID(const GUID* pguid, wchar_t** ppsz) {
    if (!ppsz) return kInvalidArgument;
    *ppsz = nullptr;
    wchar_t* out = static_cast<wchar_t*>(CoTaskMemAlloc(39 * sizeof(wchar_t)));
    if (!out) return kOutOfMemory;
    if (!StringFromGUID2(pguid, out, 39)) {
        CoTaskMemFree(out);
        return kInvalidArgument;
    }
    *ppsz = out;
    return S_OK;
}

extern "C" __declspec(dllexport) HRESULT WINAPI StringFromIID(const GUID* piid, wchar_t** ppsz) {
    return StringFromCLSID(piid, ppsz);
}

extern "C" __declspec(dllexport) HRESULT WINAPI CoInitializeEx(void* pvReserved, DWORD dwCoInit) {
    using Fn = HRESULT(WINAPI*)(void*, DWORD);
    if (Fn real = CombaseProc<Fn>("RoInitialize")) {
        // RoInitialize only accepts single-threaded/multi-threaded styles and
        // returns S_FALSE/S_OK/RPC_E_CHANGED_MODE like CoInitializeEx for the
        // cases mods use.
        const DWORD mapped = (dwCoInit & 0x2) ? 1 /* multi-threaded */ : 0;
        const HRESULT hr = real(mapped);
        return hr;
    }
    (void)pvReserved;
    return S_OK;
}

extern "C" __declspec(dllexport) HRESULT WINAPI CoInitialize(void* pvReserved) {
    return CoInitializeEx(pvReserved, 0x2);
}

extern "C" __declspec(dllexport) void WINAPI CoUninitialize() {
    using Fn = void(WINAPI*)();
    if (Fn real = CombaseProc<Fn>("RoUninitialize")) real();
}

extern "C" __declspec(dllexport) HRESULT WINAPI CoGetClassObject(
    const GUID* rclsid, DWORD dwClsContext, void* pvReserved, const GUID* riid, void** ppv) {
    using Fn = HRESULT(WINAPI*)(const GUID*, DWORD, void*, const GUID*, void**);
    if (Fn real = CombaseProc<Fn>("CoGetClassObject")) {
        return real(rclsid, dwClsContext, pvReserved, riid, ppv);
    }
    if (ppv) *ppv = nullptr;
    (void)riid;
    return kClassNotRegistered;
}

extern "C" __declspec(dllexport) HRESULT WINAPI CoCreateInstance(
    const GUID* rclsid, void* pUnkOuter, DWORD dwClsContext, const GUID* riid, void** ppv) {
    using Fn = HRESULT(WINAPI*)(const GUID*, void*, DWORD, const GUID*, void**);
    if (Fn real = CombaseProc<Fn>("CoCreateInstance")) {
        return real(rclsid, pUnkOuter, dwClsContext, riid, ppv);
    }
    if (ppv) *ppv = nullptr;
    return kClassNotRegistered;
}

extern "C" __declspec(dllexport) int WINAPI IsEqualGUID(const GUID* a, const GUID* b) {
    if (!a || !b) return 0;
    return memcmp(a, b, sizeof(GUID)) == 0;
}

extern "C" __declspec(dllexport) int WINAPI IsEqualCLSID(const GUID* a, const GUID* b) {
    return IsEqualGUID(a, b);
}

extern "C" __declspec(dllexport) int WINAPI IsEqualIID(const GUID* a, const GUID* b) {
    return IsEqualGUID(a, b);
}

extern "C" __declspec(dllexport) HRESULT WINAPI CoFreeUnusedLibraries() {
    return S_OK;
}

extern "C" __declspec(dllexport) HRESULT WINAPI CoFreeUnusedLibrariesEx(DWORD, DWORD) {
    return S_OK;
}

extern "C" __declspec(dllexport) DWORD WINAPI CoGetCurrentProcess() {
    return GetCurrentProcessId();
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(h);
    return TRUE;
}
