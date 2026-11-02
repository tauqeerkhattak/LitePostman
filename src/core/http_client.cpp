#include "core/http_client.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>

#include "core/url.h"

namespace lite {
namespace {

std::wstring utf8ToWide(const std::string& text, bool* ok = nullptr) {
    if (text.empty()) {
        if (ok) *ok = true;
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) {
        if (ok) *ok = false;
        return {};
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), length);
    if (ok) *ok = true;
    return result;
}

std::string wideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
    return result;
}

std::string trimAscii(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' || text[begin] == '\n')) ++begin;
    std::size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' || text[end - 1] == '\n')) --end;
    return text.substr(begin, end - begin);
}

std::string lowerAscii(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

class InternetHandle {
public:
    explicit InternetHandle(HINTERNET handle = nullptr) : handle_(handle) {}
    ~InternetHandle() { reset(); }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
    InternetHandle(InternetHandle&& other) noexcept : handle_(other.release()) {}
    InternetHandle& operator=(InternetHandle&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = other.release();
        }
        return *this;
    }
    HINTERNET get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }
    void reset(HINTERNET handle = nullptr) {
        if (handle_) WinHttpCloseHandle(handle_);
        handle_ = handle;
    }
    HINTERNET release() {
        HINTERNET result = handle_;
        handle_ = nullptr;
        return result;
    }

private:
    HINTERNET handle_ = nullptr;
};

struct RawResponseHeaders {
    std::vector<std::pair<std::string, std::string>> headers;
    std::vector<HttpCookie> cookies;
    std::string reason;
};

RawResponseHeaders parseResponseHeaders(const std::string& raw) {
    RawResponseHeaders result;
    std::size_t start = 0;
    bool first = true;
    while (start <= raw.size()) {
        const std::size_t end = raw.find("\r\n", start);
        const std::string line = raw.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (first) {
            first = false;
            const std::size_t firstSpace = line.find(' ');
            if (firstSpace != std::string::npos) {
                const std::size_t secondSpace = line.find(' ', firstSpace + 1);
                if (secondSpace != std::string::npos) {
                    result.reason = trimAscii(line.substr(secondSpace + 1));
                }
            }
        } else if (!line.empty()) {
            if ((line[0] == ' ' || line[0] == '\t') && !result.headers.empty()) {
                result.headers.back().second += " " + trimAscii(line);
            } else {
                const std::size_t colon = line.find(':');
                if (colon != std::string::npos) {
                    const std::string name = line.substr(0, colon);
                    const std::string value = trimAscii(line.substr(colon + 1));
                    if (lowerAscii(name) == "set-cookie") {
                        HttpCookie cookie;
                        const std::size_t semi = value.find(';');
                        const std::string pair = semi == std::string::npos ? value : value.substr(0, semi);
                        const std::size_t equals = pair.find('=');
                        if (equals != std::string::npos) {
                            cookie.name = trimAscii(pair.substr(0, equals));
                            cookie.value = trimAscii(pair.substr(equals + 1));
                            std::size_t pos = semi;
                            while (pos != std::string::npos && pos < value.size()) {
                                const std::size_t next = value.find(';', pos + 1);
                                std::string attribute = trimAscii(value.substr(pos + 1, next == std::string::npos ? std::string::npos : next - pos - 1));
                                const std::size_t eq = attribute.find('=');
                                const std::string attrName = lowerAscii(trimAscii(attribute.substr(0, eq)));
                                const std::string attrValue = eq == std::string::npos ? std::string() : trimAscii(attribute.substr(eq + 1));
                                if (attrName == "path") cookie.path = attrValue;
                                else if (attrName == "domain") cookie.domain = attrValue;
                                else if (attrName == "expires") cookie.expires = attrValue;
                                else if (attrName == "secure") cookie.secure = true;
                                else if (attrName == "httponly") cookie.httpOnly = true;
                                pos = next;
                            }
                            result.cookies.push_back(std::move(cookie));
                        }
                    }
                    result.headers.emplace_back(name, value);
                }
            }
        }
        if (end == std::string::npos) break;
        start = end + 2;
    }
    return result;
}

std::string queryHeaderString(HINTERNET request, DWORD query) {
    DWORD size = 0;
    WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &size, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) return {};
    std::wstring buffer(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, buffer.data(), &size, WINHTTP_NO_HEADER_INDEX)) return {};
    while (!buffer.empty() && buffer.back() == L'\0') buffer.pop_back();
    return wideToUtf8(buffer);
}

