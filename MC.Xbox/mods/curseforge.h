#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "mod_types.h"

namespace curseforge {

struct FileRef {
    long long modId = 0;
    long long fileId = 0;
    std::wstring fileName;
    std::wstring downloadUrl;
    std::string sha1;
    unsigned long long fileSize = 0;
    // author set allowModDistribution false, so the api hands back a null downloadUrl
    bool distributionBlocked = false;
    std::vector<long long> requiredDependencies;
};

bool KeyConfigured();
int LoaderType(const std::wstring& loader);

bool Search(
    const std::wstring& query,
    int offset,
    int limit,
    bool modpacks,
    bool sortByDownloads,
    const std::string& gameVersion,
    const std::wstring& loader,
    std::vector<ModCard>& out,
    int& totalHits,
    std::wstring& error);

bool FetchDetail(
    const std::wstring& modId,
    std::wstring& body,
    std::wstring& meta,
    std::vector<std::pair<unsigned, unsigned>>& bold,
    std::vector<std::pair<unsigned, unsigned>>& head);

bool ResolveLatestFile(
    const std::wstring& modId,
    const std::string& gameVersion,
    const std::wstring& loader,
    FileRef& out,
    std::wstring& error);

// one request for the whole manifest
bool ResolveFilesById(
    const std::vector<long long>& fileIds,
    std::vector<FileRef>& out,
    std::wstring& error);

// website urls for mods the api will not hand over, so the user can fetch them by hand
bool ResolveModWebUrls(
    const std::vector<long long>& modIds,
    std::vector<std::pair<long long, std::wstring>>& out,
    std::wstring& error);

bool DownloadFile(
    const FileRef& file,
    const std::wstring& destination,
    const std::function<void(unsigned long long)>& progress);

std::wstring ProjectWebUrl(const std::wstring& slug);

}
