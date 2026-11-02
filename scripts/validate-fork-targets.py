#!/usr/bin/env python3
"""Static consistency checks for the native + NeoForge edition of this fork.

The full APPX build only runs on Windows with MSVC, JDKs and the Windows SDK.
This validator runs anywhere (plain Python 3) and catches the configuration
drift that used to break NeoForge targets: a default target that is not in the
catalog, unsubstituted runtime_config.h tokens, stale helper-script versions,
and a bundled Controlify mod that does not match the default target.

Usage: python3 scripts/validate-fork-targets.py
"""

from __future__ import annotations

import csv
import io
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

errors: list[str] = []
warnings: list[str] = []


def read(rel: str) -> str:
    path = ROOT / rel
    if not path.exists():
        errors.append(f"missing file: {rel}")
        return ""
    return path.read_text(encoding="utf-8-sig")


def config_defaults(text: str) -> dict[str, str]:
    """Read the literal default of each ProjectConfig key."""
    out: dict[str, str] = {}
    pattern = re.compile(
        r'^\s*([A-Za-z][A-Za-z0-9]*)\s*=\s*if \(\$env:[A-Z_]+\)\s*\{[^}]*\}\s*else\s*\{\s*"([^"]*)"\s*\}',
        re.MULTILINE,
    )
    for key, value in pattern.findall(text):
        out[key] = value
    # keys whose default is a computed variable (e.g. $defaultMcVersion):
    # take the final else branch of that variable's conditional expression.
    for key, var in re.findall(r"^\s*([A-Za-z][A-Za-z0-9]*)\s*=\s*\$(\w+)\s*$", text, re.MULTILINE):
        m = re.search(r'^\$' + re.escape(var) + r'\s*=.*else\s*\{\s*"([^"]+)"\s*\}', text, re.MULTILINE)
        if m:
            out.setdefault(key, m.group(1))
    return out


def catalog_rows(text: str) -> list[dict[str, str]]:
    reader = csv.DictReader(io.StringIO(text), delimiter="\t")
    return [row for row in reader if row.get("minecraftVersion")]


