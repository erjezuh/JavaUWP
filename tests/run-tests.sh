#!/usr/bin/env bash
# Portable checks for the native + NeoForge fork. The APPX build itself needs
# Windows, MSVC and the Windows SDK; these tests run on any Linux/macOS host.
set -euo pipefail
cd "$(dirname "$0")/.."

echo "== NeoForge version helpers (C++) =="
g++ -std=c++17 -Wall -Wextra -Werror -pedantic \
    -o /tmp/neoforge_version_test tests/neoforge_version_test.cpp
/tmp/neoforge_version_test

echo
echo "== Win32 long path helper (C++) =="
g++ -std=c++17 -Wall -Wextra -Werror -pedantic \
    -o /tmp/win32_long_path_test tests/win32_long_path_test.cpp
/tmp/win32_long_path_test

echo
echo "== Sodium-family renderer classification (C++) =="
g++ -std=c++17 -Wall -Wextra -Werror -pedantic \
    -o /tmp/sodium_family_test tests/sodium_family_test.cpp
/tmp/sodium_family_test

echo
echo "== Fork configuration consistency (Python) =="
python3 scripts/validate-fork-targets.py

echo
echo "All portable checks passed."
