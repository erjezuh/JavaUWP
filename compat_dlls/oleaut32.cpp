// oleaut32.cpp - minimal oleaut32.dll stand-in for the Xbox UWP sandbox.
// Same role as ole32.cpp: staged only when the system DLL cannot be loaded.
// Implements the BSTR string family and basic VARIANT handling (the surface
// JNA's W32API/Platform helpers use); anything else fails cleanly.
//
// Implementations use Compat* names (oleaut32.def exports the real ones) to
// stay clear of the SDK's dllimport declarations in oleauto.h.

#include <windows.h>

#include <stdio.h>
#include <string.h>
#include <wchar.h>

namespace {

const HRESULT kInvalidArgument = static_cast<HRESULT>(0x80070057);   // E_INVALIDARG
const HRESULT kTypeMismatch = static_cast<HRESULT>(0x80020005);      // DISP_E_TYPEMISMATCH
const HRESULT kOutOfMemory = static_cast<HRESULT>(0x8007000E);       // E_OUTOFMEMORY

// VARIANT: 2-byte VARTYPE + 6 reserved + 8-byte union (16 bytes total, also
// on x64 where the DECIMAL overlay is exactly 16 bytes).
#pragma pack(push, 8)
struct ComVariant {
    unsigned short vt;
    unsigned short wReserved1;
    unsigned short wReserved2;
    unsigned short wReserved3;
    union {
        long long llVal;
        double dblVal;
        unsigned short boolVal;
        void* byref;
        wchar_t* bstrVal;
    } u;
};
#pragma pack(pop)

static_assert(sizeof(ComVariant) == 16, "VARIANT layout");

const unsigned short kVtBstr = 8;

// BSTR allocation: [DWORD byte-length][UTF-16 data][NUL wchar], returned
// pointer points at the data, exactly like OLE BSTRs.
struct BstrPrefix {
    unsigned long byteLen;
};

} // namespace

extern "C" wchar_t* CompatSysAllocStringLen(const wchar_t* strIn, UINT ui) {
    const size_t bytes = static_cast<size_t>(ui) * sizeof(wchar_t);
    char* raw = static_cast<char*>(HeapAlloc(GetProcessHeap(), 0,
        sizeof(BstrPrefix) + bytes + sizeof(wchar_t)));
    if (!raw) return nullptr;
    BstrPrefix* prefix = reinterpret_cast<BstrPrefix*>(raw);
    prefix->byteLen = static_cast<unsigned long>(bytes);
    wchar_t* data = reinterpret_cast<wchar_t*>(raw + sizeof(BstrPrefix));
    if (strIn && ui) memcpy(data, strIn, bytes);
    data[ui] = L'\0';
    return data;
}

extern "C" wchar_t* CompatSysAllocString(const wchar_t* strIn) {
    return CompatSysAllocStringLen(strIn, strIn ? static_cast<UINT>(wcslen(strIn)) : 0);
}

extern "C" wchar_t* CompatSysAllocStringByteLen(LPCSTR strIn, UINT ui) {
    char* raw = static_cast<char*>(HeapAlloc(GetProcessHeap(), 0,
        sizeof(BstrPrefix) + ui + sizeof(wchar_t)));
    if (!raw) return nullptr;
    BstrPrefix* prefix = reinterpret_cast<BstrPrefix*>(raw);
    prefix->byteLen = ui;
    wchar_t* data = reinterpret_cast<wchar_t*>(raw + sizeof(BstrPrefix));
    if (strIn && ui) memcpy(data, strIn, ui);
    reinterpret_cast<char*>(data)[ui] = 0;
    return data;
}

extern "C" void CompatSysFreeString(wchar_t* bstr) {
    if (!bstr) return;
    char* raw = reinterpret_cast<char*>(bstr) - sizeof(BstrPrefix);
    HeapFree(GetProcessHeap(), 0, raw);
}

extern "C" UINT CompatSysStringLen(wchar_t* bstr) {
    if (!bstr) return 0;
    const BstrPrefix* prefix = reinterpret_cast<const BstrPrefix*>(
        reinterpret_cast<const char*>(bstr) - sizeof(BstrPrefix));
    return static_cast<UINT>(prefix->byteLen / sizeof(wchar_t));
}

extern "C" UINT CompatSysStringByteLen(wchar_t* bstr) {
    if (!bstr) return 0;
    const BstrPrefix* prefix = reinterpret_cast<const BstrPrefix*>(
        reinterpret_cast<const char*>(bstr) - sizeof(BstrPrefix));
    return prefix->byteLen;
}

extern "C" void CompatVariantInit(ComVariant* pvarg) {
    if (pvarg) memset(pvarg, 0, sizeof(ComVariant));
}

extern "C" HRESULT CompatVariantClear(ComVariant* pvarg) {
    if (!pvarg) return kInvalidArgument;
    if (pvarg->vt == kVtBstr && pvarg->u.bstrVal) {
        CompatSysFreeString(pvarg->u.bstrVal);
    }
    memset(pvarg, 0, sizeof(ComVariant));
    return S_OK;
}

extern "C" HRESULT CompatVariantCopy(ComVariant* pdst, const ComVariant* psrc) {
    if (!pdst || !psrc) return kInvalidArgument;
    CompatVariantClear(pdst);
    *pdst = *psrc;
    if (psrc->vt == kVtBstr && psrc->u.bstrVal) {
        pdst->u.bstrVal = CompatSysAllocStringLen(psrc->u.bstrVal,
            CompatSysStringLen(const_cast<wchar_t*>(psrc->u.bstrVal)));
        if (!pdst->u.bstrVal) return kOutOfMemory;
    }
    return S_OK;
}

extern "C" HRESULT CompatVariantChangeType(
    ComVariant* pdst, const ComVariant* psrc, unsigned short, unsigned short vt) {
    (void)pdst; (void)psrc; (void)vt;
    // Conversions beyond identity are not provided; callers see a clean
    // DISP_E_TYPEMISMATCH instead of a crash.
    return kTypeMismatch;
}

extern "C" HRESULT CompatVariantChangeTypeEx(
    ComVariant* pdst, const ComVariant* psrc, unsigned long, unsigned short, unsigned short vt) {
    (void)pdst; (void)psrc; (void)vt;
    return kTypeMismatch;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(h);
    return TRUE;
}
