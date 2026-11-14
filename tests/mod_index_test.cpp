// Portable checks for MC.Xbox/mods/mod_index.h: cache format, invalidation and
// the lookup order that replaces five separate jar scans per launch.

#include "../MC.Xbox/mods/mod_index.h"

#include <cstdio>
#include <string>

namespace {

int g_failures = 0;

void Check(bool condition, const char* what, const std::string& detail = std::string()) {
    if (condition) return;
    std::printf("  FAIL %s%s%s\n", what, detail.empty() ? "" : " :: ", detail.c_str());
    ++g_failures;
}

void CheckEq(long long actual, long long expected, const char* what) {
    if (actual == expected) return;
    std::printf("  FAIL %s (got %lld, want %lld)\n", what, actual, expected);
    ++g_failures;
}

modindex::Entry Make(const std::string& name, unsigned long long size, unsigned long long time,
    const std::string& id, bool probed) {
    modindex::Entry entry;
    entry.name = name;
    entry.size = size;
    entry.writeTime = time;
    entry.fabricId = id;
    entry.probed = probed;
    return entry;
}

void TestRoundTrip() {
    modindex::Entries entries = {
        Make("sodium-0.6.13.jar", 100, 11, "sodium", true),
        Make("Moonlight-1.21.1.jar", 200, 22, "moonlight", true),
        Make("weird.name.jar", 300, 33, "", false)
    };
    const std::string text = modindex::Serialize(entries);
    Check(text.rfind("# bandit mod index v1", 0) == 0, "index carries a version header", text);

    modindex::Entries parsed;
    Check(modindex::Parse(text, parsed), "serialized index parses back");
    CheckEq(static_cast<long long>(parsed.size()), 3, "every entry survives the round trip");
    // Sorted by lower-case name: Moonlight, sodium, weird.
    Check(parsed[0].name == "Moonlight-1.21.1.jar", "entries are sorted case-insensitively",
        parsed[0].name);
    CheckEq(static_cast<long long>(parsed[0].size), 200, "size survives the round trip");
    CheckEq(static_cast<long long>(parsed[0].writeTime), 22, "write time survives the round trip");
    Check(parsed[0].fabricId == "moonlight", "mod id survives the round trip");
    Check(parsed[2].probed == false, "an unprobed entry stays unprobed");
}

void TestParseRejectsJunk() {
    modindex::Entries parsed;
    Check(!modindex::Parse("", parsed), "an empty index is rejected");
    Check(!modindex::Parse("# something else\n1\t2\t1\tid\ta.jar\n", parsed),
        "an index without the version header is rejected");
    Check(modindex::Parse("# bandit mod index v1\nnot-a-row\n", parsed),
        "a header-only index with junk rows still parses");
    CheckEq(static_cast<long long>(parsed.size()), 0, "a malformed row is skipped, not fatal");

    Check(modindex::Parse("# bandit mod index v1\n1\t2\t1\tid\tnotes.txt\n", parsed),
        "a header-only index parses");
    CheckEq(static_cast<long long>(parsed.size()), 0, "non-jar rows are dropped");

    Check(modindex::Parse("# bandit mod index v1\n5\t6\t1\tid\tfine.jar\n", parsed),
        "a valid single row parses");
    CheckEq(static_cast<long long>(parsed.size()), 1, "one row is loaded");
}

void TestMergeInvalidatesOnlyChangedFiles() {
    const modindex::Entries previous = {
        Make("a.jar", 10, 100, "moda", true),
        Make("b.jar", 20, 200, "modb", true),
        Make("gone.jar", 30, 300, "modgone", true)
    };
    const modindex::Entries scanned = {
        Make("a.jar", 10, 100, "", false),   // unchanged
        Make("b.jar", 21, 200, "", false),   // replaced: size changed
        Make("c.jar", 40, 400, "", false)    // new
    };

    const modindex::Entries merged = modindex::Merge(previous, scanned);
    CheckEq(static_cast<long long>(merged.size()), 3, "removed jars are dropped from the index");
    for (const modindex::Entry& entry : merged) {
        if (entry.name == "a.jar") {
            Check(entry.probed, "an unchanged jar keeps its cached id");
            Check(entry.fabricId == "moda", "an unchanged jar keeps the right id");
        } else if (entry.name == "b.jar") {
            Check(!entry.probed, "a replaced jar is re-probed");
            Check(entry.fabricId.empty(), "a replaced jar loses its stale id");
        } else if (entry.name == "c.jar") {
            Check(!entry.probed, "a new jar starts unprobed");
        } else {
            Check(false, "unexpected jar in the merged index", entry.name);
        }
    }

    // The write time alone is enough to invalidate.
    const modindex::Entries touched = modindex::Merge(previous, { Make("a.jar", 10, 101, "", false) });
    CheckEq(static_cast<long long>(touched.size()), 1, "merge follows the scan");
    Check(!touched[0].probed, "a touched jar is re-probed");
}

void TestLookupOrder() {
    using modindex::LookupSource;

    modindex::Entries entries = {
        Make("moonlight-1.21.1.jar", 10, 100, "moonlight", false),
        Make("unknown-mod.jar", 20, 200, "", false),
        Make("ControlsPlus.jar", 30, 300, "lambdacontrols", true)
    };
    modindex::SortEntries(entries);

    // 1. The file name is enough; the jar is never opened.
    modindex::Lookup byName = modindex::LookupMod(entries, "moonlight", "moonlight");
    Check(byName.source == LookupSource::NameToken, "a file name match wins");
    Check(byName.index >= 0 && entries[static_cast<size_t>(byName.index)].name ==
              "moonlight-1.21.1.jar",
        "the name match points at the right jar");

    // 2. Cached id when the name does not contain the token.
    modindex::Lookup byId = modindex::LookupMod(entries, "lambdacontrols", "lambdacontrols");
    Check(byId.source == LookupSource::NameToken || byId.source == LookupSource::FabricIdCache,
        "a re-named mod is found");

    modindex::Entries renamed = { Make("controls-plus.jar", 30, 300, "lambdacontrols", true) };
    modindex::Lookup cached = modindex::LookupMod(renamed, "lambdacontrols", "lambdacontrols");
    Check(cached.source == LookupSource::FabricIdCache, "a cached id is used without opening the jar");
    CheckEq(cached.index, 0, "the cached id points at the right entry");

    // 3. Nothing known: hand back a jar to probe.
    modindex::Entries unknown = {
        Make("aaa.jar", 1, 1, "", false),
        Make("bbb.jar", 2, 2, "", false)
    };
    modindex::Lookup probe = modindex::LookupMod(unknown, "mystery", "mystery");
    Check(probe.source == LookupSource::Probe, "an unknown folder asks for a probe");
    CheckEq(probe.index, 0, "probing starts at the first unprobed jar");

    // 4. Nothing left to probe.
    unknown[0].probed = true;
    unknown[0].fabricId = "other";
    unknown[1].probed = true;
    unknown[1].fabricId = "another";
    Check(modindex::LookupMod(unknown, "mystery", "mystery").source == LookupSource::NotFound,
        "an exhausted index reports not found");
}

void TestDuplicates() {
    const modindex::Entries entries = {
        Make("jei-1.jar", 1, 1, "jei", true),
        Make("jei-2.jar", 2, 2, "jei", true),
        Make("sodium.jar", 3, 3, "sodium", true),
        Make("unprobed.jar", 4, 4, "", false)
    };
    const std::vector<std::string> duplicates = modindex::DuplicateIds(entries);
    CheckEq(static_cast<long long>(duplicates.size()), 1, "one duplicated mod id is reported");
    Check(!duplicates.empty() && duplicates[0] == "jei", "the duplicated id is named");
}

void TestLookupTableMatchesSlowPath() {
    using modindex::LookupSource;

    modindex::Entries entries = {
        Make("sodium-0.6.13.jar", 100, 11, "sodium", true),
        Make("moonlight-1.21.1.jar", 10, 100, "", false),
        Make("jei-1.jar", 1, 1, "jei", true),
        Make("jei-2.jar", 2, 2, "jei", true),
        Make("controls-plus.jar", 30, 300, "lambdacontrols", true),
        Make("aaa-unknown.jar", 5, 5, "", false)
    };
    modindex::SortEntries(entries);

    modindex::LookupTable table = modindex::BuildLookupTable(entries);
    CheckEq(static_cast<long long>(table.lowerNames.size()),
        static_cast<long long>(entries.size()), "the table has one lowercased name per entry");
    Check(!table.lowerNames.empty() && table.lowerNames[0] == modindex::LowerAscii(entries[0].name),
        "the stored name is lowercased");

    const char* ids[] = {"sodium", "jei", "lambdacontrols", "moonlight", "mystery", ""};
    const char* tokens[] = {"sodium", "jei", "policy", "moonlight", "mystery", "nomatch"};
    for (const char* id : ids) {
        for (const char* token : tokens) {
            const modindex::Lookup slow = modindex::LookupMod(entries, id, token);
            const modindex::Lookup fast = modindex::LookupMod(entries, table, id, token);
            Check(fast.index == slow.index, "the table path agrees on the index",
                std::string(id) + "/" + token);
            Check(static_cast<int>(fast.source) == static_cast<int>(slow.source),
                "the table path agrees on the source", std::string(id) + "/" + token);
        }
    }

    // NoteProbed keeps the table in sync one entry at a time.
    size_t firstUnprobed = entries.size();
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!entries[i].probed) { firstUnprobed = i; break; }
    }
    if (firstUnprobed < entries.size()) {
        const modindex::Lookup before = modindex::LookupMod(entries, table, "hidden", "hidden");
        Check(before.source == LookupSource::Probe, "an unprobed jar is offered for a probe");
        entries[firstUnprobed].probed = true;
        entries[firstUnprobed].fabricId = "hiddenmod";
        modindex::NoteProbed(table, entries, static_cast<int>(firstUnprobed));
        const modindex::Lookup after = modindex::LookupMod(entries, table, "hiddenmod", "nothing-matches");
        Check(after.source == LookupSource::FabricIdCache,
            "a jar probed after the table was built is cached in the map");
        Check(after.index == static_cast<int>(firstUnprobed), "the cached id points at the probed jar");
    }

    // A stale table (entries changed size) must not answer from the fast path.
    modindex::Entries grown = entries;
    grown.push_back(Make("late.jar", 9, 9, "late", true));
    Check(modindex::LookupMod(grown, table, "late", "late").index >= 0,
        "a stale table falls back instead of failing");
}

