#pragma once

#include <functional>
#include <string>

#include <windows.ui.core.h>

#include "auth_ui_state.h"
#include "profiles.h"

class AuthScreenRenderer;

LaunchTarget CurrentModsTarget(const AuthUiState& state);
int PurgeBlockedModsFromDir(const std::wstring& runtimeRoot, const std::wstring& modsDir);
bool IsBlockedModFileName(const std::wstring& fileName);
// "file - why" lines for jars on the known-broken list (crash reports use this).
std::vector<std::wstring> BlockedModNotesForJars(const std::vector<std::wstring>& jarNames);

// Shared install progress reporting (mods browser + modpack installers).
void SetInstallStatus(const std::wstring& s);
std::function<void(unsigned long long)> MakeInstallProgress(
    const std::wstring& label,
    unsigned long long total);

void ShowModsPage(
    ABI::Windows::UI::Core::ICoreWindow* window,
    AuthScreenRenderer* renderer,
    AuthUiState& state,
    const std::wstring& runtimeRoot);
