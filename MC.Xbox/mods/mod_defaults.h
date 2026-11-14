#pragma once

#include <functional>
#include <string>

// onModIndexProgress(done, total) is called only when real, countable work
// exists (the JARs in mods/). It reports JARs already accounted for, never a
// fabricated percentage, and may be null for non-interactive callers.
void ConfigureKnownModDefaults(
    const std::wstring& gameDir,
    const std::wstring& userModsDir,
    const std::wstring& minecraftVersion,
    const std::wstring& bundledModsDir = L"",
    const std::function<void(size_t done, size_t total)>& onModIndexProgress = {});
