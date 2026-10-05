#include "long_path.h"

#include <cwctype>

namespace {

// the extended-length marker disables the normalization the kernel normally
// applies, so a path that still needs it has to stay unprefixed
bool NeedsNormalization(const std::wstring& path, size_t start) {
    if (path.back() == L'\\' || path.back() == L'/') return true;

    size_t segmentStart = start;
    while (segmentStart <= path.size()) {
        const size_t separator = path.find_first_of(L"\\/", segmentStart);
        const size_t segmentEnd = separator == std::wstring::npos ? path.size() : separator;
        const std::wstring segment = path.substr(segmentStart, segmentEnd - segmentStart);
        if (segment.empty() || segment == L"." || segment == L"..") return true;
        if (separator == std::wstring::npos) return false;
        segmentStart = separator + 1;
    }

    return false;
}

}  // namespace

std::wstring ExtendedLengthPath(const std::wstring& path) {
    if (path.empty() || path.compare(0, 4, L"\\\\?\\") == 0) return path;

    size_t start = 0;
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') {
        // UNC share: \\server\share\...
        start = 2;
        if (path.find_first_of(L"\\/", start) == std::wstring::npos) return path;
    } else if (path.size() >= 3 && iswalpha(path[0]) && path[1] == L':' &&
        (path[2] == L'\\' || path[2] == L'/')) {
        start = 3;
    } else {
        // relative path, drive relative path, or device path
        return path;
    }

    if (NeedsNormalization(path, start)) return path;

    std::wstring absolute = path;
    for (wchar_t& ch : absolute) {
        if (ch == L'/') ch = L'\\';
    }

    if (path[0] == L'\\') return L"\\\\?\\UNC\\" + absolute.substr(2);
    return L"\\\\?\\" + absolute;
}
