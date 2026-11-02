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