void TestDuplicateTableMatchesSlowPath() {
    const modindex::Entries entries = {
        Make("jei-1.jar", 1, 1, "jei", true),
        Make("sodium.jar", 3, 3, "sodium", true),
        Make("jei-2.jar", 2, 2, "jei", true),
        Make("unprobed.jar", 4, 4, "", false),
        Make("JEI-3.jar", 6, 6, "JEI", true)
    };
    const modindex::LookupTable table = modindex::BuildLookupTable(entries);
    const std::vector<std::string> slow = modindex::DuplicateIds(entries);
    const std::vector<std::string> fast = modindex::DuplicateIds(entries, table);
    CheckEq(static_cast<long long>(fast.size()), static_cast<long long>(slow.size()),
        "the table reports the same number of duplicates");
    Check(fast == slow, "the table reports the duplicates in the same order");
    CheckEq(static_cast<long long>(fast.size()), 1, "one duplicated id");
    Check(!fast.empty() && fast[0] == "jei", "ids compare case-insensitively");
}

} // namespace

int main() {
    std::printf("== Mod index cache ==\n");
    TestRoundTrip();
    TestParseRejectsJunk();
    TestMergeInvalidatesOnlyChangedFiles();
    TestLookupOrder();
    TestDuplicates();
    TestLookupTableMatchesSlowPath();
    TestDuplicateTableMatchesSlowPath();

    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("Mod index cache checks passed\n");
    return 0;
}
