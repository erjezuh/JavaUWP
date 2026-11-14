// Portable checks for MC.Xbox/launch/jvm_memory.h.
//
// The Xbox build itself needs MSVC, but the memory arithmetic and the crash
// classification are plain C++ and are tested here on any host.

#include "../MC.Xbox/launch/jvm_memory.h"

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

void CheckContains(const std::vector<std::string>& args, const std::string& needle) {
    for (const std::string& arg : args) {
        if (arg == needle) return;
    }
    std::printf("  FAIL missing jvm arg %s\n", needle.c_str());
    ++g_failures;
}

void CheckAbsent(const std::vector<std::string>& args, const std::string& needle) {
    for (const std::string& arg : args) {
        if (arg.find(needle) != std::string::npos) {
            std::printf("  FAIL unexpected jvm arg %s\n", arg.c_str());
            ++g_failures;
            return;
        }
    }
}

const char kFieldHsErr[] =
    "# There is insufficient memory for the Java Runtime Environment to continue.\n"
    "# Native memory allocation (mmap) failed to map 301989888 bytes.\n"
    "# Error detail: G1 virtual space\n"
    "#\n"
    "# Possible reasons:\n"
    "#   The system is out of physical RAM or swap space\n"
    "#\n"
    "# Maximum heap size: 3584M\n"
    "#\n"
    "# Java Threads: 41\n"
    "# Memory: 4k page, physical 10066329k(1189200k free), swap 0k(0k free)\n";

const char kHeapHsErr[] =
    "# java.lang.OutOfMemoryError: Java heap space\n"
    "# -XX:MaxHeapSize=2048m\n";

const char kMetaspaceCrash[] =
    "java.lang.OutOfMemoryError: Metaspace\n"
    "\tat java.lang.ClassLoader.defineClass1(Native Method)\n";

void TestProfileParsing() {
    bool recognised = false;
    Check(jvmmem::ParseProfile(L"conservative", &recognised) == jvmmem::Profile::Conservative,
        "conservative id parses");
    Check(recognised, "conservative id is recognised");
    Check(jvmmem::ParseProfile(L"  Balanced \r\n", &recognised) == jvmmem::Profile::Balanced,
        "balanced id parses with whitespace");
    Check(recognised, "balanced id is recognised");
    Check(jvmmem::ParseProfile(L"High", &recognised) == jvmmem::Profile::Performance,
        "high alias maps to performance");
    Check(recognised, "high alias is recognised");
    Check(jvmmem::ParseProfile(L"nonsense", &recognised) == jvmmem::Profile::Balanced,
        "unknown profile falls back to balanced");
    Check(!recognised, "unknown profile is reported as unrecognised");
    Check(jvmmem::LimitsFor(jvmmem::Profile::Conservative).heapCapMb <
              jvmmem::LimitsFor(jvmmem::Profile::Balanced).heapCapMb,
        "conservative caps the heap below balanced");
    Check(jvmmem::LimitsFor(jvmmem::Profile::Conservative).safetyMarginMb >
              jvmmem::LimitsFor(jvmmem::Profile::Performance).safetyMarginMb,
        "conservative keeps a larger safety margin");
}

void TestConsolePlans() {
    jvmmem::Inputs in;
    in.appLimitMb = 5120;
    in.baselineMb = 350;
    in.physicalMb = 10240;
    in.modJarCount = 140;

    in.profile = jvmmem::Profile::Conservative;
    const jvmmem::Plan conservative = jvmmem::ComputePlan(in);
    CheckEq(conservative.heapMaxMb, 2048, "conservative heap on a 5120 MB budget");
    Check(conservative.fits, "conservative plan fits the budget");
    Check(conservative.headroomMb >= 800, "conservative leaves at least 800 MB headroom");
    CheckEq(conservative.g1ReservePercent, 10, "G1 reserve back to the default");
    CheckEq(conservative.heapMinMb, 512, "initial heap stays small");

    in.profile = jvmmem::Profile::Balanced;
    const jvmmem::Plan balanced = jvmmem::ComputePlan(in);
    CheckEq(balanced.heapMaxMb, 2816, "balanced heap on a 5120 MB budget");
    Check(balanced.fits, "balanced plan fits the budget");
    Check(balanced.headroomMb > 0, "balanced plan has positive headroom");

    in.profile = jvmmem::Profile::Performance;
    const jvmmem::Plan performance = jvmmem::ComputePlan(in);
    Check(performance.heapMaxMb > balanced.heapMaxMb,
        "performance is not smaller than balanced");
    Check(performance.heapMaxMb <= 3072, "performance heap respects its cap");
    Check(performance.fits, "performance plan fits the budget on a Series S budget");

    // Every plan must leave the JVM's own native reservations inside the budget.
    for (const jvmmem::Plan& plan : { conservative, balanced, performance }) {
        Check(plan.expectedPeakMb ==
                  static_cast<unsigned long long>(plan.heapMaxMb) +
                      static_cast<unsigned long long>(plan.jvmNativeMb) +
                      static_cast<unsigned long long>(plan.graphicsReserveMb) +
                      plan.baselineMb,
            "expected peak is heap plus native plus graphics plus baseline");
        Check(plan.heapMaxMb % 64 == 0, "heap is a whole number of 64 MB blocks");
    }
}

