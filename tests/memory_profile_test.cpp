// Portable tests for the adaptive JVM memory planner.
#include "../MC.Xbox/launch/memory_profile.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_failures = 0;

static void Expect(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL %s\n", what);
        ++g_failures;
    }
}

static bool Has(const std::vector<std::string>& args, const std::string& needle) {
    for (const std::string& arg : args) {
        if (arg.find(needle) != std::string::npos) return true;
    }
    return false;
}

int main() {
    using namespace memory;

    DeviceMemory device;
    device.budgetKnown = true;
    device.budgetMb = 5120;   // reported UWP virtual/app limit
    device.usedMb = 1744;
    device.physicalAvailMb = 8000;

    const Plan conservative = ComputePlan(device, Profile::Conservative);
    const Plan balanced = ComputePlan(device, Profile::Balanced);
    const Plan high = ComputePlan(device, Profile::HighPerformance);

    // The old crashing value was -Xmx3584M. Every profile must stay below it
    // and leave a substantial native reserve.
    for (const Plan* plan : { &conservative, &balanced, &high }) {
        Expect(plan->measured, "measured budget must be used");
        Expect(plan->heapMaxMb < 3584, "heap must be smaller than the crashing 3584 MiB");
        Expect(plan->heapMaxMb <= kMaxHeapMb, "heap must respect the absolute ceiling");
        Expect(plan->heapMinMb <= plan->heapMaxMb, "Xms must not exceed Xmx");
        // The conservative profile deliberately commits once (Xms==Xmx) to
        // avoid mid-game G1 mmap growth; the heap ceiling itself stays small.
        Expect(plan->fixedHeap ? plan->heapMinMb == plan->heapMaxMb
                               : plan->heapMinMb <= 512, "Xms must stay modest");
        Expect(plan->nativeReserveMb >= kMinNativeReserveMb, "native reserve must be honoured");
        Expect(plan->directMb >= 128 && plan->directMb * 3 <= plan->nativeReserveMb + 1,
               "direct buffers must fit inside the native reserve");
        Expect(plan->heapMaxMb % 32 == 0, "heap should be a whole 32 MiB multiple");
    }
    Expect(conservative.heapMaxMb < balanced.heapMaxMb &&
           balanced.heapMaxMb < high.heapMaxMb, "profiles must be strictly ordered");
    Expect(conservative.fixedHeap && conservative.heapMinMb == conservative.heapMaxMb,
           "conservative must commit the heap once (Xms==Xmx)");
    Expect(!balanced.fixedHeap && !high.fixedHeap, "other profiles keep elastic heaps");
    Expect(ComputePlan(device, ParseProfile("")).heapMaxMb == conservative.heapMaxMb,
           "empty profile string must fall back to conservative");
    Expect(ParseProfile("PERFORMANCE") == Profile::HighPerformance, "performance alias");
    Expect(ParseProfile("equilibrado") == Profile::Balanced, "spanish alias");
    Expect(ParseProfile("garbage") == Profile::Conservative, "unknown profile must be safe");
    Expect(ParseProfile("  BALANCED \r\n") == Profile::Balanced,
           "profile file whitespace/newline must be trimmed");
    Expect(ParseProfile("   ") == Profile::Conservative, "whitespace-only setting is safe");

    // No measurement available: still safe, never a huge heap.
    DeviceMemory unknown;
    const Plan fallback = ComputePlan(unknown, Profile::Balanced);
    Expect(!fallback.measured, "fallback must be marked as unmeasured");
    Expect(fallback.heapMaxMb >= kMinHeapMb && fallback.heapMaxMb < 3584,
           "fallback heap must stay in a safe range");

    // Device scaling: a smaller device must produce a smaller heap.
    DeviceMemory small = device;
    small.budgetMb = 3072;
    Expect(ComputePlan(small, Profile::Balanced).heapMaxMb < balanced.heapMaxMb,
           "plan must adapt to the device, not use a fixed RAM amount");

    const std::vector<std::string> args = BuildJvmMemoryArgs(conservative);
    Expect(Has(args, "-Xmx") && Has(args, "-Xms"), "heap args present");
    Expect(Has(args, "-XX:MaxDirectMemorySize="), "direct memory must stay explicit");
    Expect(Has(args, "-XX:+UseG1GC"), "G1 stays enabled");
    Expect(Has(args, "-XX:+PerfDisableSharedMem"), "console I/O fix preserved");
    for (const char* removed : { "G1HeapRegionSize", "G1NewSizePercent",
                                 "G1ReservePercent", "MaxGCPauseMillis",
                                 "UnlockExperimentalVMOptions" }) {
        Expect(!Has(args, removed), removed);
    }

    // Failure classification: the exact crash text from the report, plus the
    // categories that must never be confused with each other.
    const std::string report =
        "# There is insufficient memory for the Java Runtime Environment to continue.\n"
        "# Native memory allocation (mmap) failed to map 301989888 bytes. "
        "Error detail: G1 virtual space\n";
    Expect(ClassifyFailure(report).category == Failure::Gc, "mmap/G1 virtual space => G1");
    Expect(ClassifyFailure("java.lang.OutOfMemoryError: Java heap space").category == Failure::Heap,
           "Java heap space => heap");
    Expect(ClassifyFailure("OutOfMemoryError: Direct buffer memory").category == Failure::Direct,
           "direct buffer OOM => direct");
    Expect(ClassifyFailure("OutOfMemoryError: Metaspace").category == Failure::Native,
           "metaspace => native");
    Expect(ClassifyFailure("Could not reserve enough space for object heap").category == Failure::VirtualSpace,
           "reserve failure => virtual space");
    Expect(ClassifyFailure("InsufficientMemory, app memory limit 5120 MiB reached").category == Failure::UwpLimit,
           "app limit => UWP limit");
    Expect(ClassifyFailure("java.lang.NoClassDefFoundError: some/mod/Missing").category == Failure::Dependency,
           "missing mod class must NOT be reported as memory");
    Expect(ClassifyFailure("EXCEPTION_ACCESS_VIOLATION").category == Failure::Unknown,
           "unrelated crash stays unknown");

    const std::string diagnostic = DescribeDiagnostics(conservative, device, args);
    Expect(diagnostic.find("5120 MiB") != std::string::npos, "diagnostic includes app limit");
    Expect(diagnostic.find("Xmx") != std::string::npos, "diagnostic includes JVM options");
    Expect(diagnostic.find("reserved for native") != std::string::npos,
           "diagnostic includes the native reserve");

    if (g_failures) {
        std::fprintf(stderr, "%d memory profile test(s) failed\n", g_failures);
        return 1;
    }
    std::printf("memory profile checks passed (profiles, scaling, JVM args, failure classification)\n");
    return 0;
}
