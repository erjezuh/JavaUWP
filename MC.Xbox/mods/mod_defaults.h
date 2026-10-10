#pragma once

#include <string>

void ConfigureKnownModDefaults(
    const std::wstring& gameDir,
    const std::wstring& userModsDir,
    const std::wstring& minecraftVersion,
    const std::wstring& bundledModsDir = L"");

// Detect the loader, not just an enabled pack: shaders can be switched on after launch.
bool ProfileHasShaderLoader(const std::wstring& modsDir);
