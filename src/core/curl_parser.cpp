#include "core/curl_parser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "core/url.h"

namespace lite {
namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string lowerAscii(std::string value) {
    for (char& c : value) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return value;
}

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

std::size_t continuationLength(const std::string& text, std::size_t pos) {
    // pos points at the escape character (backslash, caret or backtick).
    if (pos + 1 >= text.size()) return 0;
    if (text[pos + 1] == '\n') return 2;
    if (text[pos + 1] == '\r' && pos + 2 < text.size() && text[pos + 2] == '\n') return 3;
    return 0;
}

void appendUtf8(std::string& out, unsigned int cp) {
    if (cp <= 0x7F) out.push_back(static_cast<char>(cp));
    else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

char ansiSimpleEscape(char c) {
    switch (c) {
    case 'a': return '\a';
    case 'b': return '\b';
    case 'e': return 27;
    case 'f': return '\f';
    case 'n': return '\n';
    case 'r': return '\r';
    case 't': return '\t';
    case 'v': return '\v';
    case '\\': return '\\';
    case '\'': return '\'';
    case '"': return '"';
    case '?': return '?';
    default: return 0;
    }
}

struct TokenizeResult {
    std::vector<std::string> tokens;
    std::string error;
};

TokenizeResult tokenize(const std::string& text, CurlShell shell) {
    TokenizeResult result;
    std::string token;
    bool tokenStarted = false;
    bool inSingle = false;
    bool inDouble = false;
    std::size_t i = 0;

    const auto pushToken = [&]() {
        if (tokenStarted) {
            result.tokens.push_back(token);
            token.clear();
            tokenStarted = false;
        }
    };

    while (i < text.size()) {
        const char c = text[i];
        if (inSingle) {
            if (shell == CurlShell::PowerShell && c == '\'' && i + 1 < text.size() && text[i + 1] == '\'') {
                token.push_back('\'');
                i += 2;
                continue;
            }
            if (c == '\'') {
                inSingle = false;
                ++i;
                continue;
            }
            token.push_back(c);
            ++i;
            continue;
        }
        if (inDouble) {
            if (shell == CurlShell::Bash) {
                if (c == '\\') {
                    const std::size_t continuation = continuationLength(text, i);
                    if (continuation) {
                        i += continuation;
                        continue;
                    }
                    if (i + 1 < text.size()) {
                        const char next = text[i + 1];
                        if (next == '"' || next == '\\' || next == '$' || next == '`') {
                            token.push_back(next);
                            i += 2;
                            continue;
                        }
                    }
                    token.push_back(c);
                    ++i;
                    continue;
                }
                if (c == '"') {
                    inDouble = false;
                    ++i;
                    continue;
                }
                token.push_back(c);
                ++i;
                continue;
            }
            if (shell == CurlShell::Cmd) {
                if (c == '^' && i + 1 < text.size() && text[i + 1] == '"') {
                    inDouble = false;
                    i += 2;
                    continue;
                }
                if (c == '\\') {
                    const std::size_t start = i;
                    while (i < text.size() && text[i] == '\\') ++i;
                    const std::size_t count = i - start;
                    if (i < text.size() && text[i] == '"') {
                        token.append(count / 2, '\\');
                        if (count % 2 != 0) {
                            token.push_back('"');
                            ++i;
                        } else {
                            inDouble = false;
                            ++i;
                        }
                    } else {
                        token.append(count, '\\');
                    }
                    continue;
                }
                if (c == '"') {
                    inDouble = false;
                    ++i;
                    continue;
                }
                token.push_back(c);
                ++i;
                continue;
            }
            // PowerShell
            if (c == '`') {
                const std::size_t continuation = continuationLength(text, i);
                if (continuation) {
                    i += continuation;
                    continue;
                }
                if (i + 1 < text.size()) {
                    const char next = text[i + 1];
                    char mapped = ansiSimpleEscape(next);
                    token.push_back(mapped != 0 ? mapped : next);
                    i += 2;
                    continue;
                }
                token.push_back(c);
                ++i;
                continue;
            }
            if (c == '"' && i + 1 < text.size() && text[i + 1] == '"') {
                token.push_back('"');
                i += 2;
                continue;
            }
            if (c == '"') {
                inDouble = false;
                ++i;
                continue;
            }
            token.push_back(c);
            ++i;
            continue;
        }

        if (isSpace(c)) {
            pushToken();
            ++i;
            continue;
        }
        if (shell == CurlShell::Bash && c == '\\') {
            const std::size_t continuation = continuationLength(text, i);
            if (continuation) {
                i += continuation;
                continue;
            }
            if (i + 1 < text.size()) {
                token.push_back(text[i + 1]);
                i += 2;
                continue;
            }
            token.push_back(c);
            ++i;
            continue;
        }
        if (shell == CurlShell::Cmd && c == '^') {
            const std::size_t continuation = continuationLength(text, i);
            if (continuation) {
                i += continuation;
                continue;
            }
            if (i + 1 < text.size()) {
                if (text[i + 1] == '"') {
                    tokenStarted = true;
                    if (!inDouble) inDouble = true;
                    i += 2;
                    continue;
                }
                token.push_back(text[i + 1]);
                tokenStarted = true;
                i += 2;
                continue;
            }
            token.push_back(c);
            ++i;
            continue;
        }
        if (shell == CurlShell::PowerShell && c == '`') {
            const std::size_t continuation = continuationLength(text, i);
            if (continuation) {
                i += continuation;
                continue;
            }
            if (i + 1 < text.size()) {
                const char next = text[i + 1];
                const char mapped = ansiSimpleEscape(next);
                token.push_back(mapped != 0 ? mapped : next);
                tokenStarted = true;
                i += 2;
                continue;
            }
            token.push_back(c);
            ++i;
            continue;
        }
        if (shell == CurlShell::Bash && c == '$' && i + 1 < text.size() && text[i + 1] == '\'') {
            tokenStarted = true;
            i += 2;
            bool closed = false;
            while (i < text.size()) {
                if (text[i] == '\'') {
                    closed = true;
                    ++i;
                    break;
                }
                if (text[i] != '\\') {
                    token.push_back(text[i]);
                    ++i;
                    continue;
                }
                const std::size_t continuation = continuationLength(text, i);
                if (continuation) {
                    i += continuation;
                    continue;
                }
                if (i + 1 >= text.size()) {
                    ++i;
                    break;
                }
                const char esc = text[i + 1];
                i += 2;
                if (esc == 'x' || esc == 'u' || esc == 'U') {
                    const int maxDigits = esc == 'x' ? 2 : (esc == 'u' ? 4 : 8);
                    unsigned int cp = 0;
                    int digits = 0;
                    while (digits < maxDigits && i < text.size()) {
                        const int value = hexValue(text[i]);
                        if (value < 0) break;
                        cp = (cp << 4) | static_cast<unsigned int>(value);
                        ++i;
                        ++digits;
                    }
                    if (digits == 0) token.push_back(esc);
                    else appendUtf8(token, cp);
                    continue;
                }
                if (esc >= '0' && esc <= '7') {
                    unsigned int value = static_cast<unsigned int>(esc - '0');
                    int digits = 1;
                    while (digits < 3 && i < text.size() && text[i] >= '0' && text[i] <= '7') {
                        value = (value << 3) | static_cast<unsigned int>(text[i] - '0');
                        ++i;
                        ++digits;
                    }
                    appendUtf8(token, value);
                    continue;
                }
                const char mapped = ansiSimpleEscape(esc);
                token.push_back(mapped != 0 ? mapped : esc);
            }
            if (!closed) {
                result.error = "Unterminated $'...' string";
                return result;
            }
            continue;
        }
        if (c == '\'') {
            inSingle = true;
            tokenStarted = true;
            ++i;
            continue;
        }
        if (c == '"') {
            inDouble = true;
            tokenStarted = true;
            ++i;
            continue;
        }
        token.push_back(c);
        tokenStarted = true;
        ++i;
    }

    if (inSingle || inDouble) {
        result.error = inSingle ? "Unterminated single-quoted string" : "Unterminated double-quoted string";
        return result;
    }
    pushToken();
    return result;
}

bool isFlagToken(const std::string& token) {
    return token.size() > 1 && token[0] == '-' && token != "--";
}

std::vector<std::pair<std::string, std::string>> splitPairs(const std::string& data) {
    std::vector<std::pair<std::string, std::string>> result;
    std::size_t start = 0;
    while (start <= data.size()) {
        const std::size_t amp = data.find('&', start);
        const std::string item = data.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
        if (!item.empty()) {
            const std::size_t eq = item.find('=');
            result.emplace_back(formDecode(item.substr(0, eq)), eq == std::string::npos ? std::string() : formDecode(item.substr(eq + 1)));
        }
        if (amp == std::string::npos) break;
        start = amp + 1;
    }
    return result;
}

std::string queryEncodeChunk(const std::string& chunk) {
    const std::size_t eq = chunk.find('=');
    if (eq == std::string::npos) return formEncode(chunk);
    if (eq == 0) return formEncode(chunk.substr(1));
    return formEncode(chunk.substr(0, eq)) + "=" + formEncode(chunk.substr(eq + 1));
}

bool parseFormToken(const std::string& raw, KeyValue& field) {
    const std::size_t eq = raw.find('=');
    if (eq == std::string::npos) {
        field.key = raw;
        field.value.clear();
        field.isFile = false;
        return !raw.empty();
    }
    field.key = raw.substr(0, eq);
    std::string value = raw.substr(eq + 1);
    field.isFile = false;
    if (!value.empty() && value[0] == '@') {
        field.isFile = true;
        value.erase(0, 1);
        const std::size_t semicolon = value.find(';');
        if (semicolon != std::string::npos) value.erase(semicolon);
    } else if (!value.empty() && value[0] == '<') {
        field.isFile = true;
        value.erase(0, 1);
        const std::size_t semicolon = value.find(';');
        if (semicolon != std::string::npos) value.erase(semicolon);
    }
    field.value = value;
    return !field.key.empty();
}

std::string uniqueVariableName(const std::string& base, const std::vector<std::string>& used) {
    std::string name = base.empty() ? "variable" : base;
    int suffix = 2;
    while (std::find(used.begin(), used.end(), name) != used.end()) {
        name = base + std::to_string(suffix++);
    }
    return name;
}

std::string sanitizedHeaderName(const std::string& headerName) {
    std::string out;
    for (const char c : headerName) {
        if (std::isalnum(static_cast<unsigned char>(c))) out.push_back(c);
    }
    if (out.empty()) return "apiKey";
    out[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[0])));
    if (out == "xApiKey" || out == "apiKey" || out == "apikey") return "apiKey";
    return out;
}