std::string statusText(int code) {
    switch (code) {
    case 12001: return "WinHTTP is out of handles";
    case 12002: return "The request timed out";
    case 12005: return "The URL is invalid";
    case 12006: return "The server name could not be resolved (DNS failure)";
    case 12007: return "The server name could not be resolved (DNS failure)";
    case 12009: return "The proxy rejected the request";
    case 12015: return "The redirect target is invalid";
    case 12016: return "The operation was cancelled";
    case 12017: return "The operation was cancelled";
    case 12019: return "The server returned an invalid response";
    case 12029: return "Could not connect to the server";
    case 12030: return "The connection was aborted";
    case 12031: return "The connection was reset";
    case 12032: return "The request could not be sent";
    case 12037: return "The server certificate is invalid or expired";
    case 12038: return "The server certificate authority is not trusted";
    case 12044: return "The server certificate name does not match";
    case 12045: return "The server certificate is invalid";
    case 12057: return "The server certificate is invalid or revoked";
    default: return {};
    }
}

} // namespace

void CancelToken::cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    cancelled_.store(true, std::memory_order_relaxed);
    if (request_ && !finished_) {
        WinHttpCloseHandle(static_cast<HINTERNET>(request_));
        request_ = nullptr;
    }
}

bool CancelToken::attach(void* requestHandle) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cancelled_ || finished_) return false;
    request_ = requestHandle;
    return true;
}

bool CancelToken::detach(void* requestHandle) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (request_ == requestHandle) {
        request_ = nullptr;
        return true;
    }
    return false;
}

void CancelToken::finish() {
    std::lock_guard<std::mutex> lock(mutex_);
    finished_ = true;
    request_ = nullptr;
}

std::string winHttpErrorText(unsigned long code) {
    std::string known = statusText(static_cast<int>(code));
    if (!known.empty()) return known;
    wchar_t* buffer = nullptr;
    HMODULE module = GetModuleHandleW(L"winhttp.dll");
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_FROM_HMODULE,
                                        module, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    if (length > 0 && buffer) {
        std::string text = wideToUtf8(std::wstring(buffer, length));
        LocalFree(buffer);
        text = trimAscii(text);
        if (!text.empty()) return text;
    }
    char fallback[64];
    std::snprintf(fallback, sizeof(fallback), "WinHTTP error %lu", code);
    return fallback;
}

