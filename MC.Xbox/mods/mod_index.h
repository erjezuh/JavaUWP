#pragma once

// Mod metadata index. Portable (no windows.h / ZIP code) so the invalidation
// rules are covered by tests/mod_index_test.cpp.
//
// ConfigureKnownModDefaults() used to walk mods/*.jar once per lookup (sodium,
// moonlight, lambdacontrols, midnightcontrols, distanthorizons, two controller
// checks...). Each of those walks also opened *every* JAR to read
// fabric.mod.json, so a 150-mod pack cost roughly 8*150 ZIP opens per launch.
// The index enumerates the directory once, caches each fabric.mod.json id
// keyed by (name, size, lastWriteTime), and answers all lookups in memory.
//
// Never written inside mods/: the cache file lives in the launcher's own
// config directory, so modpack files are untouched.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace modindex {

struct Stamp {
    std::string name;             // UTF-8, file name only
    unsigned long long size = 0;
    unsigned long long writeHigh = 0;
    unsigned long long writeLow = 0;

    bool SameFile(const Stamp& other) const {
        return name == other.name && size == other.size &&
            writeHigh == other.writeHigh && writeLow == other.writeLow;
    }
};

using Snapshot = std::vector<Stamp>;

struct Entry {
    Stamp stamp;
    std::string fabricId;   // empty when the JAR has no parseable fabric.mod.json
};

struct Diff {
    std::vector<std::string> added;
    std::vector<std::string> removed;
    std::vector<std::string> changed;   // same name, different size or timestamp
    std::vector<std::string> unchanged;

    unsigned long long ChangedCount() const {
        return added.size() + removed.size() + changed.size();
    }
};

inline Diff Compare(const Snapshot& cached, const Snapshot& current) {
    Diff diff;
    for (const Stamp& now : current) {
        bool found = false;
        for (const Stamp& old : cached) {
            if (old.name != now.name) continue;
            found = true;
            if (old.SameFile(now)) diff.unchanged.push_back(now.name);
            else diff.changed.push_back(now.name);
            break;
        }
        if (!found) diff.added.push_back(now.name);
    }
    for (const Stamp& old : cached) {
        bool found = false;
        for (const Stamp& now : current) {
            if (now.name == old.name) { found = true; break; }
        }
        if (!found) diff.removed.push_back(old.name);
    }
    return diff;
}

inline std::string Escape(const std::string& value) {
    std::string out;
    for (unsigned char c : value) {
        switch (c) {
            case '%': out += "%25"; break;
            case '\t': out += "%09"; break;
            case '\n': out += "%0A"; break;
            case '\r': out += "%0D"; break;
            default: out += static_cast<char>(c);
        }
    }
    return out;
}

inline std::string Unescape(const std::string& value) {
    std::string out;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const std::string code = value.substr(i + 1, 2);
            if (code == "25") { out += '%'; i += 2; continue; }
            if (code == "09") { out += '\t'; i += 2; continue; }
            if (code == "0A") { out += '\n'; i += 2; continue; }
            if (code == "0D") { out += '\r'; i += 2; continue; }
        }
        out += value[i];
    }
    return out;
}

inline std::vector<std::string> SplitTabs(const std::string& line) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        const size_t tab = line.find('\t', start);
        if (tab == std::string::npos) {
            parts.push_back(line.substr(start));
            break;
        }
        parts.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
    return parts;
}

inline unsigned long long ParseUnsigned(const std::string& value) {
    unsigned long long result = 0;
    for (char c : value) {
        if (c < '0' || c > '9') return 0;
        result = result * 10ull + static_cast<unsigned long long>(c - '0');
    }
    return result;
}

// Stable, filesystem-safe suffix for a per-directory cache file. The launcher
// indexes both the profile mods folder and the bundled-mods folder, each with
// its own cache, without ever writing inside the modpack's mods/ directory.
inline std::string CacheKeyFor(const std::string& modsDir) {
    unsigned long long hash = 14695981039346656037ull;
    for (unsigned char c : modsDir) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%016llx", hash);
    return std::string(buffer);
}

