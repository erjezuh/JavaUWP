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
echo "== Adaptive JVM memory profiles (C++) =="
g++ -std=c++17 -Wall -Wextra -Werror -pedantic \
    -o /tmp/memory_profile_test tests/memory_profile_test.cpp
/tmp/memory_profile_test

echo
echo "== Launch phase timings (C++) =="
g++ -std=c++17 -Wall -Wextra -Werror -pedantic \
    -o /tmp/launch_phase_test tests/launch_phase_test.cpp
/tmp/launch_phase_test

echo
echo "== Mod metadata index / cache invalidation (C++) =="
g++ -std=c++17 -Wall -Wextra -Werror -pedantic \
    -o /tmp/mod_index_test tests/mod_index_test.cpp
/tmp/mod_index_test

echo
echo "== GLFW shim per-frame cache policy (C++) =="
g++ -std=c++17 -Wall -Wextra -Werror -pedantic \
    -o /tmp/perf_cache_test tests/perf_cache_test.cpp
/tmp/perf_cache_test

echo
echo "== Ole32/COM UWP shim contract (C++) =="
g++ -std=c++17 -Wall -Wextra -Werror -pedantic \
    -o /tmp/ole32_shim_test tests/ole32_shim_test.cpp
/tmp/ole32_shim_test

echo
echo "== Fork configuration consistency (Python) =="
python3 scripts/validate-fork-targets.py

echo
echo "All portable checks passed."
