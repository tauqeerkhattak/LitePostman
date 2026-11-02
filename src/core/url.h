#pragma once

#include <string>
#include <vector>

namespace lite {

struct QueryParam {
    std::string key;
    std::string value;
    bool enabled = true;
};

struct ParsedUrl {
    std::string scheme;   // lowercase, empty for relative URLs
    std::string userinfo; // without '@'
    std::string host;
    std::string port; // original spelling, empty when absent
    std::string path;
    std::string query; // without '?'
    std::string fragment; // without '#'
    bool hasAuthority = false;

    bool secure() const;
    std::string origin() const;
    std::string target() const; // path + query, suitable for an HTTP request
};

bool parseUrl(const std::string& text, ParsedUrl& url, std::string* error = nullptr);
std::string buildUrl(const ParsedUrl& url);
std::string normalizeUrl(const std::string& text);

std::string percentEncode(const std::string& value, bool keepReserved = false);
std::string percentDecode(const std::string& value, bool plusAsSpace = false);
std::string formEncode(const std::string& value);
std::string formDecode(const std::string& value);

std::vector<QueryParam> parseQueryString(const std::string& query);
std::string buildQueryString(const std::vector<QueryParam>& params, bool includeDisabled = false);
std::vector<QueryParam> paramsFromUrl(const std::string& url);
std::string replaceQueryInUrl(const std::string& url, const std::vector<QueryParam>& params);
std::string appendQuery(const std::string& url, const std::string& extraQuery);

// Splits a query body ("a=1&b=2") without touching the URL.
std::string joinUrl(const std::string& base, const std::string& reference);

} // namespace lite
