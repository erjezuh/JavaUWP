#include "crash_advice.h"

#include "crash_fingerprint.h"
#include "crash_parse.h"
#include "launcher_common.h"
#include "mods_browser.h"
#include "profiles.h"
#include "telemetry.h"

#include <algorithm>
#include <vector>

#include <windows.h>

namespace crashadvice {

namespace {

std::wstring LowerW(const std::wstring& s) {
    std::wstring out = s;
    for (wchar_t& c : out) {
        if (c >= L'A' && c <= L'Z') c = c - L'A' + L'a';
    }
    return out;
}

std::string LowerA(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::wstring TrimW(const std::wstring& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && (s[b] == L' ' || s[b] == L'\t' || s[b] == L'\r')) ++b;
    while (e > b && (s[e - 1] == L' ' || s[e - 1] == L'\t' || s[e - 1] == L'\r')) --e;
    return s.substr(b, e - b);
}

// Newest file in a directory matching a simple wildcard like "*.txt".
std::wstring NewestFile(const std::wstring& dir, const wchar_t* pattern) {
    std::wstring newestPath;
    FILETIME newestTime{};
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + L"\\" + pattern).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return newestPath;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (CompareFileTime(&fd.ftLastWriteTime, &newestTime) > 0) {
            newestTime = fd.ftLastWriteTime;
            newestPath = dir + L"\\" + fd.cFileName;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return newestPath;
}

// "create-0.5.1-f.jar" -> "Create"; "OptiFine_1.12.2_HD_U_G5.jar" -> "OptiFine".
std::wstring PrettyModName(const std::wstring& fileName) {
    std::wstring base = fileName;
    const size_t dot = base.rfind(L'.');
    if (dot != std::wstring::npos) base = base.substr(0, dot);

    // Cut at the first version-looking segment (digit after - or _).
    size_t cut = std::wstring::npos;
    for (size_t i = 1; i < base.size(); ++i) {
        if ((base[i] == L'-' || base[i] == L'_') && base[i + 1] >= L'0' && base[i + 1] <= L'9') {
            cut = i;
            break;
        }
    }
    if (cut != std::wstring::npos) base = base.substr(0, cut);
    if (base.empty()) base = fileName;

    for (wchar_t& c : base) {
        if (c == L'-' || c == L'_') c = L' ';
    }
    // Title-case words; keep short all-caps names (JEI, FSE...) readable.
    bool newWord = true;
    bool allLower = true;
    for (const wchar_t c : base) {
        if (c >= L'A' && c <= L'Z') allLower = false;
    }
    for (wchar_t& c : base) {
        if (c == L' ') {
            newWord = true;
        } else if (newWord) {
            if (allLower && c >= L'a' && c <= L'z') c = c - L'a' + L'A';
            newWord = false;
        }
    }
    return base;
}

std::wstring JarFromToken(const std::wstring& token) {
    const size_t slash = token.find_last_of(L"/\\");
    std::wstring name = (slash == std::wstring::npos) ? token : token.substr(slash + 1);
    const std::wstring lower = LowerW(name);
    if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, L".jar") == 0) return name;
    return {};
}

// Collect jar file names mentioned anywhere in the crash text.
std::vector<std::wstring> JarsInText(const std::wstring& text) {
    std::vector<std::wstring> jars;
    size_t pos = 0;
    while ((pos = text.find(L".jar", pos)) != std::wstring::npos) {
        size_t start = pos;
        while (start > 0) {
            const wchar_t c = text[start - 1];
            if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == L'"' || c == L'\'' ||
                c == L'<' || c == L'>' || c == L'(' || c == L')' || c == L'[' || c == L']') {
                break;
            }
            --start;
        }
        const std::wstring jar = JarFromToken(text.substr(start, pos + 4 - start));
        if (!jar.empty()) {
            bool seen = false;
            for (const std::wstring& j : jars) {
                if (LowerW(j) == LowerW(jar)) {
                    seen = true;
                    break;
                }
            }
            if (!seen) jars.push_back(jar);
        }
        pos += 4;
    }
    return jars;
}

