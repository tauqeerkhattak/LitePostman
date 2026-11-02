#pragma once

#include <string>
#include <vector>

#include "core/models.h"

namespace lite {

enum class CurlTarget { Bash, Cmd, PowerShell };

struct CurlExportOptions {
    CurlTarget target = CurlTarget::Bash;
    bool resolveVariables = false;
};

struct CurlExportResult {
    std::string command;
    std::vector<std::string> warnings;
    bool containsSecret = false;
};

const char* curlTargetName(CurlTarget target);
CurlExportResult generateCurl(const RequestModel& request, const VariableContext& variables,
                              const CurlExportOptions& options);
std::string generateCurlScript(const std::vector<RequestModel>& requests, const VariableContext& variables,
                               const CurlExportOptions& options);

} // namespace lite
