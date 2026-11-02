#include "core/variables.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <random>

namespace lite {
namespace {

std::mt19937_64& rng() {
    static std::mt19937_64 engine([] {
        std::random_device device;
        const auto now = static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
        std::seed_seq seed{device(), device(), static_cast<unsigned int>(now), static_cast<unsigned int>(now >> 32)};
        return std::mt19937_64(seed);
    }());
    return engine;
}

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' || text[begin] == '\n')) ++begin;
    std::size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' || text[end - 1] == '\n')) --end;
    return text.substr(begin, end - begin);
}

bool findMapped(const VariableMap& map, const std::string& name, ResolvedVariable& result) {
    const auto it = map.find(name);
    if (it == map.end()) return false;
    result.found = true;
    result.value = it->second.value;
    result.secret = it->second.secret;
    return true;
}

void resolveTextInternal(const VariableContext& context, const std::string& text, int depth, ResolutionResult& result) {
    if (depth > 8) {
        result.text += text;
        return;
    }
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t open = text.find("{{", pos);
        if (open == std::string::npos) {
            result.text.append(text, pos, std::string::npos);
            break;
        }
        result.text.append(text, pos, open - pos);
        const std::size_t close = text.find("}}", open + 2);
        if (close == std::string::npos) {
            result.text.append(text, open, std::string::npos);
            break;
        }
        const std::string name = trim(text.substr(open + 2, close - open - 2));
        ResolvedVariable variable;
        if (!name.empty() && name[0] != '{') variable = resolveVariable(context, name);
        if (variable.found) {
            if (variable.value.find("{{") != std::string::npos) {
                resolveTextInternal(context, variable.value, depth + 1, result);
            } else {
                result.text += variable.value;
            }
            result.used.push_back({name, variable.scope, variable.secret});
            if (variable.secret) result.usedSecret = true;
        } else {
            result.text.append(text, open, close + 2 - open);
            result.unresolved.push_back(name);
        }
        pos = close + 2;
    }
}

} // namespace

const char* scopeName(VariableScope scope) {
    switch (scope) {
    case VariableScope::Environment: return "Environment";
    case VariableScope::Collection: return "Collection";
    case VariableScope::Global: return "Global";
    case VariableScope::Dynamic: return "Dynamic";
    default: return "Unresolved";
    }
}

ResolvedVariable resolveVariable(const VariableContext& context, const std::string& rawName, int depth) {
    const std::string name = trim(rawName);
    ResolvedVariable result;
    if (depth > 8) return result;
    if (findMapped(context.environment, name, result)) {
        result.scope = VariableScope::Environment;
        return result;
    }
    if (findMapped(context.collection, name, result)) {
        result.scope = VariableScope::Collection;
        return result;
    }
    if (findMapped(context.global, name, result)) {
        result.scope = VariableScope::Global;
        return result;
    }
    if (name.size() > 1 && name[0] == '$') {
        result.found = true;
        result.scope = VariableScope::Dynamic;
        result.value = dynamicVariable(name);
        return result;
    }
    result.found = false;
    return result;
}

ResolutionResult resolveVariables(const VariableContext& context, const std::string& text) {
    ResolutionResult result;
    result.text.reserve(text.size());
    resolveTextInternal(context, text, 0, result);
    return result;
}

bool variableAt(const std::string& text, std::size_t offset, std::string& raw, ResolvedVariable& resolved, const VariableContext& context) {
    if (offset > text.size()) offset = text.size();
    const std::size_t closeSearch = text.find("}}", offset);
    if (closeSearch == std::string::npos) return false;
    const std::size_t openBefore = text.rfind("{{", closeSearch);
    if (openBefore == std::string::npos || openBefore >= offset) return false;
    if (offset < openBefore + 2 || offset > closeSearch) return false;
    raw = text.substr(openBefore, closeSearch + 2 - openBefore);
    const std::string name = trim(text.substr(openBefore + 2, closeSearch - openBefore - 2));
    if (name.empty()) return false;
    resolved = resolveVariable(context, name);
    return true;
}

std::string dynamicVariable(const std::string& name) {
    const auto now = std::chrono::system_clock::now();
    if (name == "$timestamp") {
        return std::to_string(static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count()));
    }
    if (name == "$isoTimestamp") {
        const std::time_t value = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#if defined(_WIN32) || defined(_MSC_VER)
        if (gmtime_s(&tm, &value) != 0) {
            return std::to_string(static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count()));
        }
#else
        if (gmtime_r(&value, &tm) == nullptr) {
            return std::to_string(static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count()));
        }
#endif
        char buffer[40];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
        return buffer;
    }
    if (name == "$guid") {
        const std::uint64_t high = rng()();
        const std::uint64_t low = rng()();
        char buffer[40];
        std::snprintf(buffer, sizeof(buffer), "%08x-%04x-4%03x-%04x-%012llx",
                      static_cast<unsigned int>(high >> 32),
                      static_cast<unsigned int>((high >> 16) & 0xFFFF),
                      static_cast<unsigned int>(high & 0x0FFF),
                      static_cast<unsigned int>(0x8000u | ((low >> 48) & 0x3FFF)),
                      static_cast<unsigned long long>(low & 0xFFFFFFFFFFFFull));
        return buffer;
    }
    if (name == "$randomInt") {
        std::uniform_int_distribution<int> distribution(0, 9999);
        return std::to_string(distribution(rng()));
    }
    return "{{" + name + "}}";
}

bool isDynamicVariable(const std::string& name) {
    return name == "$timestamp" || name == "$isoTimestamp" || name == "$guid" || name == "$randomInt";
}

} // namespace lite
