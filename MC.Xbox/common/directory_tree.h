#pragma once

#include <string>

namespace directorytree {

enum class Result { Ready, MissingParent, Failed };

// Try the leaf first. In an AppContainer, LocalState is writable even when an
// ancestor (the user profile or WindowsApps directory) cannot be queried.
// A root-to-leaf walk incorrectly treats that denied ancestor as a missing one.
template <typename TryCreate>
bool Ensure(std::wstring path, const TryCreate& tryCreate) {
    while (path.size() > 1 && (path.back() == L'\\' || path.back() == L'/') &&
        !(path.size() == 3 && path[1] == L':')) {
        path.pop_back();
    }
    if (path.empty()) return false;
    const auto result = tryCreate(path);
    if (result == Result::Ready) return true;
    if (result != Result::MissingParent) return false;

    const auto slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return false;
    const auto parent = path.substr(0, slash == 2 && path[1] == L':' ? 3 : slash);
    if (parent.empty() || parent == path || !Ensure(parent, tryCreate)) return false;
    return tryCreate(path) == Result::Ready;
}

} // namespace directorytree