HttpResponse HttpClient::execute(const HttpRequest& request, CancelToken& cancel) {
    const auto startedAt = std::chrono::steady_clock::now();
    const auto elapsed = [&startedAt]() {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count();
    };
    HttpResponse response;
    if (cancel.cancelled()) {
        response.canceled = true;
        response.error = "Request cancelled";
        return response;
    }

    ParsedUrl parsed;
    std::string url = request.url;
    std::string parseError;
    if (!parseUrl(url, parsed, &parseError) || !parsed.hasAuthority) {
        response.error = parseError.empty() ? "Enter an absolute http:// or https:// URL" : parseError;
        return response;
    }
    if (parsed.secure() && parsed.port.empty()) parsed.port = "443";
    if (parsed.scheme == "http" && parsed.port.empty()) parsed.port = "80";

    bool wideOk = true;
    const std::wstring host = utf8ToWide(parsed.host, &wideOk);
    const std::wstring target = utf8ToWide(parsed.target(), &wideOk);
    const std::wstring method = utf8ToWide(methodNormalized(request.method), &wideOk);
    if (!wideOk) {
        response.error = "The URL contains invalid UTF-8";
        return response;
    }

    const int timeout = std::clamp(request.options.timeoutMs, 100, 3600000);
    std::wstring proxy;
    DWORD accessType = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
    if (request.options.useCustomProxy && !request.options.proxy.empty()) {
        std::string proxyText = request.options.proxy;
        const std::size_t scheme = proxyText.find("://");
        if (scheme != std::string::npos) proxyText.erase(0, scheme + 3);
        proxy = utf8ToWide(proxyText, &wideOk);
        accessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
        if (!wideOk) {
            response.error = "The proxy address contains invalid UTF-8";
            return response;
        }
    }

    InternetHandle session(WinHttpOpen(L"LitePostman/1.0", accessType,
                                       request.options.useCustomProxy && !proxy.empty() ? proxy.c_str() : WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        response.error = "Cannot initialize WinHTTP: " + winHttpErrorText(GetLastError());
        return response;
    }
    WinHttpSetTimeouts(session.get(), timeout, timeout, timeout, timeout);

    if (request.options.acceptCompressed) {
        DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_DEFLATE | WINHTTP_DECOMPRESSION_FLAG_GZIP;
        WinHttpSetOption(session.get(), WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression));
    }

    std::string hostForConnect = parsed.host;
    if (hostForConnect.size() >= 2 && hostForConnect.front() == '[' && hostForConnect.back() == ']') {
        hostForConnect = hostForConnect.substr(1, hostForConnect.size() - 2);
    }
    const std::wstring connectHost = utf8ToWide(hostForConnect, &wideOk);
    const INTERNET_PORT connectPort = static_cast<INTERNET_PORT>(std::atoi(parsed.port.c_str()));
    InternetHandle connection(WinHttpConnect(session.get(), connectHost.c_str(), connectPort, 0));
    if (!connection) {
        response.error = "Cannot connect to " + parsed.host + ": " + winHttpErrorText(GetLastError());
        return response;
    }

    DWORD openFlags = parsed.secure() ? WINHTTP_FLAG_SECURE : 0;
    InternetHandle req(WinHttpOpenRequest(connection.get(), method.c_str(), target.c_str(), nullptr,
                                          WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, openFlags));
    if (!req) {
        response.error = "Cannot create the HTTP request: " + winHttpErrorText(GetLastError());
        return response;
    }

    if (!cancel.attach(req.get())) {
        response.canceled = true;
        response.error = "Request cancelled";
        return response;
    }
    const auto disposeRequest = [&]() {
        if (cancel.detach(req.get())) req.reset();
        else req.release(); // cancellation already closed the handle
    };

    DWORD redirectPolicy = request.options.followRedirects ? WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS : WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(req.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    if (request.options.ignoreTlsErrors) {
        DWORD securityFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                              SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                              SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                              SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(req.get(), WINHTTP_OPTION_SECURITY_FLAGS, &securityFlags, sizeof(securityFlags));
    }

    std::vector<std::pair<std::string, std::string>> headers = request.headers;
    bool hasAcceptEncoding = false;
    for (const auto& header : headers) {
        const std::string name = lowerAscii(header.first);
        if (name == "accept-encoding") hasAcceptEncoding = true;
    }
    if (request.options.acceptCompressed && !hasAcceptEncoding) {
        headers.emplace_back("Accept-Encoding", "gzip, deflate");
    }

    for (const auto& header : headers) {
        if (header.first.empty()) continue;
        if (header.first.find_first_of("\r\n") != std::string::npos || header.second.find_first_of("\r\n") != std::string::npos) {
            response.error = "A header contains a line break and was rejected";
            disposeRequest();
            return response;
        }
        const std::wstring line = utf8ToWide(header.first + ": " + header.second, &wideOk);
        if (!wideOk) {
            response.error = "A header contains invalid UTF-8";
            disposeRequest();
            return response;
        }
        if (!WinHttpAddRequestHeaders(req.get(), line.c_str(), static_cast<DWORD>(-1),
                                      WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
            response.error = "Cannot add header '" + header.first + "': " + winHttpErrorText(GetLastError());
            disposeRequest();
            return response;
        }
    }

    if (cancel.cancelled()) {
        response.canceled = true;
        response.error = "Request cancelled";
        disposeRequest();
        return response;
    }

    const DWORD bodyLength = static_cast<DWORD>(request.body.size());
    if (!WinHttpSendRequest(req.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            request.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(request.body.data()),
                            bodyLength, bodyLength, 0)) {
        const DWORD code = GetLastError();
        response.elapsedMs = elapsed();
        if (cancel.cancelled()) {
            response.canceled = true;
            response.error = "Request cancelled";
        } else {
            response.error = "Cannot send request: " + winHttpErrorText(code);
        }
        disposeRequest();
        return response;
    }

    if (!WinHttpReceiveResponse(req.get(), nullptr)) {
        const DWORD code = GetLastError();
        response.elapsedMs = elapsed();
        if (cancel.cancelled()) {
            response.canceled = true;
            response.error = "Request cancelled";
        } else {
            response.error = "Cannot read response: " + winHttpErrorText(code);
        }
        disposeRequest();
        return response;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
    response.statusCode = static_cast<int>(status);
    const std::string rawHeaders = queryHeaderString(req.get(), WINHTTP_QUERY_RAW_HEADERS_CRLF);
    RawResponseHeaders parsedHeaders = parseResponseHeaders(rawHeaders);
    response.headers = std::move(parsedHeaders.headers);
    response.cookies = std::move(parsedHeaders.cookies);
    response.reason = parsedHeaders.reason.empty() ? std::string("HTTP response") : parsedHeaders.reason;

    constexpr std::size_t maxBody = 256u * 1024u * 1024u;
    while (true) {
        if (cancel.cancelled()) {
            response.canceled = true;
            response.error = "Request cancelled";
            break;
        }
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(req.get(), &available)) {
            if (!cancel.cancelled()) response.error = "Cannot read response body: " + winHttpErrorText(GetLastError());
            break;
        }
        if (available == 0) break;
        if (response.body.size() + available > maxBody) {
            response.error = "Response exceeds the 256 MB safety limit";
            break;
        }
        std::vector<char> buffer(std::min<DWORD>(available, 65536));
        DWORD read = 0;
        if (!WinHttpReadData(req.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
            if (!cancel.cancelled()) response.error = "Cannot read response body: " + winHttpErrorText(GetLastError());
            break;
        }
        if (read == 0) break;
        response.body.append(buffer.data(), read);
    }

    response.elapsedMs = elapsed();
    disposeRequest();
    // A close during cancellation already released the request handle.
    cancel.finish();
    return response;
}

} // namespace lite
