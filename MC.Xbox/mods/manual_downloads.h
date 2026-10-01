#pragma once

#include <string>
#include <vector>

// mods a pack needed but curseforge would not serve, kept until the jar turns up in the profile
struct ManualDownload {
    std::wstring fileName;
    std::wstring modName;
    std::wstring url;
};

bool RecordManualDownloads(const std::wstring& profileId, const std::vector<ManualDownload>& items);

// entries whose jar is still missing from the profile mods folder
std::vector<ManualDownload> PendingManualDownloads(const std::wstring& runtimeRoot, const std::wstring& profileId);

bool ClearManualDownloads(const std::wstring& profileId);