void TestTightBudget() {
    jvmmem::Inputs in;
    in.appLimitMb = 4096;
    in.baselineMb = 500;
    in.profile = jvmmem::Profile::Balanced;
    const jvmmem::Plan plan = jvmmem::ComputePlan(in);
    CheckEq(plan.heapMaxMb, 2048, "tight budget falls back to the profile floor");
    Check(plan.clampedToFloor, "tight budget is reported as clamped");
    Check(!plan.fits, "tight budget is reported as not fitting");
    Check(plan.notes.find("tight") != std::string::npos, "tight budget is annotated");
}

void TestUnknownBudget() {
    jvmmem::Inputs in;
    in.appLimitMb = 0;
    in.baselineMb = 0;
    in.profile = jvmmem::Profile::Balanced;
    const jvmmem::Plan plan = jvmmem::ComputePlan(in);
    Check(!plan.budgetKnown, "unknown budget is flagged");
    CheckEq(static_cast<long long>(plan.budgetMb), 5120, "unknown budget falls back to 5120 MB");
    Check(plan.baselineMb >= 128, "unknown baseline gets a safe assumption");
    Check(plan.notes.find("fallback") != std::string::npos, "fallback budget is annotated");

    jvmmem::Inputs small = in;
    small.physicalMb = 6144;
    const jvmmem::Plan smallPlan = jvmmem::ComputePlan(small);
    Check(smallPlan.budgetMb < 5120, "a smaller machine does not get the console budget");
}

void TestArguments() {
    jvmmem::Inputs in;
    in.appLimitMb = 5120;
    in.baselineMb = 350;
    in.profile = jvmmem::Profile::Conservative;
    const jvmmem::Plan plan = jvmmem::ComputePlan(in);
    const std::vector<std::string> args = jvmmem::BuildArgs(plan);

    CheckContains(args, "-Xmx2048M");
    CheckContains(args, "-Xms512M");
    CheckContains(args, "-XX:MaxDirectMemorySize=192M");
    CheckContains(args, "-XX:MaxMetaspaceSize=384M");
    CheckContains(args, "-XX:ReservedCodeCacheSize=160M");
    CheckContains(args, "-XX:CompressedClassSpaceSize=192M");
    CheckContains(args, "-XX:+UseG1GC");
    CheckContains(args, "-XX:G1ReservePercent=10");
    // 32 MB regions turned one region commit into a 288 MiB mmap, which is the
    // exact allocation that failed in the field.
    CheckAbsent(args, "G1HeapRegionSize");
    CheckAbsent(args, "-XX:+AlwaysPreTouch");
    CheckAbsent(args, "Xmx3.");
    Check(!args.empty() && args.front() == "-XX:+IgnoreUnrecognizedVMOptions",
        "unrecognised options are ignored first");
    // Unlock must come before the experimental G1 flags or the JVM refuses them.
    int unlock = -1;
    int g1NewSize = -1;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "-XX:+UnlockExperimentalVMOptions") unlock = static_cast<int>(i);
        if (args[i] == "-XX:G1NewSizePercent=20") g1NewSize = static_cast<int>(i);
    }
    Check(unlock >= 0 && g1NewSize > unlock, "experimental options are unlocked first");
    for (const std::string& arg : args) {
        Check(arg.find("-Xmx3.") == std::string::npos, "no decimal -Xmx is ever produced", arg);
    }

    const std::string summary = jvmmem::PlanSummary(plan);
    Check(summary.find("heap=2048M") != std::string::npos, "summary reports the heap", summary);
    Check(summary.find("profile=conservative") != std::string::npos, "summary reports the profile", summary);
}

void TestFailureClassification() {
    using jvmmem::FailureKind;
    Check(jvmmem::ClassifyHsErr(kFieldHsErr) == FailureKind::G1VirtualSpace,
        "the field hs_err is classified as a G1 virtual space failure");
    Check(jvmmem::IsNativeFailure(jvmmem::ClassifyHsErr(kFieldHsErr)),
        "a G1 virtual space failure is not fixed by a bigger heap");
    Check(jvmmem::ClassifyHsErr(kHeapHsErr) == FailureKind::JavaHeap,
        "a heap OutOfMemoryError is classified as the Java heap");
    Check(!jvmmem::IsNativeFailure(jvmmem::ClassifyHsErr(kHeapHsErr)),
        "the Java heap failure is a heap problem");
    Check(jvmmem::ClassifyHsErr(kMetaspaceCrash) == FailureKind::Metaspace,
        "metaspace exhaustion is recognised");
    Check(jvmmem::ClassifyHsErr("Native memory allocation (mmap) failed to map 65536 bytes.\n"
                                "Error detail: Compressed class space\n") ==
              FailureKind::CompressedClassSpace,
        "compressed class space exhaustion is recognised");
    Check(jvmmem::ClassifyHsErr("Native memory allocation (mmap) failed to map 1024 bytes.\n") ==
              FailureKind::NativeMmap,
        "an unlabelled mmap failure is still a native failure");
    Check(jvmmem::ClassifyHsErr("") == FailureKind::None, "an empty log is not a failure");
    Check(std::string(jvmmem::FailureKey(FailureKind::G1VirtualSpace)) == "g1",
        "failure keys are stable for the UI");
}

} // namespace

int main() {
    std::printf("== JVM memory plan ==\n");
    TestProfileParsing();
    TestConsolePlans();
    TestTightBudget();
    TestUnknownBudget();
    TestArguments();
    TestFailureClassification();

    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("JVM memory plan checks passed\n");
    return 0;
}