// Forge 1.12.2 crash reports carry a "Suspected Mods:" section with real mod
// names and jar files; modern Forge has "Suspected Mod:" / "Mod File:" lines.
std::vector<std::wstring> SuspectedSectionLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    const std::wstring lower = LowerW(text);
    size_t pos = lower.find(L"suspected mod");
    if (pos == std::wstring::npos) pos = lower.find(L"-- mod");
    if (pos == std::wstring::npos) return lines;
    size_t lineEnd = text.find(L'\n', pos);
    size_t guard = 0;
    while (lineEnd != std::wstring::npos && guard++ < 8) {
        std::wstring line = TrimW(text.substr(pos, lineEnd - pos));
        if (!line.empty()) lines.push_back(line);
        pos = lineEnd + 1;
        if (pos >= text.size()) break;
        lineEnd = text.find(L'\n', pos);
        // stop at the next empty line or section header
        if (lineEnd != std::wstring::npos && lineEnd - pos < 2) break;
    }
    return lines;
}

std::wstring FirstLineWith(const std::wstring& text, const std::wstring& needle) {
    const std::wstring lower = LowerW(text);
    const std::wstring lowerNeedle = LowerW(needle);
    size_t pos = 0;
    while ((pos = lower.find(lowerNeedle, pos)) != std::wstring::npos) {
        size_t start = text.rfind(L'\n', pos);
        start = (start == std::wstring::npos) ? 0 : start + 1;
        size_t end = text.find(L'\n', pos);
        if (end == std::wstring::npos) end = text.size();
        return TrimW(text.substr(start, end - start));
    }
    return {};
}

// Match a crash to a jar: prefer jars named in the text, else match the top
// stack packages against installed jar names.
std::wstring AttributeJar(
    const std::wstring& text,
    const std::vector<std::wstring>& frames,
    const std::vector<std::wstring>& installedJars) {
    const std::vector<std::wstring> mentioned = JarsInText(text);
    // Prefer a mentioned jar that is actually installed in this profile.
    for (const std::wstring& jar : mentioned) {
        for (const std::wstring& installed : installedJars) {
            if (LowerW(installed) == LowerW(jar)) return jar;
        }
    }
    // Otherwise any mentioned jar beats no name at all (shared/bundled mods).
    if (!mentioned.empty()) return mentioned.front();

    // Package-token heuristic: "net.optifine.X" / "com.simibubi.create.X" vs
    // installed "OptiFine_...jar" / "create-...jar".
    for (const std::wstring& frame : frames) {
        const std::wstring lowerFrame = LowerW(frame);
        for (const std::wstring& installed : installedJars) {
            std::wstring token = LowerW(installed);
            const size_t cut = token.find_first_of(L"-_.0123456789");
            if (cut != std::wstring::npos && cut >= 3) token = token.substr(0, cut);
            if (token.size() >= 3 && lowerFrame.find(token) != std::wstring::npos) {
                return installed;
            }
        }
    }
    return {};
}

struct Rule {
    const wchar_t* needle;
    const wchar_t* reason;
    const wchar_t* solution;
};

