// Portable tests for the mod metadata index / cache invalidation.
#include "../MC.Xbox/mods/mod_index.h"

#include <cstdio>
#include <string>

static int g_failures = 0;

static void Expect(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL %s\n", what);
        ++g_failures;
    }
}

int main() {
    using namespace modindex;

    Snapshot before = {
        { "sodium-0.5.8.jar", 1000, 10, 20 },
        { "ferritecore.jar", 2000, 10, 21 },
        { "removed.jar", 3000, 10, 22 },
    };
    Snapshot after = {
        { "sodium-0.5.8.jar", 1000, 10, 20 },   // untouched
        { "ferritecore.jar", 2500, 10, 21 },    // size changed
        { "new-mod.jar", 4000, 10, 23 },        // added
    };
    const Diff diff = Compare(before, after);
    Expect(diff.unchanged.size() == 1 && diff.unchanged[0] == "sodium-0.5.8.jar",
           "unchanged JAR must be reused");
    Expect(diff.changed.size() == 1 && diff.changed[0] == "ferritecore.jar",
           "size change must invalidate");
    Expect(diff.added.size() == 1 && diff.added[0] == "new-mod.jar", "added JAR detected");
    Expect(diff.removed.size() == 1 && diff.removed[0] == "removed.jar", "removed JAR detected");
    Expect(diff.ChangedCount() == 3, "changed count");

    // Timestamp-only change must invalidate too.
    Snapshot timeChanged = before;
    timeChanged[0].writeLow = 999;
    Expect(Compare(before, timeChanged).changed.size() == 1,
           "timestamp change must invalidate even with the same size");

    // Round trip with characters that would otherwise break the TSV format.
    Index index;
    Index parsed;
    Expect(parsed.Parse(index.Serialize()), "empty index must parse");
    int reads = 0;
    index.Rebuild(Snapshot{
        { "mod%name.jar", 10, 1, 2 },
        { "tab\tname.jar", 20, 3, 4 },
    }, [&](const std::string&) { ++reads; return std::string("weird%id"); });
    Expect(reads == 2, "cold index reads every JAR once");
    Expect(parsed.Parse(index.Serialize()), "serialized index must parse");
    Expect(parsed.Entries().size() == 2, "all entries survive round trip");
    Expect(parsed.Entries()[0].stamp.name == "mod%name.jar", "percent escaped");
    Expect(parsed.Entries()[1].stamp.name == "tab\tname.jar", "tab escaped");
    Expect(parsed.Entries()[0].fabricId == "weird%id", "id escaped");

    // Warm rebuild: zero ZIP reads. One changed JAR: exactly one ZIP read.
    Snapshot warm = {
        { "mod%name.jar", 10, 1, 2 },
        { "tab\tname.jar", 20, 3, 4 },
    };
    reads = 0;
    const unsigned long long warmReads = parsed.Rebuild(warm,
        [&](const std::string&) { ++reads; return "id"; });
    Expect(warmReads == 0 && reads == 0, "warm launch must not reopen any JAR");
    warm[1].size = 99;
    reads = 0;
    const unsigned long long changedReads = parsed.Rebuild(warm,
        [&](const std::string&) { ++reads; return "newid"; });
    Expect(changedReads == 1 && reads == 1, "only the changed JAR is re-read");

    // Lookups: exact fabric id wins over a file-name token.
    Index lookups;
    lookups.Rebuild(Snapshot{
        { "other-1.0.jar", 10, 1, 1 },
        { "sodium-extra-1.0.jar", 20, 2, 2 },
        { "sodium-0.5.8.jar", 30, 3, 3 },
    }, [](const std::string& name) -> std::string {
        if (name == "sodium-0.5.8.jar") return "sodium";
        if (name == "sodium-extra-1.0.jar") return "sodium-extra";
        return "";
    });
    Expect(lookups.FindByIdOrName("sodium", "sodium") == "sodium-0.5.8.jar",
           "exact fabric id must beat a filename token");
    Expect(lookups.FindByIdOrName("no-such-id", "FERRITE") == "",
           "case-insensitive token with no match");
    Expect(lookups.FindByIdOrName("sodium-extra", "sodium") == "sodium-extra-1.0.jar",
           "second exact id");
    Expect(lookups.NameMatchesAny({ "banditvault_controller", "controlify" }) == false,
           "no duplicate/controller token present");
    Expect(lookups.NameMatchesAny({ "SODIUM" }) == false, "tokens are case-sensitive here");
    Expect(lookups.NameMatchesAny({ "sodium" }) == true, "token found");

    Expect(!parsed.Parse("WRONG-HEADER\n"), "bad header must be rejected");
    Expect(!parsed.Parse(std::string(Index::kHeader) + "\nbroken\n"), "bad row must be rejected");

    if (g_failures) {
        std::fprintf(stderr, "%d mod index test(s) failed\n", g_failures);
        return 1;
    }
    std::printf("mod index checks passed (diff by size/timestamp, warm cache, lookups, escaping)\n");
    return 0;
}
