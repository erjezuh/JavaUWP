#pragma once

#include <string>
#include <utility>
#include <vector>

struct HttpResult {
    int status = 0;
    std::string body;

    bool success() const {
        return status >= 200 && status < 300;
    }
};

std::string ExtractJsonStringValue(const std::string& content, const char* key);
int ExtractJsonIntValue(const std::string& content, const char* key, int fallback = 0);
std::string JsonEscape(const std::string& value);
std::string FormUrlEncode(const std::string& value);
std::string MakeFormBody(std::initializer_list<std::pair<std::string, std::string>> fields);
std::string NormalizeMinecraftUuid(const std::string& value);

HttpResult HttpPostString(const wchar_t* url, const std::string& body, const wchar_t* mediaType);

// the timeout covers the response and its body
HttpResult HttpPostStringTimed(
    const wchar_t* url,
    const std::string& body,
    const wchar_t* mediaType,
    unsigned timeoutMs);

HttpResult HttpGetBearer(const wchar_t* url, const std::string& token);
HttpResult HttpGetString(const wchar_t* url);

// GET with one custom header (CurseForge Core API needs x-api-key)
HttpResult HttpGetStringHeader(
    const wchar_t* url,
    const wchar_t* headerName,
    const std::wstring& headerValue);

// returns the response etag and an empty body for status 304
HttpResult HttpGetConditionalTimed(
    const wchar_t* url,
    const std::string& etag,
    unsigned timeoutMs,
    std::string& outEtag);
