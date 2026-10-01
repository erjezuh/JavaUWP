#include "auth_screen.h"

#include <objbase.h>

#include <cstdio>
#include <string>
#include <vector>

// the real one pumps the CoreWindow and lives in launcher_ui.cpp. offscreen Render returns before calling it
void ProcessAuthUiEvents() {}

namespace {

ModCard Card(ContentKind kind, const wchar_t* title, const wchar_t* description, const wchar_t* status) {
    ModCard card;
    card.kind = kind;
    card.projectId = title;
    card.title = title;
    card.description = description;
    card.status = status;
    return card;
}

std::vector<ModCard> CardsFor(int tab) {
    switch (tab) {
    case modstab::kProfiles:
        return {
            Card(ContentKind::Mod, L"+ New profile", L"Create 1.21.11 Fabric profile", L""),
            Card(ContentKind::Mod, L"Survival", L"1.21.11 Fabric - 12 mods", L"\u25CF Playing this"),
            Card(ContentKind::Mod, L"Vanilla", L"1.21.11 Fabric - Pure vanilla, no mods", L""),
        };
    case modstab::kModpacks:
        return {
            Card(ContentKind::Modpack, L"Fabulously Optimized", L"Improved performance and graphics with a vanilla feel", L"9120334 downloads"),
            Card(ContentKind::Modpack, L"Simply Optimized", L"The simple, light optimization pack", L"2110440 downloads"),
        };
    case modstab::kResourcePacks:
        return {
            Card(ContentKind::ResourcePack, L"Fresh Animations", L"Entity animations that bring the world to life", L"6213904 downloads"),
            Card(ContentKind::ResourcePack, L"Stay True", L"A vanilla friendly texture overhaul with a little extra", L"1843002 downloads"),
            Card(ContentKind::ResourcePack, L"A Long Pack Name That Has To Wrap Or Clip In The Card", L"Checks how a long title and a long description behave when both run past the edge of the card", L"12 downloads"),
        };
    case modstab::kShaders:
        return {
            Card(ContentKind::Shader, L"Complementary Reimagined", L"Vanilla inspired shaders with plenty of options", L"8801233 downloads"),
            Card(ContentKind::Shader, L"BSL Shaders", L"Bright and smooth lighting", L"7421988 downloads"),
        };
    default:
        return {
            Card(ContentKind::Mod, L"Sodium", L"The fastest rendering optimization mod", L"60113002 downloads"),
            Card(ContentKind::Mod, L"Iris Shaders", L"Shader support that works with Sodium", L"41022331 downloads"),
            Card(ContentKind::Mod, L"Mod Menu", L"Adds a mod menu to view the list of mods", L"38810220 downloads"),
            Card(ContentKind::Mod, L"Fabric API", L"Core API library for the Fabric toolchain", L"91200500 downloads"),
        };
    }
}

AuthUiState ModsPage(int tab) {
    AuthUiState state;
    state.showDeviceCode = false;
    state.showModsPage = true;
    state.selectedModsTab = tab;
    state.modsFocus = 0;
    state.activeProfileName = L"Survival";

    LaunchTarget target;
    target.targetId = L"1.21.11-fabric-0.19.3";
    target.displayName = L"1.21.11 Fabric";
    target.minecraftVersion = L"1.21.11";
    target.loader = L"fabric";
    target.loaderVersion = L"0.19.3";
    state.modsTargets = { target };
    state.modsBrowseTargetId = target.targetId;

    state.modsCards = CardsFor(tab);
    state.status = std::to_wstring(state.modsCards.size()) + L" of 1812";
    return state;
}

std::wstring Slug(const wchar_t* label) {
    std::wstring out;
    for (const wchar_t* c = label; *c; ++c) out += *c == L' ' ? L'-' : static_cast<wchar_t>(towlower(*c));
    return out;
}

// every com object the renderer holds has to be released before CoUninitialize, so it lives in here
int RenderAll(const std::wstring& outDir) {
    // the console reports a 1920x1080 view at scale 1, and the layout and font sizes are fixed to it
    AuthScreenRenderer renderer;
    if (!renderer.InitializeOffscreen(1920.0f, 1080.0f, 1.0f)) {
        fwprintf(stderr, L"could not create the offscreen renderer\n");
        return 1;
    }

    int failures = 0;
    auto renderTo = [&](const AuthUiState& state, const std::wstring& name) {
        renderer.Render(state);
        const std::wstring path = outDir + L"\\" + name + L".png";
        if (renderer.SaveFramePng(path)) {
            wprintf(L"%s\n", path.c_str());
        } else {
            fwprintf(stderr, L"could not write %s\n", path.c_str());
            ++failures;
        }
    };

    for (int tab = 0; tab < modstab::kCount; ++tab) {
        renderTo(ModsPage(tab), L"mods-" + std::to_wstring(tab) + L"-" + Slug(ModsTabAt(tab).label));
    }
    for (const int tab : { modstab::kModpacks, modstab::kResourcePacks, modstab::kShaders }) {
        AuthUiState state = ModsPage(tab);
        state.modsDetailOpen = true;
        state.modsDetailCard = state.modsCards.front();
        state.modsDetailMeta = L"Decoration, Utility  -  " + state.modsDetailCard.status;
        state.modsDetailBody = L"About\nA sample description so the body text has something to wrap.";
        renderTo(state, L"detail-" + Slug(ModsTabAt(tab).label));
    }
    return failures;
}

}

int wmain(int argc, wchar_t** argv) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
    const std::wstring outDir = argc > 1 ? argv[1] : L"ui-preview-out";
    CreateDirectoryW(outDir.c_str(), nullptr);
    const int failures = RenderAll(outDir);
    CoUninitialize();
    return failures == 0 ? 0 : 1;
}