// Plain-Spanish explanations, tried in order against reason+exception text.
const Rule kRules[] = {
    {
        L"outofmemory",
        L"El juego se quedó sin memoria RAM.",
        L"Sube la RAM del perfil (3072 MB recomendado) o quita mods muy pesados."
    },
    {
        L"unsatisfiedlinkerror",
        L"El mod pidió una función del sistema que esta consola no entrega a los mods.",
        L"Si el nombre de la función empieza por gl/egl/glfw es un tema de la capa gráfica: "
        L"prueba sin shaders y con el Anti-Alias de OptiFine apagado. Si no, la capa de "
        L"compatibilidad nueva cubre los componentes habituales: actualiza el launcher y, "
        L"si el mod insiste, quítalo."
    },
    {
        L"mixin",
        L"El mod intenta modificar una parte del juego que no coincide con esta versión.",
        L"Actualiza el mod a la versión exacta para tu Minecraft/Forge, o quítalo. "
        L"Si el mensaje nombra a otro mod, los dos chocan: quita uno de ellos."
    },
    {
        L"injection",
        L"El mod intenta modificar una parte del juego que no coincide con esta versión.",
        L"Actualiza el mod a la versión exacta para tu Minecraft/Forge, o quítalo."
    },
    {
        L"nosuchmethoderror",
        L"El mod no es compatible con esta versión de Minecraft/Forge (le falta una función del juego).",
        L"Instala la versión del mod hecha para esta versión de Minecraft, o quítalo."
    },
    {
        L"nosuchfielderror",
        L"El mod no es compatible con esta versión de Minecraft/Forge (le falta una parte del juego).",
        L"Instala la versión del mod hecha para esta versión de Minecraft, o quítalo."
    },
    {
        L"noclassdeffounderror",
        L"Al mod le falta una clase: normalmente es incompatible o le falta otro mod del que depende.",
        L"Instala la versión correcta del mod para esta versión de Minecraft, o el mod del que depende."
    },
    {
        L"missing dependency",
        L"A un mod le falta otro mod del que depende.",
        L"Instala el mod que falta (el mensaje dice cuál es) y vuelve a jugar."
    },
    {
        L"depends on",
        L"A un mod le falta otro mod del que depende.",
        L"Instala el mod que falta (el mensaje dice cuál es) y vuelve a jugar."
    },
    {
        L"duplicate",
        L"Hay dos copias del mismo mod instaladas.",
        L"Deja solo una copia del archivo repetido en la carpeta de mods."
    },
    {
        L"invalidmodfile",
        L"Un archivo de la carpeta de mods no vale para esta versión de Minecraft.",
        L"Quita el archivo que dice el mensaje: suele ser un mod de otra versión de Minecraft."
    },
    {
        L"stackoverflowerror",
        L"Un mod entró en un bucle infinito y el juego se cayó.",
        L"Quita el último mod que instalaste y prueba otra vez."
    },
    {
        L"glgeterror",
        L"Hubo un error gráfico durante el dibujado del juego.",
        L"Prueba sin shaders y con los ajustes gráficos de OptiFine por defecto."
    },
    {
        L"invalid session",
        L"La sesión de Microsoft se perdió o caducó.",
        L"Cierra el launcher por completo, vuelve a abrirlo y entra otra vez con tu cuenta."
    },
    {
        L"authentication",
        L"Problema con la sesión de Microsoft.",
        L"Cierra el launcher por completo y vuelve a entrar con tu cuenta."
    },
    {
        L"access is denied",
        L"Windows no dejó leer o escribir un archivo del juego.",
        L"Abre Remote Files y comprueba que no tengas el bloqueado el acceso; reinicia la consola si persiste."
    },
};

std::wstring SolutionFor(const std::wstring& probe) {
    const std::wstring lower = LowerW(probe);
    for (const Rule& rule : kRules) {
        if (lower.find(rule.needle) != std::wstring::npos) {
            return rule.solution;
        }
    }
    return L"Mira el bloque de detalles técnicos del informe y prueba a quitar el último mod instalado.";
}

std::wstring ReasonFor(const std::wstring& probe) {
    const std::wstring lower = LowerW(probe);
    for (const Rule& rule : kRules) {
        if (lower.find(rule.needle) != std::wstring::npos) {
            return rule.reason;
        }
    }
    return L"No se pudo identificar la causa automáticamente.";
}

} // namespace

std::wstring KnownIncompatibleLine(const std::wstring& profileId, const std::wstring& runtimeRoot) {
    const std::vector<std::wstring> jars = ListProfileMods(runtimeRoot, profileId);
    const std::vector<std::wstring> notes = BlockedModNotesForJars(jars);
    if (notes.empty()) return {};
    std::wstring line = L"Mods instalados con problemas conocidos en esta consola:";
    for (const std::wstring& note : notes) {
        line += L"\n  - " + note;
    }
    return line;
}