void replaceAll(std::string& text, const std::string& needle, const std::string& replacement) {
    if (needle.empty()) return;
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        text.replace(pos, needle.size(), replacement);
        pos += replacement.size();
    }
}

} // namespace

CurlShell detectCurlShell(const std::string& command) {
    if (command.find("$'") != std::string::npos) return CurlShell::Bash;
    if (command.find("`\n") != std::string::npos || command.find("`\r\n") != std::string::npos) return CurlShell::PowerShell;
    if (command.find("^\n") != std::string::npos || command.find("^\r\n") != std::string::npos || command.find("^\"") != std::string::npos) return CurlShell::Cmd;
    const std::string lower = lowerAscii(command);
    if (startsWith(lower, "curl.exe") || startsWith(lower, "\"curl.exe\"") || startsWith(lower, "& curl.exe")) return CurlShell::PowerShell;
    return CurlShell::Bash;
}

const char* curlShellName(CurlShell shell) {
    switch (shell) {
    case CurlShell::Cmd: return "cmd";
    case CurlShell::PowerShell: return "powershell";
    case CurlShell::Bash: return "bash";
    default: return "auto";
    }
}

CurlImportResult parseCurl(const std::string& command, CurlShell shell) {
    CurlImportResult result;
    if (shell == CurlShell::Auto) shell = detectCurlShell(command);
    result.shell = shell;
    const TokenizeResult tokenized = tokenize(command, shell);
    if (!tokenized.error.empty()) {
        result.error = tokenized.error;
        return result;
    }
    std::vector<std::string> tokens = tokenized.tokens;
    if (tokens.empty()) {
        result.error = "The cURL command is empty";
        return result;
    }
    std::size_t index = 0;
    if (lowerAscii(tokens[0]) == "curl" || lowerAscii(tokens[0]) == "curl.exe" ||
        (tokens[0].size() > 4 && lowerAscii(tokens[0].substr(tokens[0].size() - 4)) == ".exe" &&
         lowerAscii(tokens[0]).find("curl") != std::string::npos)) {
        index = 1;
    } else if (tokens[0] == "&" && tokens.size() > 1) {
        index = 2;
    }
    if (index >= tokens.size()) {
        result.error = "No cURL arguments were found";
        return result;
    }

    RequestModel request;
    request.name = "Imported Request";
    request.options.followRedirects = false;
    request.options.ignoreTlsErrors = false;
    request.options.acceptCompressed = false;
    request.options.timeoutMs = 30000;
    request.options.useCustomProxy = false;

    std::string url;
    std::string explicitMethod;
    bool hasMethod = false;
    bool getMode = false;
    bool headMode = false;
    bool insecure = false;
    bool location = false;
    bool compressed = false;
    bool hasForm = false;
    bool hasProxy = false;
    bool hasTimeout = false;
    std::string proxy;
    double timeoutSeconds = 30.0;
    std::vector<std::string> plainData;
    std::vector<std::string> encodedData;
    std::vector<KeyValue> formFields;
    std::string basicUser;
    bool hasBasicUser = false;
    std::string cookies;
    std::string userAgent;
    std::string referer;

    const auto takeValue = [&](std::size_t& cursor, const std::string& inlineValue, bool hasInline,
                               const std::string& flag, std::string& value) -> bool {
        if (hasInline) {
            value = inlineValue;
            return true;
        }
        if (cursor + 1 >= tokens.size()) {
            result.warnings.push_back("Missing value for " + flag);
            return false;
        }
        value = tokens[++cursor];
        return true;
    };

    while (index < tokens.size()) {
        const std::string token = tokens[index];
        if (token == "--") {
            ++index;
            while (index < tokens.size()) {
                if (url.empty()) url = tokens[index];
                else result.warnings.push_back("Ignored extra argument: " + tokens[index]);
                ++index;
            }
            break;
        }
        if (!isFlagToken(token)) {
            if (url.empty()) url = token;
            else result.warnings.push_back("Ignored extra argument: " + token);
            ++index;
            continue;
        }

        std::string flag;
        std::string inlineValue;
        bool hasInline = false;
        if (startsWith(token, "--")) {
            const std::size_t eq = token.find('=');
            flag = token.substr(0, eq);
            if (eq != std::string::npos) {
                hasInline = true;
                inlineValue = token.substr(eq + 1);
            }
        } else {
            flag = token.substr(0, 2);
            if (token.size() > 2) {
                hasInline = true;
                inlineValue = token.substr(2);
            }
        }

        const auto needsValue = [&](std::string& value) {
            if (!takeValue(index, inlineValue, hasInline, flag, value)) return false;
            return true;
        };

        if (flag == "-X" || flag == "--request") {
            std::string value;
            if (needsValue(value) && !value.empty()) {
                explicitMethod = methodNormalized(value);
                hasMethod = true;
            }
        } else if (flag == "-H" || flag == "--header") {
            std::string value;
            if (needsValue(value)) {
                const std::size_t colon = value.find(':');
                if (colon == std::string::npos || colon == 0) {
                    result.warnings.push_back("Ignored malformed header: " + value);
                } else {
                    request.headers.push_back(KeyValue{value.substr(0, colon), value.substr(colon + 1), true, false, false});
                }
            }
        } else if (flag == "-d" || flag == "--data" || flag == "--data-raw" || flag == "--data-binary" || flag == "--data-ascii") {
            std::string value;
            if (needsValue(value)) plainData.push_back(value);
        } else if (flag == "--data-urlencode") {
            std::string value;
            if (needsValue(value)) encodedData.push_back(value);
        } else if (flag == "-F" || flag == "--form" || flag == "--form-string") {
            std::string value;
            if (needsValue(value)) {
                KeyValue field;
                if (parseFormToken(value, field)) {
                    formFields.push_back(std::move(field));
                    hasForm = true;
                } else {
                    result.warnings.push_back("Ignored malformed form field");
                }
            }
        } else if (flag == "-u" || flag == "--user") {
            std::string value;
            if (needsValue(value)) {
                hasBasicUser = true;
                const std::size_t colon = value.find(':');
                basicUser = colon == std::string::npos ? value : value.substr(0, colon);
                request.password = colon == std::string::npos ? std::string() : value.substr(colon + 1);
                request.username = basicUser;
            }
        } else if (flag == "-b" || flag == "--cookie") {
            std::string value;
            if (needsValue(value)) {
                if (!cookies.empty()) cookies += "; ";
                cookies += value;
            }
        } else if (flag == "-A" || flag == "--user-agent") {
            std::string value;
            if (needsValue(value)) userAgent = value;
        } else if (flag == "-e" || flag == "--referer") {
            std::string value;
            if (needsValue(value)) referer = value;
        } else if (flag == "-G" || flag == "--get") {
            getMode = true;
        } else if (flag == "-I" || flag == "--head") {
            headMode = true;
        } else if (flag == "-k" || flag == "--insecure") {
            insecure = true;
        } else if (flag == "-L" || flag == "--location") {
            location = true;
        } else if (flag == "--compressed") {
            compressed = true;
        } else if (flag == "-x" || flag == "--proxy") {
            std::string value;
            if (needsValue(value)) {
                hasProxy = true;
                proxy = value;
            }
        } else if (flag == "-m" || flag == "--max-time") {
            std::string value;
            if (needsValue(value)) {
                char* end = nullptr;
                const double seconds = std::strtod(value.c_str(), &end);
                if (end && end != value.c_str() && seconds > 0.0) {
                    timeoutSeconds = seconds;
                    hasTimeout = true;
                }
            }
        } else if (flag == "--url") {
            std::string value;
            if (needsValue(value)) url = value;
        } else {
            result.unknownFlags.push_back(flag);
        }
        ++index;
    }

    if (url.empty()) {
        result.error = "No URL was found in the cURL command";
        return result;
    }
    if (url.find("://") == std::string::npos && !startsWith(url, "{{")) url = "http://" + url;

    if (!userAgent.empty()) setHeader(request.headers, "User-Agent", userAgent);
    if (!referer.empty()) setHeader(request.headers, "Referer", referer);
    if (!cookies.empty()) setHeader(request.headers, "Cookie", cookies);

    if (hasBasicUser) {
        request.authType = AuthType::Basic;
    }

    std::string contentType = findHeader(request.headers, "Content-Type");
    const std::string contentTypeLower = lowerAscii(contentType);

    // Authorization headers map to the Auth tab. Other auth headers are left
    // untouched so projects with custom schemes still work.
    bool removeAuthorization = false;
    for (auto& header : request.headers) {
        if (lowerAscii(header.key) == "authorization") {
            const std::string lowerValue = lowerAscii(header.value);
            if (startsWith(lowerValue, "basic ")) {
                std::string decoded;
                if (base64Decode(header.value.substr(6), decoded)) {
                    const std::size_t colon = decoded.find(':');
                    request.authType = AuthType::Basic;
                    request.username = colon == std::string::npos ? decoded : decoded.substr(0, colon);
                    request.password = colon == std::string::npos ? std::string() : decoded.substr(colon + 1);
                    removeAuthorization = true;
                }
            } else if (startsWith(lowerValue, "bearer ")) {
                request.authType = AuthType::Bearer;
                request.token = header.value.substr(7);
                removeAuthorization = true;
            }
        }
    }
    if (removeAuthorization) {
        request.headers.erase(std::remove_if(request.headers.begin(), request.headers.end(), [](const KeyValue& header) {
            return lowerAscii(header.key) == "authorization";
        }), request.headers.end());
    }

    std::string plainJoined;
    for (std::size_t i = 0; i < plainData.size(); ++i) {
        if (i != 0) plainJoined.push_back('&');
        plainJoined += plainData[i];
    }
    std::string encodedJoined;
    for (std::size_t i = 0; i < encodedData.size(); ++i) {
        if (i != 0) encodedJoined.push_back('&');
        encodedJoined += encodedData[i];
    }

    std::vector<std::string> queryChunks;
    for (const auto& chunk : plainData) queryChunks.push_back(chunk);
    for (const auto& chunk : encodedData) queryChunks.push_back(queryEncodeChunk(chunk));

    if (hasForm) {
        request.bodyMode = BodyMode::Multipart;
        request.formFields = std::move(formFields);
        removeHeader(request.headers, "Content-Type");
    } else if (!plainData.empty() || !encodedData.empty()) {
        if (contentTypeLower.find("json") != std::string::npos) {
            request.bodyMode = BodyMode::Raw;
            request.bodyLanguage = BodyLanguage::Json;
            request.rawBody = plainJoined;
            if (!encodedJoined.empty()) request.rawBody += encodedJoined;
        } else if (contentTypeLower.find("xml") != std::string::npos) {
            request.bodyMode = BodyMode::Raw;
            request.bodyLanguage = BodyLanguage::Xml;
            request.rawBody = plainJoined;
        } else if (contentTypeLower.find("multipart") != std::string::npos) {
            request.bodyMode = BodyMode::Raw;
            request.bodyLanguage = BodyLanguage::Text;
            request.rawBody = plainJoined;
        } else if (contentTypeLower.find("text/") != std::string::npos) {
            request.bodyMode = BodyMode::Raw;
            request.bodyLanguage = BodyLanguage::Text;
            request.rawBody = plainJoined;
        } else {
            request.bodyMode = BodyMode::FormUrlEncoded;
            for (const auto& pair : splitPairs(plainJoined)) {
                request.formFields.push_back(KeyValue{pair.first, pair.second, true, false, false});
            }
            for (const auto& chunk : encodedData) {
                const std::size_t eq = chunk.find('=');
                if (eq == std::string::npos) {
                    if (!chunk.empty()) request.formFields.push_back(KeyValue{"", chunk, true, false, false});
                } else {
                    request.formFields.push_back(KeyValue{chunk.substr(0, eq), chunk.substr(eq + 1), true, false, false});
                }
            }
        }
    }

    if (getMode) {
        std::string query;
        for (const auto& chunk : queryChunks) {
            if (!query.empty()) query.push_back('&');
            query += chunk;
        }
        url = appendQuery(url, query);
        request.bodyMode = BodyMode::None;
        request.rawBody.clear();
        request.formFields.clear();
    }

    request.url = url;
    request.params.clear();
    for (const auto& param : paramsFromUrl(url)) {
        request.params.push_back(KeyValue{param.key, param.value, param.enabled, false, false});
    }

    if (hasMethod) {
        request.method = explicitMethod;
    } else if (headMode) {
        request.method = "HEAD";
    } else {
        request.method = (request.bodyMode != BodyMode::None) ? "POST" : "GET";
    }

    request.options.followRedirects = location;
    request.options.ignoreTlsErrors = insecure;
    request.options.acceptCompressed = compressed;
    request.options.timeoutMs = hasTimeout ? std::max(100, static_cast<int>(timeoutSeconds * 1000.0)) : 30000;
    request.options.useCustomProxy = hasProxy;
    request.options.proxy = proxy;

    result.request = std::move(request);
    result.ok = true;
    return result;
}

