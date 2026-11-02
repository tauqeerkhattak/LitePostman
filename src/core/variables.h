#pragma once

#include <map>
#include <string>
#include <vector>

namespace lite {

struct Variable {
    std::string value;
    bool secret = false;
};

using VariableMap = std::map<std::string, Variable>;

enum class VariableScope { None, Environment, Collection, Global, Dynamic };

const char* scopeName(VariableScope scope);

struct ResolvedVariable {
    bool found = false;
    std::string value;
    VariableScope scope = VariableScope::None;
    bool secret = false;
};

struct VariableContext {
    VariableMap environment;
    VariableMap collection;
    VariableMap global;
    std::string activeEnvironmentName;
};

struct VariableUse {
    std::string name;
    VariableScope scope = VariableScope::None;
    bool secret = false;
};

struct ResolutionResult {
    std::string text;
    std::vector<std::string> unresolved;
    std::vector<VariableUse> used;
    bool usedSecret = false;
};

ResolvedVariable resolveVariable(const VariableContext& context, const std::string& name, int depth = 0);
ResolutionResult resolveVariables(const VariableContext& context, const std::string& text);

// Finds the {{name}} token containing a caret/character offset. Used for the
// variable preview tooltip in the request editor.
bool variableAt(const std::string& text, std::size_t offset, std::string& raw, ResolvedVariable& resolved, const VariableContext& context);

std::string dynamicVariable(const std::string& name);
bool isDynamicVariable(const std::string& name);

} // namespace lite
