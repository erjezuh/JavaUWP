#pragma once

#include <string>

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
    // 0 = mod (game/mods), 1 = shader pack (game/shaderpacks),
    // 2 = resource pack (game/resourcepacks).
    int contentKind = 0;
};