std::wstring LaunchFailureCause(const std::wstring& runtimeRoot) {
    std::wstring text;
    if (!ReadTextFile(runtimeRoot + L"\\logs\\current\\mc_launch.log", text) || text.empty()) {
        return {};
    }

    // Only judge the most recent attempt: anything before the last
    // "Launching embedded JVM" belongs to older runs.
    const size_t attempt = text.rfind(L"Launching embedded JVM");
    if (attempt != std::wstring::npos) text = text.substr(attempt);

    // 1) A Java exception during startup names its stage.
    const std::wstring excLine = FirstLineWith(text, L"Java exception during ");
    if (!excLine.empty()) {
        std::wstring stage = excLine;
        const size_t at = stage.find(L"Java exception during ");
        if (at != std::wstring::npos) stage = TrimW(stage.substr(at + 22));
        std::wstring cause = L"Causa: Java falló al iniciar (" + stage + L").";
        if (LowerW(excLine).find(L"findclass") != std::wstring::npos) {
            cause += L" Solución: no se pudo cargar la clase principal del juego; "
                L"reinstala la build y comprueba que la carpeta de mods no tenga un archivo corrupto.";
        } else {
            cause += L" Solución: cierra el launcher por completo y vuelve a abrirlo; si se repite, reinstala la build.";
        }
        return cause;
    }

    // 2) JNI_CreateJavaVM result code.
    const std::wstring jniLine = FirstLineWith(text, L"JNI_CreateJavaVM => ");
    if (!jniLine.empty()) {
        const size_t at = jniLine.find(L"=> ");
        std::wstring code = (at == std::wstring::npos) ? L"" : TrimW(jniLine.substr(at + 3));
        if (!code.empty() && code != L"0") {
            if (code == L"-1" || code == L"-5") {
                return L"Causa: el motor de Java de esta sesión ya se había usado "
                    L"(solo puede arrancar una vez por cada apertura del launcher). "
                    L"Solución: cierra el launcher COMPLETAMENTE (menú Xbox → cerrar la app) "
                    L"y vuelve a abrirlo antes de intentar otra vez.";
            }
            return L"Causa: el motor de Java no pudo arrancar (código " + code + L"). "
                L"Solución: reinicia la consola y vuelve a intentarlo; si se repite, reinstala la build.";
        }
    }

    // 3) The JVM DLL never loaded.
    const std::wstring loadLine = FirstLineWith(text, L"LoadPackagedLibrary(");
    if (!loadLine.empty() && LowerW(loadLine).find(L"failed") != std::wstring::npos) {
        return L"Causa: falta o está dañada una librería del motor de Java incluida en el launcher. "
            L"Solución: reinstala la build completa (el paquete con el Java 8).";
    }
    if (text.find(L"GetProcAddress(JNI_CreateJavaVM) failed") != std::wstring::npos) {
        return L"Causa: el motor de Java incluido no es válido para esta consola. "
            L"Solución: reinstala la build completa.";
    }

    // 3b) Missing legacy Forge 1.12.2 components in the package.
    if (text.find(L"aborting legacy JVM setup") != std::wstring::npos ||
        text.find(L"missing from package") != std::wstring::npos) {
        return L"Causa: faltan los componentes de compatibilidad de Forge 1.12.2 dentro de esta "
            L"instalación del launcher (build sin soporte 1.12.2 o paquete incompleto). "
            L"Solución: instala una build compilada con soporte 1.12.2; las builds nuevas lo "
            L"incluyen siempre, aunque el destino principal sea 1.20.1.";
    }

    // 3c) Args file locked by an earlier attempt in the same launcher session.
    if (text.find(L"FAILED args file") != std::wstring::npos) {
        return L"Causa: el archivo de argumentos del juego estaba bloqueado por un intento anterior "
            L"de esta misma sesión del launcher. "
            L"Solución: cierra el launcher COMPLETAMENTE (menú Xbox → cerrar la app) y vuelve a abrirlo.";
    }

    // 4) The game started and died instantly.
    if (text.find(L"Embedded JVM failed after startup") != std::wstring::npos) {
        return L"Causa: el juego llegó a arrancar pero se cerró inmediatamente. "
            L"Solución: abre Remote Files → logs → current y mira java_output.log para el error de Java; "
            L"un mod de la carpeta de mods suele ser el culpable.";
    }

    return {};
}