std::vector<VariableSuggestion> suggestCurlVariables(const RequestModel& request, const EnvironmentStore& environments) {
    std::vector<VariableSuggestion> suggestions;
    std::vector<std::string> usedNames;
    for (const auto& variable : environments.activeVariables()) usedNames.push_back(variable.first);

    ParsedUrl parsed;
    if (parseUrl(request.url, parsed, nullptr) && parsed.hasAuthority) {
        const std::string origin = parsed.origin();
        if (!origin.empty() && request.url.find("{{") == std::string::npos) {
            VariableSuggestion suggestion;
            suggestion.name = uniqueVariableName("baseUrl", usedNames);
            suggestion.value = origin;
            suggestion.description = "Base URL (" + origin + ")";
            suggestion.target = VariableSuggestion::Target::BaseUrl;
            suggestions.push_back(suggestion);
            usedNames.push_back(suggestion.name);
        }
    }

    if (request.authType == AuthType::Bearer && !request.token.empty() && request.token.find("{{") == std::string::npos) {
        VariableSuggestion suggestion;
        suggestion.name = uniqueVariableName("token", usedNames);
        suggestion.value = request.token;
        suggestion.description = "Bearer token";
        suggestion.target = VariableSuggestion::Target::BearerToken;
        suggestion.secret = true;
        suggestions.push_back(suggestion);
        usedNames.push_back(suggestion.name);
    }

    for (const auto& header : request.headers) {
        if (!header.enabled) continue;
        const std::string name = lowerAscii(header.key);
        const bool apiKeyHeader = name == "x-api-key" || name == "api-key" || name == "apikey" ||
                                  (name.find("key") != std::string::npos && name.find("content") == std::string::npos);
        if (!apiKeyHeader || header.value.empty() || header.value.find("{{") != std::string::npos) continue;
        VariableSuggestion suggestion;
        suggestion.name = uniqueVariableName(sanitizedHeaderName(header.key), usedNames);
        suggestion.value = header.value;
        suggestion.description = "Header " + header.key;
        suggestion.target = VariableSuggestion::Target::ApiKeyHeader;
        suggestion.headerName = header.key;
        suggestion.secret = true;
        suggestions.push_back(suggestion);
        usedNames.push_back(suggestion.name);
    }
    return suggestions;
}

