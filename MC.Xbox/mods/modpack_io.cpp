#include "modpack_io.h"

#include "launcher_common.h"
#include "mod_defaults.h"
#include "mods_browser.h"
#include "profiles.h"
#include "runtime_manager.h"
#include "http_client.h"

#include "third_party/miniz/miniz.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <fstream>
#include <vector>

using namespace winrt::Windows::Data::Json;

static std::wstring ModpackDestForRelative(const std::wstring& relRaw, const std::wstring& gameDir, const std::wstring& userModsDir) {
    std::wstring rel = relRaw;
    std::replace(rel.begin(), rel.end(), L'/', L'\\');
    const std::wstring lower = ToLowerW(rel);
    const size_t slash = rel.find_last_of(L'\\');
    const std::wstring base = slash == std::wstring::npos ? rel : rel.substr(slash + 1);
    if (lower.rfind(L"mods\\", 0) == 0) {
        return userModsDir + L"\\" + base;
    }
    return gameDir + L"\\" + rel;
}

static std::wstring ModrinthDependencyLoaderKey(const std::wstring& loader) {
    std::wstring l = loader;
    for (auto& c : l) c = static_cast<wchar_t>(towlower(c));
    if (l == L"fabric") return L"fabric-loader";
    if (l == L"quilt") return L"quilt-loader";
    if (l == L"neoforge") return L"neoforge";
    if (l == L"forge") return L"forge";
    return L"";
}

static bool ReadBinaryFileAll(const std::wstring& path, std::vector<unsigned char>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamoff sz = f.tellg();
    if (sz <= 0) return false;
    out.resize(static_cast<size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(out.data()), sz);
    return f.good() || f.eof();
}

static bool WriteAllBytes(const std::wstring& path, const void* data, size_t size) {
    EnsureDirectoryTree(GetParentDir(path));
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;
    const bool ok = fwrite(data, 1, size, f) == size;
    fclose(f);
    return ok;
}

std::wstring ProfileExportsDir(const std::wstring& runtimeRoot) {
    return runtimeRoot + L"\\exports";
}

std::wstring DefaultProfileExportPath(const std::wstring& runtimeRoot, const std::wstring& profileId) {
    const Profile profile = GetProfileById(runtimeRoot, profileId);
    std::wstring stem = profile.name.empty() ? profileId : profile.name;
    for (wchar_t& ch : stem) {
        if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' || ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|') {
            ch = L'_';
        }
    }
    if (stem.empty()) stem = profileId;
    return ProfileExportsDir(runtimeRoot) + L"\\" + SafeFileName(stem) + L".mrpack";
}