def main() -> int:
    catalog_text = read("config/versions.tsv")
    config_text = read("scripts/config.ps1")
    template_text = read("MC.Xbox/runtime_config.h.in")
    build_text = read("build.ps1")
    controller_text = read("controller_mod/neoforge/build_neoforge_controller_mod.ps1")
    gen_text = read("scripts/gen-neoforge-artifacts.ps1")
    common_text = read("scripts/common.ps1")
    neoforge_cpp = read("MC.Xbox/launch/loaders/neoforge.cpp")
    readme = read("README.md")
    bundle_path = ROOT / "config" / "controlify-bundle.json"
    if not bundle_path.exists():
        errors.append("missing file: config/controlify-bundle.json")
        bundle = {"targetId": "", "mods": []}
    else:
        try:
            bundle = json.loads(bundle_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            errors.append(f"config/controlify-bundle.json is not valid JSON: {exc}")
            bundle = {"targetId": "", "mods": []}

    rows = catalog_rows(catalog_text)
    if not rows:
        errors.append("config/versions.tsv has no target rows")

    # 1. Unique target ids and complete columns.
    seen: set[str] = set()
    for row in rows:
        tid = f'{row["minecraftVersion"]}-{row["loader"]}-{row["loaderVersion"]}'
        if tid in seen:
            errors.append(f"duplicate catalog target id: {tid}")
        seen.add(tid)
        for column in ("minecraftVersion", "displayName", "loader", "loaderVersion", "javaRuntime", "supportLevel", "controllerProvider"):
            if not row.get(column):
                errors.append(f"catalog row {tid} is missing {column}")
        if row.get("javaRuntime") not in {"current", "java21", "java17", "legacy"}:
            errors.append(f"catalog row {tid} has unknown javaRuntime {row.get('javaRuntime')!r}")

    # 2. NeoForge version numbers match their Minecraft version family.
    for row in rows:
        if row.get("loader") != "neoforge":
            continue
        mc, lv = row["minecraftVersion"], row["loaderVersion"]
        family = ".".join(mc.split(".")[1:]) if mc.startswith("1.") else mc
        expected = family.rstrip(".") + "."
        if not lv.startswith(expected) and not lv.startswith(family + "-"):
            warnings.append(f"NeoForge {lv} does not look like it belongs to Minecraft {mc} (expected prefix {expected})")

    # 3. Compiled default target is a real catalog row, with matching metadata.
    cfg = config_defaults(config_text)
    required = [
        "DefaultMinecraftVersion", "DefaultLoader", "DefaultLoaderVersion",
        "DefaultLaunchVersion", "DefaultControllerProvider", "DefaultJavaRuntime",
        "MinecraftVersion", "MinecraftAssetIndex",
    ]
    for key in required:
        if key not in cfg:
            errors.append(f"cannot read default for {key} from scripts/config.ps1")

    default_id = f'{cfg.get("DefaultMinecraftVersion")}-{cfg.get("DefaultLoader")}-{cfg.get("DefaultLoaderVersion")}'
    match = [row for row in rows if f'{row["minecraftVersion"]}-{row["loader"]}-{row["loaderVersion"]}' == default_id]
    if len(match) != 1:
        errors.append(f"default target {default_id} is not exactly one catalog row (found {len(match)})")
    else:
        row = match[0]
        if cfg.get("DefaultJavaRuntime") != row["javaRuntime"]:
            errors.append(f"default javaRuntime {cfg.get('DefaultJavaRuntime')!r} != catalog {row['javaRuntime']!r}")
        if cfg.get("DefaultControllerProvider") != row["controllerProvider"]:
            errors.append(f"default controllerProvider {cfg.get('DefaultControllerProvider')!r} != catalog {row['controllerProvider']!r}")
    if cfg.get("DefaultMinecraftVersion") != cfg.get("MinecraftVersion"):
        errors.append("DefaultMinecraftVersion and MinecraftVersion differ; build.ps1 rejects this combination")
    if cfg.get("DefaultLoader") == "neoforge":
        launch = cfg.get("DefaultLaunchVersion", "")
        if not launch.startswith("neoforge-" + cfg.get("DefaultLoaderVersion", "?")):
            errors.append(f"DefaultLaunchVersion {launch!r} does not match the default NeoForge version")

    # 4. Every runtime_config.h token has a replacement in build.ps1 and vice versa.
    tokens = set(re.findall(r"@@([A-Z_]+)@@", template_text))
    generator_text = read("scripts/gen-runtime-config.ps1")
    replacements = set(re.findall(r"'@@([A-Z_]+)@@'", generator_text))
    if not tokens:
        errors.append("runtime_config.h.in has no tokens")
    if "gen-runtime-config.ps1" not in build_text:
        errors.append("build.ps1 no longer calls the shared runtime_config.h generator")
    for token in sorted(tokens - replacements):
        errors.append(f"runtime_config.h.in token @@{token}@@ is never substituted by scripts/gen-runtime-config.ps1")
    for token in sorted(replacements - tokens):
        warnings.append(f"gen-runtime-config.ps1 substitutes @@{token}@@ but the template does not use it")

    # Every kDefault* constant referenced by C++ must exist in the template.
    cpp_files = [
        "MC.Xbox/App.cpp",
        "MC.Xbox/profiles/profiles.cpp",
        "MC.Xbox/launch/runtime_manager.cpp",
        "MC.Xbox/launch/minecraft_launch.cpp",
    ]
    used = set()
    for rel in cpp_files:
        used |= set(re.findall(r"\bk(?:Default|Minecraft|Fabric)[A-Za-z0-9_]*W?\b", read(rel)))
    defined = set(re.findall(r"\b(k[A-Za-z0-9_]+)\[\]", template_text))
    for identifier in sorted(used - defined):
        errors.append(f"C++ references {identifier} but runtime_config.h.in never defines it")

    # 5. The default manifest and default target id come from the config.
    if "$ProjectConfig.DefaultLoader" not in build_text or "$ProjectConfig.DefaultLoaderVersion" not in build_text:
        errors.append("build.ps1 must use the configured default loader/version")
    if '"fabric" `\n    -LoaderVersion $ProjectConfig.FabricLoaderVersion' in build_text.replace("\r", ""):
        errors.append("download_manifest.tsv is still hardcoded to Fabric")
    if "$defaultTargetId" not in build_text:
        errors.append("build.ps1 has no defaultTargetId")
    if "kFabricLaunchVersion" in read("MC.Xbox/App.cpp") or "kFabricLaunchVersion" in read("MC.Xbox/launch/runtime_manager.cpp"):
        errors.append("launcher still falls back to the hardcoded Fabric launch version")

    # 6. No catalog target may advertise a bundled Bandit controller any more.
    for row in rows:
        if row.get("controllerProvider") != "none":
            tid = f'{row["minecraftVersion"]}-{row["loader"]}-{row["loaderVersion"]}'
            errors.append(f"catalog row {tid} still advertises controllerProvider={row['controllerProvider']!r}")
    for script in ("build_fabric_controller_mod.ps1", "build_forge_controller_mod.ps1",
                   "build_neoforge_controller_mod.ps1", "build_cleanroom_controller_mod.ps1"):
        if script in build_text:
            errors.append(f"build.ps1 still compiles the Bandit controller mod ({script})")

    # 7. Helper scripts resolve versions from the catalog / installer, not stale literals.
    for rel, text in (("scripts/gen-neoforge-artifacts.ps1", gen_text),
                      ("controller_mod/neoforge/build_neoforge_controller_mod.ps1", controller_text)):
        if "21.1.233" in text:
            errors.append(f"{rel} still hardcodes the stale NeoForge version 21.1.233")
        if "Resolve-CatalogLoaderVersion" not in text:
            errors.append(f"{rel} does not resolve its NeoForge version from the catalog")
    if "Resolve-CatalogLoaderVersion" not in common_text or "Resolve-InstallerNeoFormVersion" not in common_text:
        errors.append("scripts/common.ps1 is missing the shared version resolvers")

    # 8. Launch code uses the catalog-aware resolver.
    if "ResolveNeoForgeLoaderVersion" not in neoforge_cpp:
        errors.append("neoforge.cpp does not use ResolveNeoForgeLoaderVersion")
    if re.search(r"std::wstring NeoForgeVersionFromLaunchVersion\(", neoforge_cpp):
        errors.append("neoforge.cpp still defines the old inline parser")
    if "ResolveNeoForgeLoaderVersion" not in read("MC.Xbox/launch/minecraft_launch.cpp"):
        errors.append("minecraft_launch.cpp still parses the NeoForge version on its own")

    # 9. Manifest generator robustness.
    manifest_gen = read("scripts/new-download-manifest.ps1")
    for needle in ("Get-RemoteSha1", "Get-LibraryVersionFromPrefixes", "Get-NeoFormVersionFromProfile $loaderProfile $installProfile"):
        if needle not in manifest_gen:
            errors.append(f"new-download-manifest.ps1 is missing {needle}")

    # 10. Fork documentation is present and linked.
    if "docs/FORK-NATIVE-NEOFORGE.es.md" not in readme:
        errors.append("README.md does not link the Spanish fork guide")
    if not (ROOT / "docs" / "FORK-NATIVE-NEOFORGE.es.md").exists():
        errors.append("docs/FORK-NATIVE-NEOFORGE.es.md is missing")

    # 11. The pinned Controlify bundle is complete, loader-matched and verified.
    mods = bundle.get("mods") or []
    if bundle.get("targetId") != default_id:
        errors.append(f"Controlify bundle targetId {bundle.get('targetId')!r} != default {default_id!r}")
    if bundle.get("loader") != cfg.get("DefaultLoader"):
        errors.append("Controlify bundle loader does not match the default loader")
    if bundle.get("minecraftVersion") != cfg.get("DefaultMinecraftVersion"):
        errors.append("Controlify bundle Minecraft version does not match the default")
    if not mods:
        errors.append("Controlify bundle has no mods")
    names = []
    for mod in mods:
        name = mod.get("name", "")
        names.append(name)
        if not name.endswith(".jar"):
            errors.append(f"Controlify bundle entry {name!r} is not a jar")
        if not re.fullmatch(r"[0-9a-f]{40}", str(mod.get("sha1", ""))):
            errors.append(f"Controlify bundle entry {name!r} has no SHA1")
        if not isinstance(mod.get("size"), int) or mod["size"] <= 0:
            errors.append(f"Controlify bundle entry {name!r} has no positive size")
        if not str(mod.get("url", "")).startswith("https://cdn.modrinth.com/"):
            errors.append(f"Controlify bundle entry {name!r} has no Modrinth CDN url")
    if len(set(names)) != len(names):
        errors.append("Controlify bundle has duplicate mod names")
    if "config\controlify-bundle.json" not in build_text:
        errors.append("build.ps1 does not read config/controlify-bundle.json")
    if "Controlify" not in read("MC.Xbox/ui/auth_screen.h"):
        errors.append("the launcher UI still advertises the Bandit controller toggle")

    # 12. The GLFW shim must report menu cursor positions in the current
    # framebuffer space and draw the crosshair through the same mapping.
    # main-native merged over main's fix and kept the stale framebuffer size,
    # which made Minecraft think the mouse lived at 854x480 on a 1920x1080
    # surface: tooltips/clicks landed near the top left, the crosshair elsewhere.
    shim = read("glfw_shim/glfw_uwp.cpp")
    def cpp_function(text: str, signature: str) -> str:
        start = text.find(signature)
        if start < 0:
            return ""
        brace = text.find("{", start)
        if brace < 0:
            return ""
        depth = 0
        for i in range(brace, len(text)):
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    return text[brace:i + 1]
        return ""

    metrics = cpp_function(shim, "static void RefreshWindowMetrics(")
    if not metrics:
        errors.append("glfw_uwp.cpp: RefreshWindowMetrics not found")
    else:
        if "g_menu_window_width = newFramebufferWidth;" not in metrics or \
           "g_menu_window_height = newFramebufferHeight;" not in metrics:
            errors.append("glfw_uwp.cpp: menu cursor space is not the current framebuffer size")
        if "g_menu_window_width = g_framebuffer_width;" in metrics:
            errors.append("glfw_uwp.cpp: menu cursor space uses the stale framebuffer size")
    if "const float cx = (float)WindowToMenuInputX(g_menu_abs_x);" not in shim or \
       "const float cy = (float)WindowToMenuInputY(g_menu_abs_y);" not in shim:
        errors.append("glfw_uwp.cpp: the crosshair overlay does not use the cursor coordinate mapping")

    # 13. Rendering/optimisation mods on 1.20.1 Forge. The Recommended tab
    # used to list Rubidium AND Embeddium together even though Modrinth marks
    # them mutually incompatible, and ConfigureSodiumDefaults only knew about
    # Fabric's sodium-options.json, so the Forge ports booted with the Mesa/UWP
    # unsafe defaults (ASYNC staging, render-ahead 3) and no conflict guard.
    recommended_path = ROOT / "config" / "recommended-mods.json"
    try:
        recommended = json.loads(recommended_path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as exc:
        errors.append(f"config/recommended-mods.json is not valid JSON: {exc}")
        recommended = {}
    renderers = {"sodium", "embeddium", "rubidium"}
    for loader_name, versions in recommended.items():
        for mc_version, slugs in versions.items():
            present = renderers.intersection(slugs)
            if len(present) > 1:
                errors.append(
                    f"recommended-mods.json {loader_name}.{mc_version} lists several "
                    f"mutually exclusive renderers: {', '.join(sorted(present))}"
                )
    forge_1201 = (recommended.get("forge") or {}).get("1.20.1") or []
    if "embeddium" not in forge_1201:
        errors.append("recommended-mods.json forge.1.20.1 does not recommend Embeddium")
    if "rubidium" in forge_1201:
        errors.append("recommended-mods.json forge.1.20.1 still recommends abandoned Rubidium")
    mod_defaults = read("MC.Xbox/mods/mod_defaults.cpp")
    sodium_family = read("MC.Xbox/mods/sodium_family.h")
    if '#include "sodium_family.h"' not in mod_defaults:
        errors.append("mod_defaults.cpp does not use the portable sodium_family.h classification")
    for needle in (
        "ConfigureSodiumFamilyDefaults",
        "KeepOneSodiumFamilyRenderer",
        "MovePathIfExists",
        "SodiumFamilyForFileName",
        "always_defer_chunk_updates_v2",
        "arena_memory_allocator",
        "use_advanced_staging_buffers",
        "cpu_render_ahead_limit",
    ):
        if needle not in mod_defaults:
            errors.append(f"mod_defaults.cpp is missing {needle}")
    for needle in ("embeddium-options.json", "rubidium-options.json", "sodium-options.json",
                   "PreferredSodiumFamilyIndex", "IsSodiumFamilyAddon"):
        if needle not in sodium_family:
            errors.append(f"sodium_family.h is missing {needle}")
    if "tests/sodium_family_test.cpp" not in read("tests/run-tests.sh"):
        errors.append("tests/run-tests.sh does not run the sodium family classification test")
    if "ConfigureSodiumDefaults(" in mod_defaults:
        errors.append("mod_defaults.cpp still calls the Fabric-only ConfigureSodiumDefaults")
    if 'CompareVersionNumbers(w2a(minecraftVersion), "1.20.1") > 0' not in mod_defaults:
        errors.append("mod_defaults.cpp legacy mixin repair still excludes Minecraft 1.20.1")
    if "ConfigureSodiumFamilyDefaults(gameDir, userModsDir, minecraftVersion)" not in mod_defaults:
        errors.append("mod_defaults.cpp does not call the Sodium-family defaults resolver")

    # 14. OSHI/JNA/COM hardware probes must not abort Forge mod loading on UWP.
    # Embeddium 0.3.x calls SystemInfo.getHardware().getGraphicsCards() from
    # SodiumMixinPlugin.onLoad(); OSHI 6.2.2 reaches JNA's Ole32/WMI from
    # WmiQueryHandler.initCOM(), which the app container blocks. A packaged
    # Ole32 shim plus -Djna.library.path gives JNA an absolute path and turns
    # the probe into a caught COMException with an empty WmiResult.
    ole32_source = read("shims/ole32_uwp.cpp")
    build_ps1 = read("build.ps1")
    minecraft_launch = read("MC.Xbox/launch/minecraft_launch.cpp")
    run_tests = read("tests/run-tests.sh")
    for needle in (
        "CoInitializeEx",
        "CoInitializeSecurity",
        "CoCreateInstance",
        "SysAllocString",
        "VariantClear",
        "0x80004005u",
        "WmiQueryHandler.queryWMI()",
    ):
        if needle not in ole32_source:
            errors.append(f"shims/ole32_uwp.cpp is missing {needle}")
    for needle in (
        "ole32_uwp.cpp",
        "$ole32ShimDll",
        "natives\\Ole32.dll",
        "natives\\OleAut32.dll",
        "Building Ole32/COM UWP shim",
    ):
        if needle not in build_ps1:
            errors.append(f"build.ps1 does not build/package the Ole32 shim ({needle})")
    for needle in ("-Djna.library.path=", "ole32ShimDir", "Ole32.dll", "wmi"):
        if needle.lower() not in minecraft_launch.lower():
            errors.append(f"minecraft_launch.cpp is missing the JNA COM shim wiring ({needle})")
    if "tests/ole32_shim_test.cpp" not in run_tests:
        errors.append("tests/run-tests.sh does not run the Ole32 shim contract test")
    matrix_validator = read("scripts/validate-modern-version-matrix.ps1")
    for needle in ('natives\\Ole32.dll', 'natives\\OleAut32.dll'):
        if needle not in matrix_validator:
            errors.append(f"validate-modern-version-matrix.ps1 does not verify {needle} in the APPX")
    for doc in ("docs/FAQ.md", "docs/FORK-NATIVE-NEOFORGE.es.md"):
        if "OSHI" not in read(doc):
            errors.append(f"{doc} does not document the OSHI/WMI COM shim")

    for warning in warnings:
        print(f"WARN  {warning}")
    for error in errors:
        print(f"ERROR {error}")
    if errors:
        print(f"\n{len(errors)} error(s), {len(warnings)} warning(s): fork configuration is inconsistent")
        return 1
    print(f"\nOK: {len(rows)} catalog targets, default {default_id}, {len(tokens)} config tokens, "
          f"{len(mods)} bundled Controlify mod(s), no Bandit controller builds ({len(warnings)} warning(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
