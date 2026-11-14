#pragma once

// Launch phase accounting. Portable (no windows.h) so the timing arithmetic is
// covered by tests/launch_phase_test.cpp. The caller supplies GetTickCount64()
// values; this header never reads the clock itself.
//
// The old launcher reported 12/24/38/52/64 % at arbitrary points. Those numbers
// were not measurements. Phases that cannot be measured now use an
// indeterminate progress value (-1), which the existing UI renders as a pulsing
// bar with "Loading..." instead of a fake percentage. Only real counters (e.g.
// JARs indexed out of JARs found) produce a numeric value.

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace launchphase {

enum class Phase {
    CheckingFiles,
    AnalyzingMods,
    PreparingDependencies,
    PreparingJava,
    StartingLoader,
    LoadingMinecraft,
};

constexpr int kPhaseCount = 6;

inline const wchar_t* Label(Phase phase) {
    switch (phase) {
        case Phase::CheckingFiles: return L"Checking files";
        case Phase::AnalyzingMods: return L"Analyzing mods";
        case Phase::PreparingDependencies: return L"Preparing dependencies";
        case Phase::PreparingJava: return L"Preparing Java";
        case Phase::StartingLoader: return L"Starting NeoForge";
        case Phase::LoadingMinecraft: return L"Loading Minecraft";
    }
    return L"Preparing Minecraft";
}

inline const char* Key(Phase phase) {
    switch (phase) {
        case Phase::CheckingFiles: return "checking_files";
        case Phase::AnalyzingMods: return "analyzing_mods";
        case Phase::PreparingDependencies: return "preparing_dependencies";
        case Phase::PreparingJava: return "preparing_java";
        case Phase::StartingLoader: return "starting_neoforge";
        case Phase::LoadingMinecraft: return "loading_minecraft";
    }
    return "unknown";
}

// -1 means "indeterminate": the UI must not draw a percentage.
constexpr float kIndeterminate = -1.0f;

inline float MeasuredProgress(unsigned long long done, unsigned long long total) {
    if (total == 0) return kIndeterminate;
    const double value = (double)(std::min)(done, total) / (double)total;
    return (float)(std::max)(0.0, (std::min)(1.0, value));
}

struct Record {
    Phase phase = Phase::CheckingFiles;
    unsigned long long startMs = 0;
    unsigned long long endMs = 0;
    bool closed = false;
    bool indeterminate = true;

    unsigned long long ElapsedMs() const {
        // A wrapped tick counter must not become a gigantic duration.
        return closed && endMs >= startMs ? endMs - startMs : 0ull;
    }
};

class Timeline {
public:
    void Begin(Phase phase, unsigned long long nowMs) {
        // A phase is entered once per launch; re-entering closes the previous one.
        for (Record& record : records_) {
            if (!record.closed && record.phase == phase) {
                record.endMs = nowMs;
                record.closed = true;
            }
        }
        Record record;
        record.phase = phase;
        record.startMs = nowMs;
        record.indeterminate = true;
        records_.push_back(record);
    }

    void End(Phase phase, unsigned long long nowMs) {
        for (auto it = records_.rbegin(); it != records_.rend(); ++it) {
            if (it->phase == phase && !it->closed) {
                it->endMs = nowMs;
                it->closed = true;
                return;
            }
        }
    }

    void MarkMeasured(Phase phase) {
        for (auto it = records_.rbegin(); it != records_.rend(); ++it) {
            if (it->phase == phase && !it->closed) {
                it->indeterminate = false;
                return;
            }
        }
    }

    void CloseAll(unsigned long long nowMs) {
        for (Record& record : records_) {
            if (!record.closed) {
                record.endMs = nowMs;
                record.closed = true;
            }
        }
    }

    const std::vector<Record>& Records() const { return records_; }

    unsigned long long TotalMs() const {
        unsigned long long total = 0;
        for (const Record& record : records_) total += record.ElapsedMs();
        return total;
    }

    unsigned long long PhaseMs(Phase phase) const {
        unsigned long long total = 0;
        for (const Record& record : records_) {
            if (record.phase == phase) total += record.ElapsedMs();
        }
        return total;
    }

    // One line per phase plus a total, ready for mc_launch.log. This is the
    // data needed for an honest before/after comparison; no values are invented.
    std::string FormatLog() const {
        std::string out = "Launch phase timings (ms):";
        for (Phase phase : { Phase::CheckingFiles, Phase::AnalyzingMods,
                             Phase::PreparingDependencies, Phase::PreparingJava,
                             Phase::StartingLoader, Phase::LoadingMinecraft }) {
            out += " " + std::string(Key(phase)) + "=" + std::to_string(PhaseMs(phase));
        }
        out += " total=" + std::to_string(TotalMs());
        return out;
    }

private:
    std::vector<Record> records_;
};

// Ensures the timing report is emitted on every exit path, including early
// failures during preparation (which are the ones most worth measuring).
class ScopeExit {
public:
    explicit ScopeExit(std::function<void()> fn) : fn_(std::move(fn)) {}
    ~ScopeExit() { if (fn_) fn_(); }
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
private:
    std::function<void()> fn_;
};

} // namespace launchphase
