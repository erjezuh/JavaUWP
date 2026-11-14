// Portable tests for launch phase timing / indeterminate progress.
#include "../MC.Xbox/launch/launch_phase.h"

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
    using namespace launchphase;

    Expect(MeasuredProgress(0, 0) == kIndeterminate, "zero total must be indeterminate");
    Expect(MeasuredProgress(0, 100) == 0.0f, "zero done must be 0");
    Expect(MeasuredProgress(50, 100) == 0.5f, "half done must be 0.5");
    Expect(MeasuredProgress(100, 100) == 1.0f, "complete must be 1");
    Expect(MeasuredProgress(500, 100) == 1.0f, "over-complete must be clamped");
    Expect(kIndeterminate < 0.0f, "indeterminate must be a negative sentinel");

    Timeline timeline;
    timeline.Begin(Phase::CheckingFiles, 1000);
    timeline.End(Phase::CheckingFiles, 1120);
    timeline.Begin(Phase::AnalyzingMods, 1120);
    timeline.MarkMeasured(Phase::AnalyzingMods);
    timeline.End(Phase::AnalyzingMods, 1900);
    timeline.Begin(Phase::PreparingDependencies, 1900);
    timeline.End(Phase::PreparingDependencies, 1950);
    timeline.Begin(Phase::PreparingJava, 1950);
    timeline.End(Phase::PreparingJava, 2300);
    timeline.Begin(Phase::StartingLoader, 2300);
    timeline.End(Phase::StartingLoader, 2900);
    timeline.Begin(Phase::LoadingMinecraft, 2900);
    timeline.CloseAll(4200);

    Expect(timeline.PhaseMs(Phase::CheckingFiles) == 120, "checking files duration");
    Expect(timeline.PhaseMs(Phase::AnalyzingMods) == 780, "analyzing mods duration");
    Expect(timeline.PhaseMs(Phase::LoadingMinecraft) == 1300, "loading minecraft duration");
    Expect(timeline.TotalMs() == 120 + 780 + 50 + 350 + 600 + 1300, "total must be the sum");
    Expect(timeline.Records().size() == 6, "six phases recorded");
    Expect(!timeline.Records()[1].indeterminate, "measured phase must be marked");
    Expect(timeline.Records()[0].indeterminate, "unmeasured phase stays indeterminate");

    const std::string log = timeline.FormatLog();
    Expect(log.find("analyzing_mods=780") != std::string::npos, "log includes per-phase ms");
    Expect(log.find("total=3200") != std::string::npos, "log includes total ms");
    Expect(log.find("starting_neoforge=600") != std::string::npos, "log includes loader phase");

    // Wrapped tick counter must not become a huge duration.
    Timeline wrapped;
    wrapped.Begin(Phase::PreparingJava, 0xFFFFFFFFFFFFFF00ull);
    wrapped.End(Phase::PreparingJava, 10);
    Expect(wrapped.TotalMs() == 0, "wrapped counter must not inflate timings");

    // Re-entering a phase closes the previous span instead of leaking it.
    Timeline repeated;
    repeated.Begin(Phase::CheckingFiles, 0);
    repeated.Begin(Phase::CheckingFiles, 50);
    repeated.End(Phase::CheckingFiles, 90);
    Expect(repeated.PhaseMs(Phase::CheckingFiles) == 90, "re-entered phase accounted once");

    if (g_failures) {
        std::fprintf(stderr, "%d launch phase test(s) failed\n", g_failures);
        return 1;
    }
    std::printf("launch phase checks passed (timings, indeterminate progress, wrap, re-entry)\n");
    return 0;
}
