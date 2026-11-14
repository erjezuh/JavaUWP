#pragma once

// Persistent mod metadata index.
//
// Before this existed, every launch opened the mod folder five separate times and
// read `fabric.mod.json` out of every single jar to answer questions whose answer
// was in most cases already in the file name (PatchMoonlightPoiMixin,
// ConfigureSodiumFamilyDefaults, ConfigureDistantHorizonsDefaults,
// ConfigureLambdaControlsDefaults, ConfigureMidnightControlsDefaults and
// DirectoryHasBanditController each did their own pass). A 100-150 jar profile
// paid hundreds of jar reads before Java even started.
//
// The index stores, per jar, the size and last-write time plus the mod id that
// was read out of it. Entries are reused only while (size, write time) still
// match, so adding, replacing or deleting a jar invalidates exactly that jar and
// nothing else.
//
// Portable on purpose: tests/mod_index_test.cpp exercises the format, the
// invalidation and the lookup order on any host.

#include <algorithm>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

namespace modindex {

constexpr int kFormatVersion = 1;

struct Entry {
    std::string name;      // file name as on disk, UTF-8
    unsigned long long size = 0;
    unsigned long long writeTime = 0;
    std::string fabricId;  // empty when the jar has no fabric.mod.json
    bool probed = false;   // false: the jar was never opened
};

using Entries = std::vector<Entry>;

inline std::string LowerAscii(std::string value) {
    for (char& ch : value) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return value;
}

inline bool EndsWithJar(const std::string& name) {
    if (name.size() < 4) return false;
    const std::string tail = LowerAscii(name.substr(name.size() - 4));
    return tail == ".jar";
}

// Sorted by lower-case name so the order on disk (which the console does not
// guarantee) can never change a decision.
inline void SortEntries(Entries& entries) {
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        const std::string left = LowerAscii(a.name);
        const std::string right = LowerAscii(b.name);
        if (left != right) return left < right;
        return a.name < b.name;
    });
}

inline std::string Serialize(const Entries& entries) {
    std::string out = "# bandit mod index v" + std::to_string(kFormatVersion) + "\n";
    for (const Entry& entry : entries) {
        if (entry.name.empty()) continue;
        out += std::to_string(entry.size);
        out += '\t';
        out += std::to_string(entry.writeTime);
        out += '\t';
        out += entry.probed ? '1' : '0';
        out += '\t';
        out += entry.fabricId;
        out += '\t';
        out += entry.name;
        out += '\n';
    }
    return out;
}

inline bool Parse(const std::string& text, Entries& out) {
    out.clear();
    if (text.empty()) return false;

    size_t pos = 0;
    bool sawHeader = false;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) {
            if (end == text.size()) break;
            continue;
        }
        if (line[0] == '#') {
            const std::string expected = "# bandit mod index v" + std::to_string(kFormatVersion);
            if (line.substr(0, expected.size()) == expected) sawHeader = true;
            if (end == text.size()) break;
            continue;
        }

        std::vector<std::string> fields;
        size_t fieldStart = 0;
        while (true) {
            const size_t tab = line.find('\t', fieldStart);
            if (tab == std::string::npos) {
                fields.push_back(line.substr(fieldStart));
                break;
            }
            fields.push_back(line.substr(fieldStart, tab - fieldStart));
            fieldStart = tab + 1;
        }
        if (fields.size() < 5 || fields[4].empty()) {
            if (end == text.size()) break;
            continue;
        }

        Entry entry;
        try {
            entry.size = std::stoull(fields[0]);
            entry.writeTime = std::stoull(fields[1]);
        } catch (...) {
            if (end == text.size()) break;
            continue;
        }
        entry.probed = fields[2] == "1";
        entry.fabricId = fields[3];
        entry.name = fields[4];
        if (!EndsWithJar(entry.name)) {
            if (end == text.size()) break;
            continue;
        }
        out.push_back(std::move(entry));
        if (end == text.size()) break;
    }

    if (!sawHeader) {
        out.clear();
        return false;
    }
    SortEntries(out);
    return true;
}

// Reuses what the old index already knew about unchanged jars and marks the rest
// as needing a probe. Entries for jars that no longer exist are dropped.
inline Entries Merge(const Entries& previous, const Entries& scanned) {
    Entries merged;
    merged.reserve(scanned.size());
    for (const Entry& current : scanned) {
        if (!EndsWithJar(current.name)) continue;
        Entry entry = current;
        entry.probed = false;
        entry.fabricId.clear();
        for (const Entry& old : previous) {
            if (old.name != current.name) continue;
            if (old.size != current.size) break;
            if (old.writeTime != current.writeTime) break;
            entry.fabricId = old.fabricId;
            entry.probed = old.probed;
            break;
        }
        merged.push_back(std::move(entry));
    }
    SortEntries(merged);
    return merged;
}

enum class LookupSource {
    NameToken,     // resolved from the file name, no jar was opened
    FabricIdCache, // resolved from an index entry read on an earlier launch
    Probe,         // caller must open this jar and try again
    NotFound
};

struct Lookup {
    int index = -1;
    LookupSource source = LookupSource::NotFound;
};

// Resolution order, cheapest first:
//   1. the file name contains the token -> assume it is that mod (this is what
//      the old code did as a *fallback*, after opening every jar first);
//   2. a cached fabric id matches;
//   3. hand back the next jar that was never probed, so the caller can fill the
//      cache incrementally instead of rescanning the folder.
inline Lookup LookupMod(const Entries& entries, const std::string& modId, const std::string& lowerToken) {
    const std::string token = LowerAscii(lowerToken);
    const std::string wanted = LowerAscii(modId);

    if (!token.empty()) {
        int firstMatch = -1;
        for (size_t i = 0; i < entries.size(); ++i) {
            const std::string name = LowerAscii(entries[i].name);
            if (name.find(token) == std::string::npos) continue;
            if (firstMatch < 0) firstMatch = static_cast<int>(i);
            if (!wanted.empty() && LowerAscii(entries[i].fabricId) == wanted) {
                return Lookup{ static_cast<int>(i), LookupSource::NameToken };
            }
        }
        if (firstMatch >= 0) return Lookup{ firstMatch, LookupSource::NameToken };
    }

    if (!wanted.empty()) {
        for (size_t i = 0; i < entries.size(); ++i) {
            if (!entries[i].probed) continue;
            if (LowerAscii(entries[i].fabricId) == wanted) {
                return Lookup{ static_cast<int>(i), LookupSource::FabricIdCache };
            }
        }
    }

    for (size_t i = 0; i < entries.size(); ++i) {
        if (!entries[i].probed) return Lookup{ static_cast<int>(i), LookupSource::Probe };
    }
    return Lookup{ -1, LookupSource::NotFound };
}

// Same mod id in more than one jar. Reported, never deleted: the launcher must
// not decide for the user which of two jars to keep.
inline std::vector<std::string> DuplicateIds(const Entries& entries) {
    std::vector<std::string> ids;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!entries[i].probed || entries[i].fabricId.empty()) continue;
        const std::string id = LowerAscii(entries[i].fabricId);
        bool duplicate = false;
        for (size_t j = 0; j < i; ++j) {
            if (entries[j].probed && LowerAscii(entries[j].fabricId) == id) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            for (size_t j = i + 1; j < entries.size(); ++j) {
                if (entries[j].probed && LowerAscii(entries[j].fabricId) == id) {
                    ids.push_back(id);
                    break;
                }
            }
        }
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

} // namespace modindex
