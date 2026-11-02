#include "core/curl_generator.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "core/url.h"

namespace lite {
namespace {

std::string lowerAscii(std::string value) {
    for (char& c : value) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return value;
}

std::string bashQuote(const std::string& value) {
    std::string out = "'";
    for (const char c : value) {
        if (c == '\'') out += "'\\''";
        else out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

std::string cmdQuote(const std::string& value) {
    // Quote for the MSVC/curl command-line parser and cmd.exe. Backslashes are
    // only special immediately before a quote, so runs are doubled there.
    std::string out = "\"";
    std::size_t i = 0;
    while (i < value.size()) {
        std::size_t backslashes = 0;
        while (i < value.size() && value[i] == '\\') {
            ++backslashes;
            ++i;
        }
        if (i < value.size() && value[i] == '"') {
            out.append(backslashes * 2, '\\');
            out += "\\\"";
            ++i;
        } else {
            out.append(backslashes, '\\');
            if (i < value.size()) {
                out.push_back(value[i]);
                ++i;
            }
        }
    }
    // Trailing backslashes must be doubled so they don't escape the closing quote.
    if (!value.empty() && value.back() == '\\') out.append(value.size() - value.find_last_not_of('\\'), '\\');
    out.push_back('"');
    return out;
}

std::string powershellQuote(const std::string& value) {
    std::string out = "'";
    for (const char c : value) {
        if (c == '\'') out += "''";
        else out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

std::string quoteFor(CurlTarget target, const std::string& value) {
    switch (target) {
    case CurlTarget::Cmd: return cmdQuote(value);
    case CurlTarget::PowerShell: return powershellQuote(value);
    default: return bashQuote(value);
    }
}

std::string continuation(CurlTarget target) {
    switch (target) {
    case CurlTarget::Cmd: return " ^\n  ";
    case CurlTarget::PowerShell: return " `\n  ";
    default: return " \\\n  ";
    }
}

std::string executable(CurlTarget target) {
    return target == CurlTarget::Bash ? "curl" : "curl.exe";
}

struct Resolver {
    const VariableContext& variables;
    bool enabled = false;
    CurlExportResult& result;
    std::set<std::string> unresolved;

    std::string operator()(const std::string& value) {
        if (!enabled) return value;
        const ResolutionResult resolved = resolveVariables(variables, value);
        if (resolved.usedSecret) result.containsSecret = true;
        for (const auto& name : resolved.unresolved) unresolved.insert(name);
        return resolved.text;
    }

    void finishWarnings() {
        for (const auto& name : unresolved) {
            result.warnings.push_back("Unresolved variable: {{" + name + "}}");
        }
        if (result.containsSecret) {
            result.warnings.push_back("The command contains resolved secret variable values. Share it carefully.");
        }
    }
};

std::string timeoutValue(int timeoutMs) {
    if (timeoutMs <= 0) return {};
    char buffer[40];
    const double seconds = static_cast<double>(timeoutMs) / 1000.0;
    std::snprintf(buffer, sizeof(buffer), "%.3f", seconds);
    std::string text = buffer;
    while (!text.empty() && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
    return text;
}

bool headerExists(const std::vector<KeyValue>& headers, const std::string& name) {
    const std::string wanted = lowerAscii(name);
    for (const auto& header : headers) {
        if (header.enabled && lowerAscii(header.key) == wanted) return true;
    }
    return false;
}

} // namespace

const char* curlTargetName(CurlTarget target) {
    switch (target) {
    case CurlTarget::Cmd: return "cmd";
    case CurlTarget::PowerShell: return "powershell";
    default: return "bash";
    }
}

CurlExportResult generateCurl(const RequestModel& request, const VariableContext& variables, const CurlExportOptions& options) {
    CurlExportResult result;
    Resolver resolve{variables, options.resolveVariables, result, {}};
    const std::string method = methodNormalized(request.method);

    std::vector<std::string> parts;
    if (method != "GET") parts.push_back("-X " + method);
    parts.push_back(quoteFor(options.target, resolve(request.url)));

    const auto appendHeader = [&](const std::string& name, const std::string& value) {
        const std::string text = resolve(name) + ": " + resolve(value);
        parts.push_back("-H " + quoteFor(options.target, text));
    };

    for (const auto& header : request.headers) {
        if (!header.enabled || header.key.empty()) continue;
        if (lowerAscii(header.key) == "authorization" && request.authType != AuthType::None) continue;
        if (request.bodyMode == BodyMode::Multipart && lowerAscii(header.key) == "content-type") continue;
        appendHeader(header.key, header.value);
    }

    if (request.authType == AuthType::Basic) {
        parts.push_back("-u " + quoteFor(options.target, resolve(request.username) + ":" + resolve(request.password)));
    } else if (request.authType == AuthType::Bearer) {
        appendHeader("Authorization", "Bearer " + resolve(request.token));
    }

    if (request.bodyMode == BodyMode::Raw && !request.rawBody.empty()) {
        if (!headerExists(request.headers, "Content-Type")) {
            const std::string contentType = defaultContentType(request.bodyMode, request.bodyLanguage);
            if (!contentType.empty()) appendHeader("Content-Type", contentType);
        }
        parts.push_back("--data-raw " + quoteFor(options.target, resolve(request.rawBody)));
    } else if (request.bodyMode == BodyMode::FormUrlEncoded) {
        bool hasField = false;
        for (const auto& field : request.formFields) {
            if (!field.enabled || field.key.empty()) continue;
            hasField = true;
            parts.push_back("--data-urlencode " + quoteFor(options.target, resolve(field.key) + "=" + resolve(field.value)));
        }
        if (hasField && !headerExists(request.headers, "Content-Type")) {
            appendHeader("Content-Type", "application/x-www-form-urlencoded");
        }
    } else if (request.bodyMode == BodyMode::Multipart) {
        for (const auto& field : request.formFields) {
            if (!field.enabled || field.key.empty()) continue;
            std::string value = resolve(field.key) + "=";
            if (field.isFile) value += "@";
            value += resolve(field.value);
            parts.push_back("-F " + quoteFor(options.target, value));
        }
    }

    if (request.options.ignoreTlsErrors) parts.push_back("-k");
    if (request.options.followRedirects) parts.push_back("-L");
    const std::string timeout = timeoutValue(request.options.timeoutMs);
    if (!timeout.empty()) parts.push_back("--max-time " + timeout);
    if (request.options.useCustomProxy && !request.options.proxy.empty()) {
        parts.push_back("--proxy " + quoteFor(options.target, resolve(request.options.proxy)));
    }
    if (request.options.acceptCompressed) parts.push_back("--compressed");

    std::string command = executable(options.target);
    for (std::size_t index = 0; index < parts.size(); ++index) {
        command += " " + parts[index];
        if (index + 1 < parts.size()) command += continuation(options.target);
    }
    result.command = command;
    resolve.finishWarnings();
    return result;
}

std::string generateCurlScript(const std::vector<RequestModel>& requests, const VariableContext& variables, const CurlExportOptions& options) {
    std::string script;
    if (options.target == CurlTarget::Cmd) {
        script += "@echo off\r\n";
        script += "REM LitePostman cURL export\r\n\r\n";
    } else if (options.target == CurlTarget::PowerShell) {
        script += "# LitePostman cURL export\n\n";
    } else {
        script += "#!/usr/bin/env bash\n\n";
    }
    for (const auto& request : requests) {
        const CurlExportResult generated = generateCurl(request, variables, options);
        if (options.target == CurlTarget::Cmd) script += "REM " + request.name + "\r\n";
        else script += "# " + request.name + "\n";
        script += generated.command;
        script += options.target == CurlTarget::Cmd ? "\r\n\r\n" : "\n\n";
    }
    return script;
}

} // namespace lite