void applyVariableSuggestions(RequestModel& request, EnvironmentStore& environments,
                              const std::vector<const VariableSuggestion*>& accepted,
                              std::string* error) {
    if (accepted.empty()) return;
    Environment* environment = environments.active();
    if (!environment) {
        Environment created;
        created.id = newId("environment");
        created.name = "Imported Variables";
        environments.environments.push_back(std::move(created));
        environments.activeId = environments.environments.back().id;
        environment = &environments.environments.back();
    }
    if (!environment) {
        if (error) *error = "Could not create an environment for imported variables";
        return;
    }
    for (const auto* suggestion : accepted) {
        if (!suggestion) continue;
        auto existing = std::find_if(environment->variables.begin(), environment->variables.end(),
                                     [&](const EnvironmentVar& value) { return value.key == suggestion->name; });
        if (existing == environment->variables.end()) {
            environment->variables.push_back(EnvironmentVar{suggestion->name, suggestion->value, true, suggestion->secret});
        } else {
            existing->value = suggestion->value;
            existing->enabled = true;
            existing->secret = suggestion->secret;
        }
        const std::string placeholder = "{{" + suggestion->name + "}}";
        if (suggestion->target == VariableSuggestion::Target::BaseUrl) {
            replaceAll(request.url, suggestion->value, placeholder);
        } else if (suggestion->target == VariableSuggestion::Target::BearerToken) {
            if (request.authType == AuthType::Bearer && request.token == suggestion->value) {
                request.token = placeholder;
            }
        } else if (suggestion->target == VariableSuggestion::Target::ApiKeyHeader) {
            for (auto& header : request.headers) {
                if (lowerAscii(header.key) == lowerAscii(suggestion->headerName) && header.value == suggestion->value) {
                    header.value = placeholder;
                    break;
                }
            }
        }
    }
}

} // namespace lite
