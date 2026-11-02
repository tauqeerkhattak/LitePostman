#include "core/url.h"

#include <algorithm>
#include <cctype>

namespace lite {
namespace {

bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isSchemeChar(char c) {
    return isAlpha(c) || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
}

bool isUnreserved(unsigned char c) {
    return isAlpha(static_cast<char>(c)) || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string lowerAscii(std::string value) {
    for (char& c : value) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return value;
}

bool keepReservedChar(char c) {
    switch (c) {
    case ':': case '/': case '?': case '#': case '[': case ']': case '@':
    case '!': case '$': case '&': case '\'': case '(': case ')': case '*':
    case '+': case ',': case ';': case '=':
        return true;
    default:
        return false;
    }
}

} // namespace

bool ParsedUrl::secure() const {
    return lowerAscii(scheme) == "https";
}

std::string ParsedUrl::origin() const {
    if (scheme.empty()) return {};
    std::string out = scheme + "://";
    if (!userinfo.empty()) out += userinfo + "@";
    out += host;
    if (!port.empty()) out += ":" + port;
    return out;
}

std::string ParsedUrl::target() const {
    std::string out = path.empty() ? "/" : path;
    if (!query.empty()) out += "?" + query;
    return out;
}

bool parseUrl(const std::string& text, ParsedUrl& url, std::string* error) {
    url = ParsedUrl{};
    if (text.empty()) {
        if (error) *error = "URL is empty";
        return false;
    }

    std::size_t pos = 0;
    const std::size_t colon = text.find(':');
    if (colon != std::string::npos && colon > 0 && isAlpha(text[0])) {
        bool validScheme = true;
        for (std::size_t i = 1; i < colon; ++i) {
            if (!isSchemeChar(text[i])) {
                validScheme = false;
                break;
            }
        }
        if (validScheme) {
            url.scheme = lowerAscii(text.substr(0, colon));
            pos = colon + 1;
            if (url.scheme != "http" && url.scheme != "https") {
                if (error) *error = "unsupported URL scheme '" + url.scheme + "'";
                return false;
            }
            if (text.compare(pos, 2, "//") != 0) {
                if (error) *error = "URL must include // after the scheme";
                return false;
            }
            pos += 2;
            url.hasAuthority = true;
        }
    }

    if (url.hasAuthority) {
        const std::size_t authorityEnd = text.find_first_of("/?#", pos);
        const std::size_t end = authorityEnd == std::string::npos ? text.size() : authorityEnd;
        std::string authority = text.substr(pos, end - pos);
        const std::size_t at = authority.rfind('@');
        if (at != std::string::npos) {
            url.userinfo = authority.substr(0, at);
            authority.erase(0, at + 1);
        }
        if (!authority.empty() && authority[0] == '[') {
            const std::size_t close = authority.find(']');
            if (close == std::string::npos) {
                if (error) *error = "unterminated IPv6 host";
                return false;
            }
            url.host = authority.substr(0, close + 1);
            if (close + 1 < authority.size()) {
                if (authority[close + 1] != ':') {
                    if (error) *error = "invalid characters after IPv6 host";
                    return false;
                }
                url.port = authority.substr(close + 2);
            }
        } else {
            const std::size_t portSep = authority.rfind(':');
            if (portSep != std::string::npos) {
                url.host = authority.substr(0, portSep);
                url.port = authority.substr(portSep + 1);
            } else {
                url.host = authority;
            }
        }
        if (url.host.empty()) {
            if (error) *error = "URL host is empty";
            return false;
        }
        if (!url.port.empty()) {
            for (const char c : url.port) {
                if (c < '0' || c > '9') {
                    if (error) *error = "URL port is not numeric";
                    return false;
                }
            }
            const int port = std::atoi(url.port.c_str());
            if (port <= 0 || port > 65535) {
                if (error) *error = "URL port is out of range";
                return false;
            }
        }
        pos = end;
    }

    const std::size_t queryPos = text.find('?', pos);
    const std::size_t fragmentPos = text.find('#', pos);
    std::size_t pathEnd = text.size();
    if (queryPos != std::string::npos) pathEnd = std::min(pathEnd, queryPos);
    if (fragmentPos != std::string::npos) pathEnd = std::min(pathEnd, fragmentPos);
    url.path = text.substr(pos, pathEnd - pos);
    if (url.path.empty() && url.hasAuthority) url.path = "/";

    if (queryPos != std::string::npos && (fragmentPos == std::string::npos || queryPos < fragmentPos)) {
        std::size_t queryEnd = fragmentPos == std::string::npos ? text.size() : fragmentPos;
        url.query = text.substr(queryPos + 1, queryEnd - queryPos - 1);
    }
    if (fragmentPos != std::string::npos) {
        url.fragment = text.substr(fragmentPos + 1);
    }

    if (!url.hasAuthority && url.scheme.empty()) {
        // Relative URL: keep path/query/fragment and accept it for display.
        if (error) error->clear();
    }
    return true;
}

std::string buildUrl(const ParsedUrl& url) {
    std::string out;
    if (!url.scheme.empty()) {
        out = url.scheme + "://";
        if (!url.userinfo.empty()) out += url.userinfo + "@";
        out += url.host;
        if (!url.port.empty()) out += ":" + url.port;
    }
    out += url.path;
    if (!url.query.empty()) out += "?" + url.query;
    if (!url.fragment.empty()) out += "#" + url.fragment;
    return out;
}

std::string normalizeUrl(const std::string& text) {
    ParsedUrl url;
    if (!parseUrl(text, url, nullptr)) return text;
    if (!url.hasAuthority) return text;
    if (url.path.empty()) url.path = "/";
    return buildUrl(url);
}

std::string percentEncode(const std::string& value, bool keepReserved) {
    static const char* digits = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (const unsigned char c : value) {
        if (isUnreserved(c) || (keepReserved && keepReservedChar(static_cast<char>(c)))) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(digits[(c >> 4) & 0x0F]);
            out.push_back(digits[c & 0x0F]);
        }
    }
    return out;
}

std::string percentDecode(const std::string& value, bool plusAsSpace) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (c == '+' && plusAsSpace) {
            out.push_back(' ');
        } else if (c == '%' && i + 2 < value.size()) {
            const int hi = hexDigit(value[i + 1]);
            const int lo = hexDigit(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
            } else {
                out.push_back(c);
            }
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string formEncode(const std::string& value) {
    static const char* digits = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (const unsigned char c : value) {
        if (isUnreserved(c)) {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            out.push_back('+');
        } else {
            out.push_back('%');
            out.push_back(digits[(c >> 4) & 0x0F]);
            out.push_back(digits[c & 0x0F]);
        }
    }
    return out;
}

std::string formDecode(const std::string& value) {
    return percentDecode(value, true);
}

std::vector<QueryParam> parseQueryString(const std::string& query) {
    std::vector<QueryParam> result;
    std::size_t start = 0;
    while (start <= query.size()) {
        const std::size_t amp = query.find('&', start);
        const std::size_t end = amp == std::string::npos ? query.size() : amp;
        const std::string item = query.substr(start, end - start);
        if (!item.empty()) {
            const std::size_t eq = item.find('=');
            QueryParam param;
            param.enabled = true;
            if (eq == std::string::npos) {
                param.key = formDecode(item);
            } else {
                param.key = formDecode(item.substr(0, eq));
                param.value = formDecode(item.substr(eq + 1));
            }
            result.push_back(std::move(param));
        }
        if (amp == std::string::npos) break;
        start = amp + 1;
    }
    return result;
}

std::string buildQueryString(const std::vector<QueryParam>& params, bool includeDisabled) {
    std::string out;
    for (const auto& param : params) {
        if (!param.enabled && !includeDisabled) continue;
        if (!out.empty()) out.push_back('&');
        out += formEncode(param.key);
        out.push_back('=');
        out += formEncode(param.value);
    }
    return out;
}

std::vector<QueryParam> paramsFromUrl(const std::string& url) {
    ParsedUrl parsed;
    if (!parseUrl(url, parsed, nullptr) || parsed.query.empty()) return {};
    return parseQueryString(parsed.query);
}

std::string replaceQueryInUrl(const std::string& url, const std::vector<QueryParam>& params) {
    ParsedUrl parsed;
    if (!parseUrl(url, parsed, nullptr)) return url;
    parsed.query = buildQueryString(params, false);
    return buildUrl(parsed);
}

std::string appendQuery(const std::string& url, const std::string& extraQuery) {
    if (extraQuery.empty()) return url;
    ParsedUrl parsed;
    if (!parseUrl(url, parsed, nullptr)) return url;
    if (!parsed.query.empty() && parsed.query.back() != '&') parsed.query.push_back('&');
    parsed.query += extraQuery;
    return buildUrl(parsed);
}

std::string joinUrl(const std::string& base, const std::string& reference) {
    if (reference.find("://") != std::string::npos) return reference;
    ParsedUrl parsed;
    if (!parseUrl(base, parsed, nullptr) || !parsed.hasAuthority) return reference;
    if (reference.empty()) return buildUrl(parsed);
    if (reference[0] == '/') {
        parsed.path = reference;
        const std::size_t q = parsed.path.find('?');
        if (q != std::string::npos) {
            parsed.query = parsed.path.substr(q + 1);
            parsed.path.erase(q);
        } else {
            parsed.query.clear();
        }
        parsed.fragment.clear();
        return buildUrl(parsed);
    }
    const std::size_t slash = parsed.path.rfind('/');
    parsed.path = (slash == std::string::npos ? "/" : parsed.path.substr(0, slash + 1)) + reference;
    parsed.query.clear();
    parsed.fragment.clear();
    return buildUrl(parsed);
}

} // namespace lite
