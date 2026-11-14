#pragma once

// Adaptive JVM memory planning for the UWP/Xbox launcher.
//
// This header is deliberately free of windows.h / WinRT so it can be compiled
// and tested on any host (tests/memory_profile_test.cpp). The launcher measures
// the device (ReadAppMemoryBudget -> Windows.System.MemoryManager) and feeds
// the numbers in; everything below is pure arithmetic and string matching.
//
// Background for the "InsufficientMemory / Native memory allocation (mmap)
// failed to map 301989888 bytes. Error detail: G1 virtual space" crash:
// 301989888 = 9 * 32 MiB, i.e. G1 trying to commit nine 32 MiB heap regions at
// once. With -Xmx3584M the JVM reserves most of the ~5120 MiB UWP virtual
// budget up front; G1HeapRegionSize=32M, G1NewSizePercent=20 and
// G1ReservePercent=20 then force large, repeated commits exactly when the
// client re-enters a world and native graphics allocations have grown. The fix
// is a smaller heap ceiling with an explicit native reserve, plus letting G1
// choose its own region size and young generation.

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace memory {

enum class Profile {
    Conservative,
    Balanced,
    HighPerformance,
};

inline Profile ParseProfile(const std::string& value) {
    // A profile file written by hand often ends with a newline; trim it.
    const char* whitespace = " \t\r\n";
    const size_t first = value.find_first_not_of(whitespace);
    std::string lowered = first == std::string::npos
        ? std::string() : value.substr(first, value.find_last_not_of(whitespace) - first + 1);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(::tolower(c)); });
    if (lowered == "performance" || lowered == "high" || lowered == "altorendimiento") {
        return Profile::HighPerformance;
    }
    if (lowered == "balanced" || lowered == "equilibrado") {
        return Profile::Balanced;
    }
    // Unknown / empty / "conservative" all fall back to the stable default.
    return Profile::Conservative;
}

inline const char* ProfileName(Profile profile) {
    switch (profile) {
        case Profile::HighPerformance: return "performance";
        case Profile::Balanced: return "balanced";
        default: return "conservative";
    }
}

struct DeviceMemory {
    unsigned long long budgetMb = 0;   // MemoryManager::AppMemoryUsageLimit (UWP app limit)
    unsigned long long usedMb = 0;     // MemoryManager::AppMemoryUsage at launch time
    unsigned long long physicalAvailMb = 0; // optional GlobalMemoryStatusEx reading
    bool budgetKnown = false;          // false when WinRT threw (no apartment)
};

struct Plan {
    Profile profile = Profile::Conservative;
    unsigned long long budgetMb = 0;
    unsigned long long nativeReserveMb = 0;
    unsigned long long heapMaxMb = 0;
    unsigned long long heapMinMb = 0;
    unsigned long long directMb = 0;
    bool measured = false;   // false => safe fallback, not a real device reading
    bool fixedHeap = false;  // Xms == Xmx, avoids G1 mmap growth mid-game
};

// Kept deliberately modest: even a 16 GB console gives a UWP app only a few
// GiB of virtual/commit budget, and Mesa/D3D12 + LWJGL + metaspace + thread
// stacks all have to live outside the Java heap.
constexpr unsigned long long kMinNativeReserveMb = 1536;
constexpr unsigned long long kMinHeapMb = 1024;
constexpr unsigned long long kMaxHeapMb = 2816;
constexpr unsigned long long kFallbackBudgetMb = 4096;

struct ProfileTuning {
    unsigned int reservePercent; // share of the app budget kept away from the heap
    unsigned int heapPercent;    // share of the remaining space given to the heap
    unsigned long long directMb;
    bool fixedHeap;
};

inline ProfileTuning TuningFor(Profile profile) {
    switch (profile) {
        case Profile::HighPerformance:
            return { 32, 88, 512, false };
        case Profile::Balanced:
            return { 38, 75, 384, false };
        default:
            // Stability first: largest native reserve and a fully committed
            // heap so G1 never has to mmap more virtual space mid-game.
            return { 45, 60, 256, true };
    }
}

