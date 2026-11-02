#pragma once

#include <string>
#include <vector>

#include "core/models.h"

namespace lite {

enum class CurlShell { Auto, Bash, Cmd, PowerShell };

struct CurlImportResult {
    bool ok = false;
    RequestModel request;
    CurlShell shell = CurlShell::Bash;
    std::vector<std::string> unknownFlags;
    std::vector<std::string> warnings;
    std::string error;
};

struct VariableSuggestion {
    std::string name;
    std::string value;
    std::string description;
    enum class Target { BaseUrl, BearerToken, ApiKeyHeader } target = Target::BaseUrl;
    std::string headerName;
    bool secret = false;
    bool accepted = true;
};

CurlShell detectCurlShell(const std::string& command);
const char* curlShellName(CurlShell shell);
CurlImportResult parseCurl(const std::string& command, CurlShell shell = CurlShell::Auto);
std::vector<VariableSuggestion> suggestCurlVariables(const RequestModel& request, const EnvironmentStore& environments);
void applyVariableSuggestions(RequestModel& request, EnvironmentStore& environments,
                              const std::vector<const VariableSuggestion*>& accepted,
                              std::string* error = nullptr);

} // namespace lite
