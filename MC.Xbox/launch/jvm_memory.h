#pragma once

// JVM memory planning for the Xbox UWP host.
//
// Why this exists
// ---------------
// The launcher and Minecraft share ONE app memory budget. On a Series S in
// developer mode the PLM reports roughly 5120 MB for the whole process, and the
// host process is the one that owns it: Direct2D, Mesa/D3D12, LWJGL, JNA and the
// JVM all live inside the same job object. That means a heap that "fits" in the
// budget on paper can still abort with
//
//     Native memory allocation (mmap) failed to map 301989888 bytes.
//     Error detail: G1 virtual space
//
// because the *native* side of the JVM (metaspace, code cache, compressed class
// space, direct buffers, thread stacks, GC auxiliary structures) plus the
// graphics runtime grows on top of the Java heap.
//
// The old code hardcoded -Xmx3584M, left metaspace and the code cache unlimited,
// forced 32 MB G1 regions and kept G1ReservePercent=20. This module replaces it
// with an explicit budget: every consumer is bounded, the heap is derived from
// the memory actually available instead of a constant, and the result is logged
// so a crash report can be read against the configuration that produced it.
//
// The file is deliberately portable (no windows.h): the arithmetic and the hs_err
// classification are covered by tests/jvm_memory_test.cpp on any host.

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace jvmmem {

// Conservative is the default: it leaves the largest native headroom. Balanced
// is meant for a console that already runs well, Performance spends that
// headroom on the heap and is only useful when the budget allows it.
enum class Profile {
    Conservative,
    Balanced,
    Performance
};

struct ProfileLimits {
    Profile profile;
    const char* id;
    int heapCapMb;        // never exceed this heap, whatever the budget says
    int heapFloorMb;      // below this the pack will not boot, so stop instead
    int directMb;         // -XX:MaxDirectMemorySize
    int metaspaceMb;      // -XX:MaxMetaspaceSize
    int codeCacheMb;      // -XX:ReservedCodeCacheSize
    int classSpaceMb;     // -XX:CompressedClassSpaceSize
    int graphicsReserveMb; // Mesa/D3D12 + GL driver heaps, not visible to the JVM
    int safetyMarginMb;   // never hand the last megabytes to the heap
    int g1ReservePercent;
    int maxGcPauseMs;
};

inline const ProfileLimits& LimitsFor(Profile profile) {
    static const ProfileLimits kConservative = {
        Profile::Conservative, "conservative",
        2048, 1536,
        192, 384, 160, 192,
        320, 256,
        10, 50
    };
    static const ProfileLimits kBalanced = {
        Profile::Balanced, "balanced",
        2816, 2048,
        256, 448, 192, 256,
        320, 192,
        10, 50
    };
    static const ProfileLimits kPerformance = {
        Profile::Performance, "performance",
        3072, 2560,
        256, 448, 192, 256,
        320, 128,
        10, 50
    };
    switch (profile) {
    case Profile::Conservative:
        return kConservative;
    case Profile::Performance:
        return kPerformance;
    case Profile::Balanced:
    default:
        return kBalanced;
    }
}

inline const wchar_t* ProfileLabel(Profile profile) {
    switch (profile) {
    case Profile::Conservative:
        return L"Conservative";
    case Profile::Performance:
        return L"Performance";
    case Profile::Balanced:
    default:
        return L"Balanced";
    }
}

inline const wchar_t* ProfileDescription(Profile profile) {
    switch (profile) {
    case Profile::Conservative:
        return L"Smallest heap, most native headroom. Use it if the game still closes without a message.";
    case Profile::Performance:
        return L"Largest heap the console budget allows. Leaves less room for native memory.";
    case Profile::Balanced:
    default:
        return L"Fits a 100-150 mod pack and keeps a safety margin for native memory.";
    }
}

// Accepts the stored ids ("conservative", "balanced", "performance"), the UI
// labels and the older "low"/"medium"/"high"/"default" spellings.
inline Profile ParseProfile(const std::wstring& text, bool* recognised = nullptr) {
    std::wstring lower;
    lower.reserve(text.size());
    for (const wchar_t ch : text) {
        if (ch == L'\n' || ch == L'\r' || ch == L' ' || ch == L'\t') continue;
        // ASCII-only on purpose: the stored ids and the UI labels are ASCII, and
        // this keeps the helper free of locale-dependent case mapping.
        lower.push_back(ch >= L'A' && ch <= L'Z' ? static_cast<wchar_t>(ch - L'A' + L'a') : ch);
    }
    auto is = [&](const wchar_t* value) { return lower == value; };

    if (is(L"conservative") || is(L"low") || is(L"min") || is(L"minimum") || is(L"safe")) {
        if (recognised) *recognised = true;
        return Profile::Conservative;
    }
    if (is(L"balanced") || is(L"medium") || is(L"default") || is(L"normal") || is(L"auto")) {
        if (recognised) *recognised = true;
        return Profile::Balanced;
    }
    if (is(L"performance") || is(L"high") || is(L"max") || is(L"maximum")) {
        if (recognised) *recognised = true;
        return Profile::Performance;
    }
    if (recognised) *recognised = false;
    return Profile::Balanced;
}

// Fallback app budget when Windows will not tell us. The Series S developer-mode
// limit is ~5120 MB; that number is a fallback, never an assumption baked into
// the heap itself.
constexpr unsigned long long kFallbackBudgetMb = 5120ull;
// Baseline used only when AppMemoryUsage() cannot be read.
constexpr unsigned long long kAssumedBaselineMb = 384ull;

struct Inputs {
    unsigned long long appLimitMb = 0;   // 0 = Windows did not report a limit
    unsigned long long baselineMb = 0;   // measured AppMemoryUsage before the JVM
    unsigned long long physicalMb = 0;   // console RAM, logging only
    int modJarCount = 0;
    Profile profile = Profile::Balanced;
};

struct Plan {
    Profile profile = Profile::Balanced;
    bool budgetKnown = false;
    unsigned long long budgetMb = 0;
    unsigned long long baselineMb = 0;
    int graphicsReserveMb = 0;
    int heapMaxMb = 0;
    int heapMinMb = 0;
    int directMb = 0;
    int metaspaceMb = 0;
    int codeCacheMb = 0;
    int classSpaceMb = 0;
    int g1ReservePercent = 10;
    int maxGcPauseMs = 50;
    int jvmNativeMb = 0;        // everything the JVM reserves outside the heap
    unsigned long long expectedPeakMb = 0;
    unsigned long long headroomMb = 0;
    bool fits = true;           // expected peak inside the budget
    bool clampedToFloor = false;
    bool clampedToCap = false;
    std::string notes;

    // Heap actually offered to the JVM, in MiB.
    int heapMb() const { return heapMaxMb; }
};

namespace detail {

constexpr int kThreadStackMb = 96; // ~90 live threads at 1 MB, plus guard pages
constexpr int kGcAuxMb = 64;       // card table, mark bitmap, remembered sets
constexpr int kHeapRoundMb = 64;

inline void AppendNote(std::string& notes, const std::string& note) {
    if (note.empty()) return;
    if (!notes.empty()) notes += "; ";
    notes += note;
}

inline std::string Int(int value) { return std::to_string(value); }

} // namespace detail

inline Plan ComputePlan(const Inputs& in) {
    Plan plan;
    const ProfileLimits& limits = LimitsFor(in.profile);
    plan.profile = in.profile;
    plan.graphicsReserveMb = limits.graphicsReserveMb;
    plan.directMb = limits.directMb;
    plan.metaspaceMb = limits.metaspaceMb;
    plan.codeCacheMb = limits.codeCacheMb;
    plan.classSpaceMb = limits.classSpaceMb;
    plan.g1ReservePercent = limits.g1ReservePercent;
    plan.maxGcPauseMs = limits.maxGcPauseMs;

    // 1. Budget. Windows reports the real PLM limit; the constant is only a
    // fallback and is flagged in the log when it is used.
    if (in.appLimitMb >= 1536ull) {
        plan.budgetKnown = true;
        plan.budgetMb = in.appLimitMb;
    } else {
        plan.budgetKnown = false;
        plan.budgetMb = kFallbackBudgetMb;
        // A genuinely small machine must not get the console default.
        if (in.physicalMb > 0 && in.physicalMb < kFallbackBudgetMb + 2048ull) {
            plan.budgetMb = (std::max)(2048ull, in.physicalMb / 2ull);
        }
        detail::AppendNote(plan.notes, "app memory limit not reported, using fallback budget");
    }

    // 2. What the process already uses, and what it will need later.
    plan.baselineMb = in.baselineMb >= 128ull ? in.baselineMb : kAssumedBaselineMb;
    if (in.baselineMb < 128ull) {
        detail::AppendNote(plan.notes, "app usage unreadable, assumed baseline");
    }

    plan.jvmNativeMb = plan.directMb + plan.metaspaceMb + plan.codeCacheMb +
        plan.classSpaceMb + detail::kThreadStackMb + detail::kGcAuxMb;

    // 3. Heap = budget - baseline - graphics - native - margin, clamped.
    const unsigned long long fixedNative =
        plan.baselineMb + static_cast<unsigned long long>(plan.graphicsReserveMb) +
        static_cast<unsigned long long>(plan.jvmNativeMb) +
        static_cast<unsigned long long>(limits.safetyMarginMb);

    int heapMb = 0;
    if (plan.budgetMb > fixedNative) {
        const unsigned long long available = plan.budgetMb - fixedNative;
        heapMb = available > 4096ull ? 4096 : static_cast<int>(available);
    } else {
        detail::AppendNote(plan.notes, "budget below fixed native cost");
    }

    if (heapMb > limits.heapCapMb) {
        heapMb = limits.heapCapMb;
        plan.clampedToCap = true;
    }
    if (heapMb < limits.heapFloorMb) {
        heapMb = limits.heapFloorMb;
        plan.clampedToFloor = true;
        detail::AppendNote(plan.notes, "heap raised to the profile floor, budget is tight");
    }
    // G1 is happiest with the heap a whole number of large regions; rounding down
    // keeps every MiB we round away as headroom instead of spending it.
    heapMb -= heapMb % detail::kHeapRoundMb;
    if (heapMb < limits.heapFloorMb) heapMb = limits.heapFloorMb;

    plan.heapMaxMb = heapMb;
    // -Xms stays small on purpose: committing the whole heap at boot is exactly
    // what turns a tight budget into an immediate mmap failure.
    plan.heapMinMb = (std::min)(512, heapMb / 2);
    if (plan.heapMinMb < 256) plan.heapMinMb = 256;

    plan.expectedPeakMb = static_cast<unsigned long long>(plan.heapMaxMb) +
        static_cast<unsigned long long>(plan.jvmNativeMb) +
        static_cast<unsigned long long>(plan.graphicsReserveMb) + plan.baselineMb;
    plan.headroomMb = plan.budgetMb > plan.expectedPeakMb ? plan.budgetMb - plan.expectedPeakMb : 0ull;
    plan.fits = plan.expectedPeakMb <= plan.budgetMb;

    if (in.modJarCount > 0) {
        detail::AppendNote(plan.notes, "mods=" + std::to_string(in.modJarCount));
    }
    if (in.modJarCount > 150 && plan.metaspaceMb < 448) {
        detail::AppendNote(plan.notes, "large pack with a small metaspace cap, raise it in jvm_args.txt if load fails");
    }
    if (!plan.fits) {
        detail::AppendNote(plan.notes, "expected peak exceeds the app budget");
    }
    return plan;
}

// The complete memory and GC part of the JVM command line. Order matters for the
// experimental flags: UnlockExperimentalVMOptions has to be seen before
// G1NewSizePercent/G1ReservePercent.
inline std::vector<std::string> BuildArgs(const Plan& plan) {
    std::vector<std::string> args;
    args.reserve(16);
    args.push_back("-XX:+IgnoreUnrecognizedVMOptions");
    args.push_back("-XX:+UnlockExperimentalVMOptions");

    args.push_back("-Xmx" + std::to_string(plan.heapMaxMb) + "M");
    args.push_back("-Xms" + std::to_string(plan.heapMinMb) + "M");
    args.push_back("-XX:MaxDirectMemorySize=" + std::to_string(plan.directMb) + "M");
    // Bounded native reservations. Without these HotSpot reserves 1 GB of
    // compressed class space and grows metaspace without limit, which is the
    // budget that the heap was silently competing with.
    args.push_back("-XX:MaxMetaspaceSize=" + std::to_string(plan.metaspaceMb) + "M");
    args.push_back("-XX:ReservedCodeCacheSize=" + std::to_string(plan.codeCacheMb) + "M");
    args.push_back("-XX:CompressedClassSpaceSize=" + std::to_string(plan.classSpaceMb) + "M");

    args.push_back("-XX:+UseG1GC");
    args.push_back("-XX:MaxGCPauseMillis=" + std::to_string(plan.maxGcPauseMs));
    args.push_back("-XX:G1NewSizePercent=20");
    // Dropped from 20 to the default 10: the extra reserve shrank the usable
    // heap and did nothing for native headroom.
    args.push_back("-XX:G1ReservePercent=" + std::to_string(plan.g1ReservePercent));
    // No -XX:G1HeapRegionSize on purpose. Forcing 32 MB made G1 commit nine
    // regions (288 MiB) in a single mmap, which is the exact size that failed in
    // the field. HotSpot now picks 2-4 MB regions for this heap.
    args.push_back("-XX:+PerfDisableSharedMem");
    // Never commit the whole heap up front, and never pre-touch it.
    args.push_back("-XX:-AlwaysPreTouch");
    return args;
}

inline std::string PlanSummary(const Plan& plan) {
    std::string out;
    out += "profile=";
    out += LimitsFor(plan.profile).id;
    out += " budget=" + std::to_string(plan.budgetMb) + "M" + (plan.budgetKnown ? "" : "(fallback)");
    out += " baseline=" + std::to_string(plan.baselineMb) + "M";
    out += " heap=" + std::to_string(plan.heapMaxMb) + "M/" + std::to_string(plan.heapMinMb) + "M";
    out += " direct=" + std::to_string(plan.directMb) + "M";
    out += " metaspace=" + std::to_string(plan.metaspaceMb) + "M";
    out += " codecache=" + std::to_string(plan.codeCacheMb) + "M";
    out += " classspace=" + std::to_string(plan.classSpaceMb) + "M";
    out += " native=" + std::to_string(plan.jvmNativeMb) + "M";
    out += " graphics=" + std::to_string(plan.graphicsReserveMb) + "M";
    out += " peak=" + std::to_string(plan.expectedPeakMb) + "M";
    out += " headroom=" + std::to_string(plan.headroomMb) + "M";
    if (!plan.notes.empty()) out += " notes=" + plan.notes;
    return out;
}

// ---------------------------------------------------------------------------
// Native memory failure classification
// ---------------------------------------------------------------------------
//
// "InsufficientMemory" on its own tells the user nothing: heap exhaustion, a
// failed native mmap and a full metaspace need different answers. This reads the
// hs_err header and names the resource that ran out.

enum class FailureKind {
    None,
    JavaHeap,
    NativeMmap,
    G1VirtualSpace,
    CompressedClassSpace,
    Metaspace,
    CodeCache,
    DirectBuffer,
    ThreadStack,
    GcOverhead,
    Unknown
};

inline FailureKind ClassifyHsErr(const std::string& text) {
    if (text.empty()) return FailureKind::None;

    auto has = [&](const char* needle) { return text.find(needle) != std::string::npos; };

    // Specific mappings first: "Native memory allocation (mmap) failed" appears
    // for all of them, the header line says which space it was.
    if (has("Native memory allocation (mmap) failed") || has("Out of Memory Error")) {
        if (has("G1 virtual space") || has("G1")) {
            // Pure heap reservation failure has no -Xmx answer.
            if (has("Java heap space")) return FailureKind::JavaHeap;
            return FailureKind::G1VirtualSpace;
        }
        if (has("Compressed class space")) return FailureKind::CompressedClassSpace;
        if (has("Metaspace")) return FailureKind::Metaspace;
        if (has("code cache") || has("CodeCache")) return FailureKind::CodeCache;
        if (has("buffer") || has("Direct buffer")) return FailureKind::DirectBuffer;
        if (has("thread") && has("stack")) return FailureKind::ThreadStack;
        return FailureKind::NativeMmap;
    }
    if (has("java.lang.OutOfMemoryError: Java heap space") ||
        has("java.lang.OutOfMemoryError: Requested array size exceeds VM limit")) {
        return FailureKind::JavaHeap;
    }
    if (has("java.lang.OutOfMemoryError: Metaspace")) return FailureKind::Metaspace;
    if (has("java.lang.OutOfMemoryError: Compressed class space")) return FailureKind::CompressedClassSpace;
    if (has("java.lang.OutOfMemoryError: Direct buffer memory")) return FailureKind::DirectBuffer;
    if (has("GC overhead limit exceeded")) return FailureKind::GcOverhead;
    // The Minecraft crash report wraps the native abort in a Java exception.
    if (has("InsufficientMemory") || has("failed to map")) return FailureKind::NativeMmap;
    return FailureKind::Unknown;
}

// Short, localization-ready key. Returns "none" for FailureKind::None.
inline const char* FailureKey(FailureKind kind) {
    switch (kind) {
    case FailureKind::JavaHeap:
        return "heap";
    case FailureKind::NativeMmap:
        return "native";
    case FailureKind::G1VirtualSpace:
        return "g1";
    case FailureKind::CompressedClassSpace:
        return "classspace";
    case FailureKind::Metaspace:
        return "metaspace";
    case FailureKind::CodeCache:
        return "codecache";
    case FailureKind::DirectBuffer:
        return "direct";
    case FailureKind::ThreadStack:
        return "threads";
    case FailureKind::GcOverhead:
        return "gc";
    case FailureKind::Unknown:
        return "unknown";
    case FailureKind::None:
    default:
        return "none";
    }
}

// True when a bigger heap cannot help, so the UI must not suggest it.
inline bool IsNativeFailure(FailureKind kind) {
    return kind == FailureKind::NativeMmap || kind == FailureKind::G1VirtualSpace ||
        kind == FailureKind::CompressedClassSpace || kind == FailureKind::Metaspace ||
        kind == FailureKind::CodeCache || kind == FailureKind::DirectBuffer ||
        kind == FailureKind::ThreadStack;
}

} // namespace jvmmem