ModCrashInfo AnalyzeLastRun(
    const std::wstring& runtimeRoot,
    const std::wstring& gameDir,
    const std::wstring& profileId,
    const telemetry::CrashRecord& record) {
    ModCrashInfo info;

    // 1) Collect leftovers: newest crash report, newest hs_err. Only real crash
    // artifacts count - a clean session must not produce a report.
    const std::wstring crashDir = gameDir + L"\\crash-reports";
    const std::wstring crashReport = NewestFile(crashDir, L"*.txt");
    const std::wstring hsErr = NewestFile(gameDir, L"hs_err_pid*.log");

    std::wstring reportText;
    if (!crashReport.empty()) ReadTextFile(crashReport, reportText);
    std::wstring hsText;
    if (reportText.empty() && !hsErr.empty()) ReadTextFile(hsErr, hsText);
    const std::wstring text = reportText.empty() ? hsText : reportText;

    if (text.empty() && !record.found) return info;

    // 2) Parse what we can with the existing fingerprint pipeline.
    std::string narrow(text.begin(), text.end());
    crashparse::ParsedCrash parsed = crashparse::ParseMinecraftCrashReport(narrow);
    if (!parsed.valid() && !hsText.empty()) {
        parsed = crashparse::ParseHsErr(std::string(hsText.begin(), hsText.end()));
    }

    std::vector<std::wstring> frames;
    if (record.found) {
        frames = record.frames;
    } else {
        for (const std::string& f : parsed.frames) frames.push_back(a2w(f.c_str()));
    }

    std::wstring probe = text;
    if (record.found) {
        probe += L"\n" + record.exception + L"\n" + record.message;
        for (const std::wstring& f : record.frames) probe += L"\n" + f;
    }

    // 3) Attribute to a mod.
    const std::vector<std::wstring> installed = ListProfileMods(runtimeRoot, profileId);
    std::wstring jar = AttributeJar(text, frames, installed);

    std::wstring name;
    if (record.found && !record.suspectedMod.empty() && jar.empty()) {
        name = record.suspectedMod;
    }
    if (jar.empty() && record.found) {
        jar = AttributeJar(record.message, record.frames, installed);
    }
    if (!jar.empty()) name = PrettyModName(jar);

    // Suspected-section text beats heuristics when it carries a jar.
    const std::vector<std::wstring> suspected = SuspectedSectionLines(text);
    for (const std::wstring& line : suspected) {
        const std::vector<std::wstring> lineJars = JarsInText(line);
        if (!lineJars.empty()) {
            jar = lineJars.front();
            name = PrettyModName(jar);
            break;
        }
    }

    info.found = true;
    info.culpritName = name;
    info.culpritFile = jar;

    // 4) Plain-Spanish reason + solution.
    std::wstring reasonProbe = probe;
    if (record.found) {
        if (!record.detail.kind.empty()) reasonProbe += L"\n" + a2w(record.detail.kind.c_str());
        if (!record.detail.symbol.empty()) reasonProbe += L"\n" + a2w(record.detail.symbol.c_str());
        if (!record.detail.targetClass.empty()) reasonProbe += L"\n" + a2w(record.detail.targetClass.c_str());
    }
    info.reason = ReasonFor(reasonProbe);
    info.solution = SolutionFor(reasonProbe);

    // A dependency problem names the dependency: put it in the solution.
    const std::wstring lowerProbe = LowerW(probe);
    const bool depProblem = lowerProbe.find(L"missing") != std::wstring::npos ||
        lowerProbe.find(L"depends on") != std::wstring::npos ||
        lowerProbe.find(L"mod rejections") != std::wstring::npos;
    if (depProblem) {
        std::wstring depLine = FirstLineWith(text, L"requires");
        if (depLine.empty()) depLine = FirstLineWith(text, L"missing");
        if (!depLine.empty()) {
            info.solution = L"Instala el mod que falta según este mensaje: " + depLine;
        }
    }

    // Graphics-symbol note is more precise than the generic rule.
    if (record.found && !record.detail.symbol.empty()) {
        std::string sym = LowerA(record.detail.symbol);
        const bool graphics = sym.rfind("gl", 0) == 0 ||
            sym.find("egl") != std::string::npos ||
            sym.find("glfw") != std::string::npos ||
            sym.find("opengl") != std::string::npos;
        if (graphics) {
            info.reason = L"El mod pidió funciones gráficas que la capa gráfica de la consola no expone.";
            info.solution = L"Prueba sin shaders y con el Anti-Alias de OptiFine apagado. "
                L"Si el mod sigue fallando, quítalo: usa funciones gráficas que esta consola no tiene.";
        }
    }

    info.incompatible = KnownIncompatibleLine(profileId, runtimeRoot);

    // 5) Identity of this crash for show-once dedupe.
    info.sourceStamp = crashReport.empty() ? hsErr : crashReport;
    info.sourceStamp += L"#" + std::to_wstring(text.size());
    if (record.found) info.sourceStamp += L"#" + record.fingerprint;
    if (!info.culpritFile.empty()) info.sourceStamp += L"#" + info.culpritFile;
    if (!record.message.empty()) {
        info.sourceStamp += L"#" + record.message.substr(0, 48);
    }

    // 6) Write the human report where Remote Files can reach it.
    std::wstring body;
    body += L"INFORME DE CIERRE DEL JUEGO\n";
    body += L"===========================\n\n";
    if (!info.culpritName.empty()) {
        body += L"MOD QUE HA FALLADO: " + info.culpritName;
        if (!info.culpritFile.empty()) body += L"  (" + info.culpritFile + L")";
        body += L"\n\n";
    } else {
        body += L"MOD QUE HA FALLADO: no se pudo identificar con seguridad.\n\n";
    }
    body += L"QUÉ HA PASADO:\n  " + info.reason + L"\n\n";
    body += L"QUÉ PUEDES HACER:\n  " + info.solution + L"\n\n";
    if (!info.incompatible.empty()) {
        body += info.incompatible + L"\n\n";
    }
    body += L"DETALLES TÉCNICOS (solo si los necesitas para pedir ayuda):\n";
    if (record.found) {
        body += L"  " + record.exception + L"\n";
        if (!record.message.empty()) body += L"  " + record.message + L"\n";
        for (size_t i = 0; i < record.frames.size() && i < 6; ++i) {
            body += L"    at " + record.frames[i] + L"\n";
        }
        body += L"  fingerprint " + record.fingerprint + L"\n";
        if (!record.zip.empty()) body += L"  logs: " + record.zip + L"\n";
    } else if (!text.empty()) {
        // First non-empty lines of the raw crash file.
        size_t pos = 0;
        int shown = 0;
        while (shown < 8 && pos < text.size()) {
            size_t end = text.find(L'\n', pos);
            if (end == std::wstring::npos) end = text.size();
            const std::wstring line = TrimW(text.substr(pos, end - pos));
            if (!line.empty()) {
                body += L"  " + line + L"\n";
                ++shown;
            }
            pos = end + 1;
        }
    } else {
        body += L"  (sin restos legibles)\n";
    }

    info.reportPath = CrashReportsDir(runtimeRoot) + L"\\ultimo-informe.txt";
    WriteTextFile(info.reportPath, body);
    WriteLogF(L"crash advice written to %s (culprit=%s)",
        info.reportPath.c_str(), info.culpritName.c_str());

    return info;
}

}