inline Plan ComputePlan(const DeviceMemory& device, Profile profile) {
    const ProfileTuning tuning = TuningFor(profile);

    Plan plan;
    plan.profile = profile;
    plan.measured = device.budgetKnown && device.budgetMb > 0;
    // Never trust a bogus reading: a budget smaller than the reserve would
    // otherwise collapse the heap to zero.
    if (plan.measured) {
        plan.budgetMb = device.budgetMb;
    } else if (device.physicalAvailMb > 0) {
        plan.budgetMb = (std::min)(device.physicalAvailMb, kFallbackBudgetMb);
    } else {
        plan.budgetMb = kFallbackBudgetMb;
    }
    if (plan.budgetMb < 2048) plan.budgetMb = 2048;

    const unsigned long long percentReserve =
        plan.budgetMb * tuning.reservePercent / 100ull;
    plan.nativeReserveMb = (std::max)(kMinNativeReserveMb, percentReserve);
    if (plan.nativeReserveMb >= plan.budgetMb) {
        plan.nativeReserveMb = plan.budgetMb / 2;
    }
    const unsigned long long usable = plan.budgetMb - plan.nativeReserveMb;
    unsigned long long heap = usable * tuning.heapPercent / 100ull;
    heap = (std::max)(kMinHeapMb, (std::min)(kMaxHeapMb, heap));
    // Round down to a whole 32 MiB multiple; keeps the numbers readable in
    // logs and avoids odd G1 region counts.
    plan.heapMaxMb = (heap / 32ull) * 32ull;
    if (plan.heapMaxMb < kMinHeapMb) plan.heapMaxMb = kMinHeapMb;

    plan.fixedHeap = tuning.fixedHeap;
    plan.heapMinMb = plan.fixedHeap
        ? plan.heapMaxMb
        : (std::min)(512ull, plan.heapMaxMb / 2ull);
    plan.directMb = (std::min)(tuning.directMb, plan.nativeReserveMb / 3ull);
    if (plan.directMb < 128ull) plan.directMb = 128ull;
    return plan;
}

inline std::vector<std::string> BuildJvmMemoryArgs(const Plan& plan) {
    std::vector<std::string> args;
    args.push_back("-Xmx" + std::to_string(plan.heapMaxMb) + "M");
    args.push_back("-Xms" + std::to_string(plan.heapMinMb) + "M");
    // Java 21 defaults MaxDirectMemorySize to Xmx. Leaving it implicit lets
    // LWJGL/Mesa consume virtually the whole heap budget as direct buffers, so
    // it must stay explicit and bounded.
    args.push_back("-XX:MaxDirectMemorySize=" + std::to_string(plan.directMb) + "M");
    args.push_back("-XX:+UseG1GC");
    // Intentionally absent: G1HeapRegionSize (forced the 9x32 MiB commit),
    // G1NewSizePercent, G1ReservePercent, MaxGCPauseMillis and
    // UnlockExperimentalVMOptions. G1's own ergonomics are safer on UWP.
    args.push_back("-XX:+IgnoreUnrecognizedVMOptions");
    args.push_back("-XX:+PerfDisableSharedMem");
    return args;
}

enum class Failure {
    Heap,
    Native,
    Direct,
    VirtualSpace,
    UwpLimit,
    Gc,
    Dependency,
    Unknown,
};

struct FailureInfo {
    Failure category = Failure::Unknown;
    const char* label = "unknown";
    const char* advice = "";
};

inline std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(::tolower(c)); });
    return value;
}

inline bool Contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