bool InstallModpackFromFile(
    const std::wstring& mrpackPath,
    const std::wstring& runtimeRoot,
    const std::wstring& profileId,
    std::wstring& error) {
    error.clear();
    if (profileId == kVanillaProfileId) {
        error = L"Vanilla is read-only. Pick or create a profile first.";
        return false;
    }
    if (GetFileAttributesW(mrpackPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = L"Pack file not found";
        return false;
    }

    EnsureProfileGameDataInitialized(runtimeRoot, profileId);
    const std::wstring gameDir = ProfileGameDir(runtimeRoot, profileId);
    const std::wstring userModsDir = ProfileModsDir(runtimeRoot, profileId);
    ConfigureKnownModDefaults(gameDir, userModsDir, GetProfileById(runtimeRoot, profileId).minecraftVersion);

    std::vector<unsigned char> packBytes;
    if (!ReadBinaryFileAll(mrpackPath, packBytes)) {
        error = L"Could not read pack file";
        return false;
    }
    if (packBytes.empty()) {
        error = L"Pack file was empty";
        return false;
    }

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, packBytes.data(), packBytes.size(), 0)) {
        error = L"Pack is not a valid .mrpack archive";
        return false;
    }

    std::string indexJson;
    {
        const int idx = mz_zip_reader_locate_file(&zip, "modrinth.index.json", nullptr, 0);
        if (idx < 0) {
            mz_zip_reader_end(&zip);
            error = L"Pack is missing modrinth.index.json";
            return false;
        }
        size_t outSize = 0;
        void* p = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(idx), &outSize, 0);
        if (!p) {
            mz_zip_reader_end(&zip);
            error = L"Could not read pack index";
            return false;
        }
        indexJson.assign(static_cast<const char*>(p), outSize);
        mz_free(p);
    }

    struct PackFile { std::wstring path; std::wstring url; std::string sha1; unsigned long long size = 0; };
    std::vector<PackFile> jobs;
    int skipped = 0;
    try {
        JsonObject root = JsonObject::Parse(winrt::to_hstring(indexJson));
        JsonArray files{ nullptr };
        if (root.HasKey(L"files") && root.GetNamedValue(L"files").ValueType() == JsonValueType::Array) {
            files = root.GetNamedArray(L"files");
        }
        const uint32_t fcount = files ? files.Size() : 0;
        for (uint32_t i = 0; i < fcount; ++i) {
            auto v = files.GetAt(i);
            if (v.ValueType() != JsonValueType::Object) continue;
            JsonObject fo = v.GetObject();
            if (fo.HasKey(L"env") && fo.GetNamedValue(L"env").ValueType() == JsonValueType::Object) {
                JsonObject env = fo.GetNamedObject(L"env");
                if (env.HasKey(L"client") && env.Lookup(L"client").ValueType() == JsonValueType::String &&
                    env.Lookup(L"client").GetString() == L"unsupported") {
                    continue;
                }
            }
            const std::wstring path = fo.Lookup(L"path").GetString().c_str();
            if (path.empty()) continue;
            std::wstring durl;
            if (fo.HasKey(L"downloads") && fo.GetNamedValue(L"downloads").ValueType() == JsonValueType::Array) {
                JsonArray dls = fo.GetNamedArray(L"downloads");
                if (dls.Size() > 0 && dls.GetAt(0).ValueType() == JsonValueType::String) {
                    durl = dls.GetAt(0).GetString().c_str();
                }
            }
            std::string sha1;
            if (fo.HasKey(L"hashes") && fo.GetNamedValue(L"hashes").ValueType() == JsonValueType::Object) {
                sha1 = w2a(fo.GetNamedObject(L"hashes").Lookup(L"sha1").GetString().c_str());
            }
            unsigned long long fsize = 0;
            if (fo.HasKey(L"fileSize") && fo.GetNamedValue(L"fileSize").ValueType() == JsonValueType::Number) {
                fsize = static_cast<unsigned long long>(fo.GetNamedNumber(L"fileSize"));
            }
            const size_t slash = path.find_last_of(L"/\\");
            const std::wstring base = slash == std::wstring::npos ? path : path.substr(slash + 1);
            if (IsBlockedModFileName(base)) {
                WriteLogF(L"Skipping blocked modpack file: %s", base.c_str());
                ++skipped;
                continue;
            }
            jobs.push_back({ path, durl, sha1, fsize });
        }
    } catch (const winrt::hresult_error&) {
        mz_zip_reader_end(&zip);
        error = L"Could not parse pack index";
        return false;
    }

    std::wstring firstError;
    int done = 0;
    for (const PackFile& job : jobs) {
        const std::wstring dest = ModpackDestForRelative(job.path, gameDir, userModsDir);
        const size_t bslash = job.path.find_last_of(L"/\\");
        const std::wstring base = bslash == std::wstring::npos ? job.path : job.path.substr(bslash + 1);
        WriteLogF(L"Modpack install file %d/%zu: %s", done + 1, jobs.size(), base.c_str());
        if (!job.sha1.empty() && FileMatchesSha1(dest, job.sha1)) {
            ++done;
            continue;
        }
        EnsureDirectoryTree(GetParentDir(dest));
        const std::wstring tmp = dest + L".download";
        DeleteFileW(tmp.c_str());
        bool fileOk = false;
        if (!job.url.empty()) {
            fileOk = DownloadUrlToFile(job.url, tmp, nullptr) &&
                (job.sha1.empty() || FileMatchesSha1(tmp, job.sha1));
        } else {
            std::string relA = w2a(job.path);
            std::replace(relA.begin(), relA.end(), '\\', '/');
            const int embedded = mz_zip_reader_locate_file(&zip, relA.c_str(), nullptr, 0);
            if (embedded >= 0) {
                size_t outSize = 0;
                void* p = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(embedded), &outSize, 0);
                if (p && outSize > 0) {
                    fileOk = WriteAllBytes(tmp, p, outSize);
                    if (fileOk && !job.sha1.empty()) {
                        fileOk = FileMatchesSha1(tmp, job.sha1);
                    }
                    mz_free(p);
                }
            }
        }
        if (fileOk) {
            DeleteFileW(dest.c_str());
            if (!MoveFileExW(tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                DeleteFileW(tmp.c_str());
                if (firstError.empty()) firstError = L"Some pack files could not be saved";
            }
        } else {
            DeleteFileW(tmp.c_str());
            if (firstError.empty()) firstError = L"Some pack files failed to install";
        }
        ++done;
    }

    WriteLog(L"Applying modpack overrides...");
    const mz_uint entryCount = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < entryCount; ++i) {
        if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        std::string name = st.m_filename;
        std::string prefix;
        if (name.rfind("overrides/", 0) == 0) prefix = "overrides/";
        else if (name.rfind("client-overrides/", 0) == 0) prefix = "client-overrides/";
        else continue;
        const std::string relA = name.substr(prefix.size());
        if (relA.empty()) continue;
        const std::wstring rel = a2w(relA.c_str());
        const std::wstring dest = ModpackDestForRelative(rel, gameDir, userModsDir);
        const size_t slash = dest.find_last_of(L'\\');
        const std::wstring base = slash == std::wstring::npos ? dest : dest.substr(slash + 1);
        if (ToLowerW(dest).find(ToLowerW(userModsDir)) == 0 && IsBlockedModFileName(base)) {
            WriteLogF(L"Skipping blocked modpack override: %s", base.c_str());
            ++skipped;
            continue;
        }
        size_t outSize = 0;
        void* p = mz_zip_reader_extract_to_heap(&zip, i, &outSize, 0);
        if (!p) continue;
        WriteAllBytes(dest, p, outSize);
        mz_free(p);
    }

    mz_zip_reader_end(&zip);
    PurgeBlockedModsFromDir(runtimeRoot, userModsDir);

    WriteLogF(L"Modpack import done: %d indexed files, %d blocked", done, skipped);
    if (jobs.empty() && firstError.empty()) {
        error = L"Pack had no installable client files";
        return false;
    }
    if (!firstError.empty()) {
        error = firstError;
        return false;
    }
    return true;
}