class Index {
public:
    static constexpr const char* kHeader = "BANDIT-MOD-INDEX-v1";

    bool Parse(const std::string& text) {
        entries_.clear();
        size_t pos = 0;
        bool headerSeen = false;
        while (pos <= text.size()) {
            const size_t end = text.find('\n', pos);
            const size_t lineEnd = end == std::string::npos ? text.size() : end;
            std::string line = text.substr(pos, lineEnd - pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) {
                if (!headerSeen) {
                    if (line != kHeader) return false;
                    headerSeen = true;
                } else {
                    const std::vector<std::string> parts = SplitTabs(line);
                    if (parts.size() != 5) return false;
                    Entry entry;
                    entry.stamp.name = Unescape(parts[0]);
                    entry.stamp.size = ParseUnsigned(parts[1]);
                    entry.stamp.writeHigh = ParseUnsigned(parts[2]);
                    entry.stamp.writeLow = ParseUnsigned(parts[3]);
                    entry.fabricId = Unescape(parts[4]);
                    if (entry.stamp.name.empty()) return false;
                    entries_.push_back(entry);
                }
            }
            if (end == std::string::npos) break;
            pos = end + 1;
        }
        return headerSeen;
    }

    std::string Serialize() const {
        std::string out = kHeader;
        out += "\n";
        for (const Entry& entry : entries_) {
            out += Escape(entry.stamp.name) + "\t" +
                std::to_string(entry.stamp.size) + "\t" +
                std::to_string(entry.stamp.writeHigh) + "\t" +
                std::to_string(entry.stamp.writeLow) + "\t" +
                Escape(entry.fabricId) + "\n";
        }
        return out;
    }

    // Rebuild against the current directory snapshot. `readId` is only invoked
    // for JARs whose (size, timestamp) changed or which are new, so a warm
    // launch performs zero ZIP reads.
    unsigned long long Rebuild(
        const Snapshot& current,
        const std::function<std::string(const std::string&)>& readId) {
        std::vector<Entry> next;
        unsigned long long zipReads = 0;
        next.reserve(current.size());
        for (const Stamp& stamp : current) {
            Entry entry;
            entry.stamp = stamp;
            bool cached = false;
            for (const Entry& old : entries_) {
                if (old.stamp.SameFile(stamp)) {
                    entry.fabricId = old.fabricId;
                    cached = true;
                    break;
                }
            }
            if (!cached) {
                entry.fabricId = readId(stamp.name);
                ++zipReads;
            }
            next.push_back(entry);
        }
        entries_.swap(next);
        return zipReads;
    }

    const std::vector<Entry>& Entries() const { return entries_; }

    // Exact fabric.mod.json id first, then a case-insensitive file-name token.
    // Returns the file name (not a path); the caller joins it with the mods dir.
    std::string FindByIdOrName(const std::string& modId,
                               const std::string& fileNameToken) const {
        if (!modId.empty()) {
            for (const Entry& entry : entries_) {
                if (entry.fabricId == modId) return entry.stamp.name;
            }
        }
        if (!fileNameToken.empty()) {
            std::string token = fileNameToken;
            std::transform(token.begin(), token.end(), token.begin(),
                           [](unsigned char c) { return static_cast<char>(::tolower(c)); });
            for (const Entry& entry : entries_) {
                std::string name = entry.stamp.name;
                std::transform(name.begin(), name.end(), name.begin(),
                               [](unsigned char c) { return static_cast<char>(::tolower(c)); });
                if (name.find(token) != std::string::npos) return entry.stamp.name;
            }
        }
        return std::string();
    }

    bool NameMatchesAny(const std::vector<std::string>& tokens) const {
        for (const Entry& entry : entries_) {
            std::string name = entry.stamp.name;
            std::transform(name.begin(), name.end(), name.begin(),
                           [](unsigned char c) { return static_cast<char>(::tolower(c)); });
            for (const std::string& token : tokens) {
                if (name.find(token) != std::string::npos) return true;
            }
        }
        return false;
    }

private:
    std::vector<Entry> entries_;
};

} // namespace modindex