// Distinguish resource failures from mod/dependency failures so the crash
// screen never blames "not enough memory" for a missing library.
inline FailureInfo ClassifyFailure(const std::string& rawText) {
    const std::string text = Lowercase(rawText);
    if (Contains(text, "noclassdeffounderror") || Contains(text, "classnotfoundexception") ||
        Contains(text, "modloadingexception") || Contains(text, "missing dependency") ||
        Contains(text, "java.lang.modulefindexception") || Contains(text, "mixin apply failed")) {
        return { Failure::Dependency, "mod/dependency",
                 "This is a missing or incompatible mod/library, not a memory failure. Check the mod list and versions." };
    }
    if (Contains(text, "direct buffer memory") || Contains(text, "outofmemoryerror: direct")) {
        return { Failure::Direct, "direct buffers",
                 "Raise -XX:MaxDirectMemorySize or switch to the balanced profile." };
    }
    if (Contains(text, "java heap space") || Contains(text, "gc overhead limit exceeded")) {
        return { Failure::Heap, "Java heap",
                 "The Java heap is full. Use the balanced or performance profile, or reduce the modpack." };
    }
    if (Contains(text, "metaspace") || Contains(text, "compressed class space") ||
        Contains(text, "unable to create new native thread")) {
        return { Failure::Native, "native/metaspace",
                 "Native JVM structures ran out of room. Lower the heap ceiling to leave more native space." };
    }
    if (Contains(text, "could not reserve enough space") ||
        Contains(text, "failed to map") || Contains(text, "mmap failed") ||
        // hs_err uses both "insufficient memory" and the single-word
        // "InsufficientMemory" form seen on Xbox crash screens.
        Contains(text, "insufficient memory") || Contains(text, "insufficientmemory") ||
        Contains(text, "g1 virtual space") || Contains(text, "native memory allocation")) {
        if (Contains(text, "g1 virtual space") || Contains(text, "g1 heap region")) {
            return { Failure::Gc, "G1 virtual space",
                     "G1 could not commit more heap regions. The heap ceiling is too large for the UWP virtual budget; "
                     "use the conservative profile (fixed heap, smaller ceiling, default G1 region size)." };
        }
        if (Contains(text, "5120") || Contains(text, "appmemoryusagelimit") ||
            Contains(text, "virtual memory") || Contains(text, "commit limit")) {
            return { Failure::UwpLimit, "UWP app memory limit",
                     "The UWP app budget (not the console RAM) is exhausted. Lower the heap and direct buffer caps." };
        }
        return { Failure::VirtualSpace, "virtual address space",
                 "The process could not map more virtual memory. Leave headroom for Mesa/D3D12, LWJGL and metaspace." };
    }
    if (Contains(text, "outofmemoryerror")) {
        return { Failure::Native, "native allocation",
                 "A native allocation failed. Check the memory diagnostic in the crash ZIP." };
    }
    return { Failure::Unknown, "unknown", "" };
}

inline std::string JoinArgs(const std::vector<std::string>& args) {
    std::string out;
    for (const std::string& arg : args) {
        if (!out.empty()) out += " ";
        out += arg;
    }
    return out;
}

// Human-readable report written to logs/current/memory_diagnostic.txt and
// included in the crash ZIP. Receives the plan, the measured device state and
// the exact JVM options used, so no guessing is needed after a crash.
inline std::string DescribeDiagnostics(const Plan& plan, const DeviceMemory& device,
                                       const std::vector<std::string>& allArgs) {
    std::string out;
    out += "BanditLauncher memory diagnostic\n";
    out += "profile: " + std::string(ProfileName(plan.profile)) + "\n";
    out += "measurement: " + std::string(plan.measured ? "WinRT MemoryManager" : "safe fallback") + "\n";
    out += "app memory limit: " + std::to_string(device.budgetMb) + " MiB\n";
    out += "app memory used at launch: " + std::to_string(device.usedMb) + " MiB\n";
    out += "effective budget: " + std::to_string(plan.budgetMb) + " MiB\n";
    out += "reserved for native/direct/graphics/launcher: " + std::to_string(plan.nativeReserveMb) + " MiB\n";
    out += "heap min: " + std::to_string(plan.heapMinMb) + " MiB\n";
    out += "heap max: " + std::to_string(plan.heapMaxMb) + " MiB\n";
    out += "MaxDirectMemorySize: " + std::to_string(plan.directMb) + " MiB\n";
    out += "fixed heap (Xms==Xmx): " + std::string(plan.fixedHeap ? "yes" : "no") + "\n";
    out += "JVM options: " + JoinArgs(allArgs) + "\n";
    return out;
}

} // namespace memory
