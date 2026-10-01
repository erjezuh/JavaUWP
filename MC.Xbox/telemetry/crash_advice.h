#pragma once

// Human-readable crash diagnosis for the launcher UI.
//
// Turns the raw leftovers of a failed/crashed game session (crash-reports,
// hs_err logs, last_crash.json) into a short Spanish report that names the
// mod that failed and suggests a fix. The user asked for exact mod names and
// potential solutions instead of a wall of frames and fingerprints.

#include <string>

#include "telemetry.h"

namespace crashadvice {

struct ModCrashInfo {
    bool found = false;
    std::wstring culpritName;    // pretty name, e.g. "OptiFine"
    std::wstring culpritFile;    // jar file, e.g. "OptiFine_1.12.2_HD_U_G5.jar"
    std::wstring reason;         // what happened, in plain Spanish
    std::wstring solution;       // what to try, in plain Spanish
    std::wstring incompatible;   // installed mods with known problems
    std::wstring sourceStamp;    // identity of the analyzed crash (dedupe marker)
    std::wstring reportPath;     // written report file (Remote Files readable)
};

// Analyze the leftovers of the last run and write crash-reports\ultimo-informe.txt.
// gameDir is the played profile's game directory; profileId selects the mod list.
// Pass the record returned by telemetry::ReadLastCrash() (read it once: it has
// streak side effects); an empty record is fine when only files remain.
ModCrashInfo AnalyzeLastRun(
    const std::wstring& runtimeRoot,
    const std::wstring& gameDir,
    const std::wstring& profileId,
    const telemetry::CrashRecord& record);

// Installed jars that are on the launcher's known-broken list, one "file - why"
// line each. Used by the crash report and the launch-failure screen.
std::wstring KnownIncompatibleLine(const std::wstring& profileId, const std::wstring& runtimeRoot);

}