static std::wstring MpJsonString(const winrt::Windows::Data::Json::JsonObject& obj, const wchar_t* key) {
    using namespace winrt::Windows::Data::Json;
    if (obj.HasKey(key) && obj.GetNamedValue(key).ValueType() == JsonValueType::String) {
        return std::wstring(obj.GetNamedString(key).c_str());
    }
    return std::wstring();
}

static int MpJsonInt(const winrt::Windows::Data::Json::JsonObject& obj, const wchar_t* key) {
    using namespace winrt::Windows::Data::Json;
    if (obj.HasKey(key) && obj.GetNamedValue(key).ValueType() == JsonValueType::Number) {
        return static_cast<int>(obj.GetNamedNumber(key));
    }
    return 0;
}

// CurseForge modpack zip: manifest.json + overrides/. Each files[] entry is a
// (projectID, fileID) pair resolved through the CurseForge Core API.
bool InstallCurseForgeModpackFromFile(
    const std::wstring& packZipPath,
    const std::wstring& runtimeRoot,
    const std::wstring& profileId,
    const std::wstring& curseForgeApiKey,
    std::wstring& error) {
    using namespace winrt::Windows::Data::Json;
    error.clear();
    if (profileId == kVanillaProfileId) {
        error = L"Vanilla is read-only. Pick or create a profile first.";
        return false;
    }
    if (curseForgeApiKey.empty()) {
        error = L"CurseForge API key missing (curseforge_api_key.txt)";
        return false;
    }
    if (GetFileAttributesW(packZipPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = L"Pack file not found";
        return false;
    }

    EnsureProfileGameDataInitialized(runtimeRoot, profileId);
    const Profile profile = GetProfileById(runtimeRoot, profileId);
    const std::wstring gameDir = ProfileGameDir(runtimeRoot, profileId);
    const std::wstring userModsDir = ProfileModsDir(runtimeRoot, profileId);
    ConfigureKnownModDefaults(gameDir, userModsDir, profile.minecraftVersion);

    std::vector<unsigned char> packBytes;
    if (!ReadBinaryFileAll(packZipPath, packBytes) || packBytes.empty()) {
        error = L"Could not read pack file";
        return false;
    }

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, packBytes.data(), packBytes.size(), 0)) {
        error = L"Pack is not a valid CurseForge modpack archive";
        return false;
    }

    std::string manifestJson;
    {
        const int idx = mz_zip_reader_locate_file(&zip, "manifest.json", nullptr, 0);
        if (idx < 0) {
            mz_zip_reader_end(&zip);
            error = L"Pack is missing manifest.json";
            return false;
        }
        size_t outSize = 0;
        void* p = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(idx), &outSize, 0);
        if (!p) {
            mz_zip_reader_end(&zip);
            error = L"Could not read pack manifest";
            return false;
        }
        manifestJson.assign(static_cast<const char*>(p), outSize);
        mz_free(p);
    }

    std::wstring overridesFolder = L"overrides";
    struct PackRef { int projectId = 0; int fileId = 0; };
    std::vector<PackRef> refs;
    int optionalSkipped = 0;
    try {
        JsonObject root = JsonObject::Parse(winrt::to_hstring(manifestJson));
        std::wstring packMinecraft;
        std::wstring packLoaderFamily;
        if (root.HasKey(L"minecraft") && root.GetNamedValue(L"minecraft").ValueType() == JsonValueType::Object) {
            JsonObject mc = root.GetNamedObject(L"minecraft");
            packMinecraft = MpJsonString(mc, L"version");
            if (mc.HasKey(L"modLoaders") && mc.GetNamedValue(L"modLoaders").ValueType() == JsonValueType::Array) {
                JsonArray loaders = mc.GetNamedArray(L"modLoaders");
                std::wstring primaryId;
                std::wstring anyId;
                for (uint32_t i = 0; i < loaders.Size(); ++i) {
                    auto v = loaders.GetAt(i);
                    if (v.ValueType() != JsonValueType::Object) continue;
                    JsonObject lo = v.GetObject();
                    const std::wstring id = MpJsonString(lo, L"id");  // e.g. forge-47.4.20
                    if (id.empty()) continue;
                    if (anyId.empty()) anyId = id;
                    if (lo.HasKey(L"primary") && lo.GetNamedValue(L"primary").ValueType() == JsonValueType::Boolean &&
                        lo.GetNamedBoolean(L"primary")) {
                        primaryId = id;
                    }
                }
                const std::wstring loaderId = !primaryId.empty() ? primaryId : anyId;
                const size_t dash = loaderId.find(L'-');
                packLoaderFamily = dash == std::wstring::npos ? loaderId : loaderId.substr(0, dash);
                for (wchar_t& c : packLoaderFamily) c = static_cast<wchar_t>(towlower(c));
            }
        }

        // The profile (created from the browse target) must match the pack's
        // Minecraft line, otherwise the launch would crash with the wrong game.
        if (!packMinecraft.empty() && packMinecraft != profile.minecraftVersion) {
            error = L"Pack is for Minecraft " + packMinecraft +
                L" but the selected target is " + profile.minecraftVersion +
                L". Set Target to a " + packMinecraft + L" profile and install again.";
            return false;
        }
        std::wstring profileLoader = profile.loader;
        for (wchar_t& c : profileLoader) c = static_cast<wchar_t>(towlower(c));
        if (!packLoaderFamily.empty() && packLoaderFamily != profileLoader) {
            error = L"Pack uses " + packLoaderFamily + L" but the selected target is " +
                profileLoader + L". Set Target to a " + packLoaderFamily + L" profile and install again.";
            return false;
        }

        const std::wstring ov = MpJsonString(root, L"overrides");
        if (!ov.empty()) {
            overridesFolder = ov;
            while (!overridesFolder.empty() && (overridesFolder.back() == L'/' || overridesFolder.back() == L'\\')) {
                overridesFolder.pop_back();
            }
            if (overridesFolder.empty()) overridesFolder = L"overrides";
        }

        if (root.HasKey(L"files") && root.GetNamedValue(L"files").ValueType() == JsonValueType::Array) {
            JsonArray files = root.GetNamedArray(L"files");
            for (uint32_t i = 0; i < files.Size(); ++i) {
                auto v = files.GetAt(i);
                if (v.ValueType() != JsonValueType::Object) continue;
                JsonObject fo = v.GetObject();
                if (fo.HasKey(L"required") && fo.GetNamedValue(L"required").ValueType() == JsonValueType::Boolean &&
                    !fo.GetNamedBoolean(L"required")) {
                    ++optionalSkipped;
                    continue;
                }
                PackRef ref;
                ref.projectId = MpJsonInt(fo, L"projectID");
                ref.fileId = MpJsonInt(fo, L"fileID");
                if (ref.projectId <= 0 || ref.fileId <= 0) continue;
                refs.push_back(ref);
            }
        }
    } catch (const winrt::hresult_error&) {
        error = L"Could not parse pack manifest";
        return false;
    }

    if (refs.empty()) {
        error = L"Pack had no required mods to install";
        return false;
    }

    std::wstring firstError;
    int done = 0;
    int blocked = 0;
    const int total = static_cast<int>(refs.size());
    for (const PackRef& ref : refs) {
        const std::wstring modId = std::to_wstring(ref.projectId);
        const std::wstring fileId = std::to_wstring(ref.fileId);
        const std::wstring label = L"Mod " + std::to_wstring(done + 1) + L"/" + std::to_wstring(total);
        SetInstallStatus(label + L"...");

        std::wstring fileName;
        std::wstring downloadUrl;
        std::string sha1;
        try {
            const std::wstring resolveUrl =
                L"https://api.curseforge.com/v1/mods/" + modId + L"/files/" + fileId;
            const HttpResult response =
                HttpGetStringHeader(resolveUrl.c_str(), L"x-api-key", curseForgeApiKey);
            if (!response.success()) {
                if (firstError.empty()) firstError = L"File lookup failed for project " + modId;
                ++done;
                continue;
            }
            JsonObject root = JsonObject::Parse(winrt::to_hstring(response.body));
            if (root.HasKey(L"data") && root.GetNamedValue(L"data").ValueType() == JsonValueType::Object) {
                JsonObject data = root.GetNamedObject(L"data");
                fileName = MpJsonString(data, L"fileName");
                downloadUrl = MpJsonString(data, L"downloadUrl");
                if (data.HasKey(L"hashes") && data.GetNamedValue(L"hashes").ValueType() == JsonValueType::Array) {
                    JsonArray hashes = data.GetNamedArray(L"hashes");
                    for (uint32_t h = 0; h < hashes.Size(); ++h) {
                        auto hv = hashes.GetAt(h);
                        if (hv.ValueType() != JsonValueType::Object) continue;
                        JsonObject hash = hv.GetObject();
                        if (MpJsonInt(hash, L"algo") == 1) sha1 = w2a(MpJsonString(hash, L"value").c_str());
                    }
                }
            }
        } catch (const winrt::hresult_error&) {
        }

        if (downloadUrl.empty()) {
            const std::wstring fallbackUrl =
                L"https://api.curseforge.com/v1/mods/" + modId + L"/files/" + fileId + L"/download-url";
            const HttpResult resolve =
                HttpGetStringHeader(fallbackUrl.c_str(), L"x-api-key", curseForgeApiKey);
            if (resolve.success()) {
                try {
                    JsonObject root = JsonObject::Parse(winrt::to_hstring(resolve.body));
                    downloadUrl = MpJsonString(root, L"data");
                } catch (...) {
                }
            }
        }
        if (fileName.empty()) fileName = L"mod-" + modId + L"-" + fileId + L".jar";
        if (downloadUrl.empty()) {
            WriteLogF(L"CurseForge pack file without download url: %s", fileName.c_str());
            if (firstError.empty()) firstError = L"Some pack files failed to install";
            ++done;
            continue;
        }

        const std::wstring dest = ModpackDestForRelative(L"mods\\" + fileName, gameDir, userModsDir);
        const size_t bslash = dest.find_last_of(L'\\');
        const std::wstring base = bslash == std::wstring::npos ? dest : dest.substr(bslash + 1);
        if (IsBlockedModFileName(base)) {
            WriteLogF(L"Skipping blocked modpack file: %s", base.c_str());
            ++blocked;
            ++done;
            continue;
        }
        if (!sha1.empty() && FileMatchesSha1(dest, sha1)) {
            WriteLogF(L"Modpack file already installed: %s", base.c_str());
            ++done;
            continue;
        }

        EnsureDirectoryTree(GetParentDir(dest));
        const std::wstring tmp = dest + L".download";
        DeleteFileW(tmp.c_str());
        SetInstallStatus(label + L": " + base);
        bool fileOk = DownloadUrlToFile(downloadUrl, tmp,
            MakeInstallProgress(label + L": " + base, 0)) &&
            (sha1.empty() || FileMatchesSha1(tmp, sha1));
        if (fileOk) {
            DeleteFileW(dest.c_str());
            if (!MoveFileExW(tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                DeleteFileW(tmp.c_str());
                fileOk = false;
            }
        } else {
            DeleteFileW(tmp.c_str());
        }
        if (!fileOk && firstError.empty()) firstError = L"Some pack files failed to install";
        WriteLogF(L"Modpack file %d/%d %s: %s", done + 1, total, fileOk ? L"ok" : L"failed", base.c_str());
        ++done;
    }

    WriteLog(L"Applying CurseForge modpack overrides...");
    const std::string prefixA = w2a(overridesFolder.c_str()) + "/";
    const mz_uint entryCount = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < entryCount; ++i) {
        if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        std::string name = st.m_filename;
        std::string prefix;
        if (name.rfind(prefixA, 0) == 0) prefix = prefixA;
        else if (name.rfind("overrides/", 0) == 0) prefix = "overrides/";
        else if (name.rfind("client-overrides/", 0) == 0) prefix = "client-overrides/";
        else continue;
        const std::string relA = name.substr(prefix.size());
        if (relA.empty()) continue;
        const std::wstring rel = a2w(relA.c_str());
        const std::wstring dest = ModpackDestForRelative(rel, gameDir, userModsDir);
        const size_t slash = dest.find_last_of(L'\\');
        const std::wstring base = slash == std::wstring::npos ? dest : dest.substr(slash + 1);
        if (ToLowerW(dest).find(ToLowerW(userModsDir)) == 0 && IsBlockedModFileName(base)) {
            WriteLogF(L"Skipping blocked modpack override: %s", base.c_str());
            ++blocked;
            continue;
        }
        size_t outSize = 0;
        void* p = mz_zip_reader_extract_to_heap(&zip, i, &outSize, 0);
        if (!p) continue;
        WriteAllBytes(dest, p, outSize);
        mz_free(p);
    }

    mz_zip_reader_end(&zip);
    PurgeBlockedModsFromDir(runtimeRoot, userModsDir);

    WriteLogF(L"CurseForge modpack import done: %d files, %d optional skipped, %d blocked",
        done, optionalSkipped, blocked);
    if (!firstError.empty()) {
        error = firstError;
        return false;
    }
    return true;
}

