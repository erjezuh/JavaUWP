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
echo "== Fork configuration consistency (Python) =="
python3 scripts/validate-fork-targets.py

echo
echo "All portable checks passed."
