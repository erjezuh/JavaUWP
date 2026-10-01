#pragma once

#include <string>

std::wstring ProfileExportsDir(const std::wstring& runtimeRoot);
std::wstring DefaultProfileExportPath(const std::wstring& runtimeRoot, const std::wstring& profileId);

bool ExportProfileMrpack(
    const std::wstring& runtimeRoot,
    const std::wstring& profileId,
    const std::wstring& outputPath,
    std::wstring& error);

bool InstallModpackFromFile(
    const std::wstring& mrpackPath,
    const std::wstring& runtimeRoot,
    const std::wstring& profileId,
    std::wstring& error);

// CurseForge modpack zip (manifest.json + overrides). Each manifest file entry
// is resolved through the CurseForge Core API, so the key must be valid.
bool InstallCurseForgeModpackFromFile(
    const std::wstring& packZipPath,
    const std::wstring& runtimeRoot,
    const std::wstring& profileId,
    const std::wstring& curseForgeApiKey,
    std::wstring& error);