static bool ZipAddBytes(mz_zip_archive* zip, const std::string& archiveName, const std::vector<unsigned char>& bytes) {
    if (bytes.empty()) return false;
    return mz_zip_writer_add_mem(zip, archiveName.c_str(), bytes.data(), bytes.size(), MZ_DEFAULT_COMPRESSION) != 0;
}

static bool ZipAddFileFromPath(mz_zip_archive* zip, const std::string& archiveName, const std::wstring& path) {
    std::vector<unsigned char> bytes;
    if (!ReadBinaryFileAll(path, bytes)) return false;
    return ZipAddBytes(zip, archiveName, bytes);
}

struct MrpackIndexedFile {
    std::wstring path;
    std::wstring downloadUrl;
    std::string sha1;
    std::string sha512;
    unsigned long long fileSize = 0;
};

static std::wstring JsonStringOrEmpty(const JsonObject& obj, const wchar_t* key) {
    if (!obj.HasKey(key) || obj.Lookup(key).ValueType() != JsonValueType::String) return L"";
    return obj.Lookup(key).GetString().c_str();
}

static bool JsonBoolOrFalse(const JsonObject& obj, const wchar_t* key) {
    if (!obj.HasKey(key) || obj.Lookup(key).ValueType() != JsonValueType::Boolean) return false;
    return obj.Lookup(key).GetBoolean();
}

