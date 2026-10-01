#pragma once

#include <string>

enum class ModSource {
    Modrinth = 0,
    CurseForge = 1,
};

struct ModCard {
    std::wstring projectId;
    std::wstring slug;
    std::wstring title;
    std::wstring description;
    std::wstring iconPath;
    std::wstring iconUrl;
    std::wstring filePath;
    std::wstring status;
    bool installed = false;
    bool isModpack = false;
    ModSource source = ModSource::Modrinth;
};