static bool TryResolveModrinthIndexedFile(
    const std::wstring& localPath,
    const wchar_t* pathPrefix,
    MrpackIndexedFile& out) {
    out = {};
    std::string sha1;
    if (!Sha1File(localPath, &sha1) || sha1.empty()) return false;

    const std::wstring url =
        L"https://api.modrinth.com/v2/version_file/" + a2w(sha1.c_str()) + L"?algorithm=sha1";
    const HttpResult response = HttpGetString(url.c_str());
    if (!response.success()) return false;

    try {
        JsonObject version = JsonObject::Parse(winrt::to_hstring(response.body));
        if (!version.HasKey(L"files") || version.GetNamedValue(L"files").ValueType() != JsonValueType::Array) {
            return false;
        }
        JsonArray files = version.GetNamedArray(L"files");
        JsonObject selected = nullptr;
        for (uint32_t i = 0; i < files.Size(); ++i) {
            auto value = files.GetAt(i);
            if (value.ValueType() != JsonValueType::Object) continue;
            JsonObject file = value.GetObject();
            if (!selected || JsonBoolOrFalse(file, L"primary")) {
                selected = file;
                if (JsonBoolOrFalse(file, L"primary")) break;
            }
        }
        if (!selected) return false;

        const std::wstring downloadUrl = JsonStringOrEmpty(selected, L"url");
        const std::wstring filename = JsonStringOrEmpty(selected, L"filename");
        if (downloadUrl.empty() || filename.empty()) return false;

        std::string resolvedSha1 = sha1;
        std::string sha512;
        if (selected.HasKey(L"hashes") && selected.GetNamedValue(L"hashes").ValueType() == JsonValueType::Object) {
            JsonObject hashes = selected.GetNamedObject(L"hashes");
            const std::wstring sha1Text = JsonStringOrEmpty(hashes, L"sha1");
            const std::wstring sha512Text = JsonStringOrEmpty(hashes, L"sha512");
            if (!sha1Text.empty()) resolvedSha1 = w2a(sha1Text.c_str());
            if (!sha512Text.empty()) sha512 = w2a(sha512Text.c_str());
        }
        if (sha512.empty()) return false;

        unsigned long long fileSize = 0;
        if (selected.HasKey(L"size") && selected.GetNamedValue(L"size").ValueType() == JsonValueType::Number) {
            fileSize = static_cast<unsigned long long>(selected.GetNamedNumber(L"size"));
        }
        if (fileSize == 0) {
            WIN32_FILE_ATTRIBUTE_DATA fad = {};
            if (GetFileAttributesExW(localPath.c_str(), GetFileExInfoStandard, &fad)) {
                fileSize = (static_cast<unsigned long long>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
            }
        }

        out.path = std::wstring(pathPrefix) + filename;
        out.downloadUrl = downloadUrl;
        out.sha1 = resolvedSha1;
        out.sha512 = sha512;
        out.fileSize = fileSize;
        return true;
    } catch (const winrt::hresult_error&) {
        return false;
    }
}

static JsonObject MakeIndexedFileObject(const MrpackIndexedFile& file) {
    JsonObject fileObj;
    fileObj.SetNamedValue(L"path", JsonValue::CreateStringValue(file.path));
    JsonObject hashes;
    hashes.SetNamedValue(L"sha1", JsonValue::CreateStringValue(a2w(file.sha1.c_str())));
    hashes.SetNamedValue(L"sha512", JsonValue::CreateStringValue(a2w(file.sha512.c_str())));
    fileObj.SetNamedValue(L"hashes", hashes);
    fileObj.SetNamedValue(L"fileSize", JsonValue::CreateNumberValue(static_cast<double>(file.fileSize)));
    JsonArray downloads;
    downloads.Append(JsonValue::CreateStringValue(file.downloadUrl));
    fileObj.SetNamedValue(L"downloads", downloads);
    return fileObj;
}

static bool ConfigDirHasFiles(const std::wstring& configDir) {
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((configDir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool any = false;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            any = true;
            break;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return any;
}

static void AddConfigOverridesToZip(mz_zip_archive* zip, const std::wstring& configDir, const char* prefix) {
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((configDir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        const std::wstring full = configDir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const std::string archiveName = std::string(prefix) + w2a(fd.cFileName);
        ZipAddFileFromPath(zip, archiveName, full);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

bool ExportProfileMrpack(
    const std::wstring& runtimeRoot,
    const std::wstring& profileId,
    const std::wstring& outputPath,
    std::wstring& error) {
    error.clear();
    if (profileId == kVanillaProfileId) {
        error = L"Vanilla cannot be exported as a modpack";
        return false;
    }

    const Profile profile = GetProfileById(runtimeRoot, profileId);
    const LaunchTarget target = ResolveProfileTarget(runtimeRoot, profile);
    const std::wstring gameDir = ProfileGameDir(runtimeRoot, profileId);
    const std::wstring userModsDir = ProfileModsDir(runtimeRoot, profileId);
    const std::vector<std::wstring> mods = ListProfileMods(runtimeRoot, profileId);

    JsonObject root;
    root.SetNamedValue(L"formatVersion", JsonValue::CreateNumberValue(1));
    root.SetNamedValue(L"game", JsonValue::CreateStringValue(L"minecraft"));
    root.SetNamedValue(L"versionId", JsonValue::CreateStringValue(target.minecraftVersion));
    root.SetNamedValue(L"name", JsonValue::CreateStringValue(profile.name));
    root.SetNamedValue(L"summary", JsonValue::CreateStringValue(
        L"Exported from Bandit Launcher profile " + profile.name + L" (" + TargetProfileText(target) + L")"));

    JsonArray files = JsonArray();
    std::vector<std::pair<std::string, std::wstring>> overrideFiles;
    int indexedMods = 0;
    int overrideMods = 0;

    for (const std::wstring& jar : mods) {
        if (IsBlockedModFileName(jar)) continue;
        const std::wstring jarPath = userModsDir + L"\\" + jar;
        if (GetFileAttributesW(jarPath.c_str()) == INVALID_FILE_ATTRIBUTES) continue;

        MrpackIndexedFile indexed;
        if (TryResolveModrinthIndexedFile(jarPath, L"mods/", indexed)) {
            files.Append(MakeIndexedFileObject(indexed));
            ++indexedMods;
        } else {
            overrideFiles.push_back({ "overrides/mods/" + w2a(jar), jarPath });
            ++overrideMods;
            WriteLogF(L"Export override mod (not on Modrinth CDN): %s", jar.c_str());
        }
    }

    const std::wstring resourceDir = gameDir + L"\\resourcepacks";
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((resourceDir + L"\\*.zip").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const std::wstring packPath = resourceDir + L"\\" + fd.cFileName;
            MrpackIndexedFile indexed;
            if (TryResolveModrinthIndexedFile(packPath, L"resourcepacks/", indexed)) {
                files.Append(MakeIndexedFileObject(indexed));
            } else {
                overrideFiles.push_back({ "overrides/resourcepacks/" + w2a(fd.cFileName), packPath });
                WriteLogF(L"Export override resource pack: %s", fd.cFileName);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    const bool hasConfigOverrides = ConfigDirHasFiles(gameDir + L"\\config");
    if (files.Size() == 0 && overrideFiles.empty() && !hasConfigOverrides) {
        error = L"This profile has no mods or resource packs to export";
        return false;
    }
    root.SetNamedValue(L"files", files);

    JsonObject deps;
    deps.SetNamedValue(L"minecraft", JsonValue::CreateStringValue(target.minecraftVersion));
    const std::wstring loaderKey = ModrinthDependencyLoaderKey(target.loader);
    if (!loaderKey.empty() && !target.loaderVersion.empty()) {
        deps.SetNamedValue(loaderKey, JsonValue::CreateStringValue(target.loaderVersion));
    }
    root.SetNamedValue(L"dependencies", deps);

    const std::wstring indexText = std::wstring(root.Stringify().c_str());
    const std::string indexUtf8 = w2a(indexText.c_str());

    DeleteFileW(outputPath.c_str());
    EnsureDirectoryTree(GetParentDir(outputPath));

    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 1024 * 1024)) {
        error = L"Could not create export archive";
        return false;
    }

    if (!mz_zip_writer_add_mem(&zip, "modrinth.index.json", indexUtf8.data(), indexUtf8.size(), MZ_DEFAULT_COMPRESSION)) {
        mz_zip_writer_end(&zip);
        error = L"Could not write pack index";
        return false;
    }

    for (const auto& overrideFile : overrideFiles) {
        if (!ZipAddFileFromPath(&zip, overrideFile.first, overrideFile.second)) {
            WriteLogF(L"Export skipped override file: %s", a2w(overrideFile.first.c_str()).c_str());
        }
    }

    AddConfigOverridesToZip(&zip, gameDir + L"\\config", "overrides/config/");

    void* heapBuf = nullptr;
    size_t heapSize = 0;
    if (!mz_zip_writer_finalize_heap_archive(&zip, &heapBuf, &heapSize) || !heapBuf || heapSize == 0) {
        mz_zip_writer_end(&zip);
        error = L"Could not finalize export archive";
        return false;
    }
    const bool wrote = WriteAllBytes(outputPath, heapBuf, heapSize);
    mz_free(heapBuf);
    mz_zip_writer_end(&zip);
    if (!wrote) {
        DeleteFileW(outputPath.c_str());
        error = L"Could not write export archive";
        return false;
    }

    WriteLogF(L"Exported profile %s as mrpack: %s indexed=%d override=%d",
        profileId.c_str(), outputPath.c_str(), indexedMods, overrideMods);
    return true;
}
