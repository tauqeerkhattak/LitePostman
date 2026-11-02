#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/json.h"
#include "core/variables.h"

namespace lite {

enum class BodyMode { None, Raw, FormUrlEncoded, Multipart };
enum class BodyLanguage { Json, Text, Xml };
enum class AuthType { None, Basic, Bearer };
enum class ThemeMode { System, Light, Dark };

struct KeyValue {
    std::string key;
    std::string value;
    bool enabled = true;
    bool isFile = false; // multipart file field when true
    bool secret = false; // used by environment/collection variables
};

struct RequestOptions {
    bool followRedirects = true;
    bool ignoreTlsErrors = false;
    bool acceptCompressed = true;
    int timeoutMs = 30000;
    bool useCustomProxy = false;
    std::string proxy;
};

struct RequestModel {
    std::string name = "Untitled Request";
    std::string method = "GET";
    std::string url;
    std::vector<KeyValue> params;
    std::vector<KeyValue> headers;
    BodyMode bodyMode = BodyMode::None;
    BodyLanguage bodyLanguage = BodyLanguage::Json;
    std::string rawBody;
    std::vector<KeyValue> formFields;
    AuthType authType = AuthType::None;
    std::string username;
    std::string password;
    std::string token;
    RequestOptions options;
};

struct CollectionNode {
    enum class Type { Folder, Request };
    std::string id;
    std::string name;
    Type type = Type::Folder;
    RequestModel request;
    std::vector<CollectionNode> children;
};

struct Collection {
    int version = 1;
    std::string id;
    std::string name;
    std::vector<KeyValue> variables;
    std::vector<CollectionNode> children;
};

struct TabState {
    RequestModel request;
    std::string collectionId;
    std::string nodeId;
    bool dirty = false;
};

struct HistoryEntry {
    RequestModel request;
    std::int64_t sentAt = 0;
    int statusCode = 0;
    int elapsedMs = 0;
};

struct EnvironmentVar {
    std::string key;
    std::string value;
    bool enabled = true;
    bool secret = false;
};

struct Environment {
    std::string id;
    std::string name;
    std::vector<EnvironmentVar> variables;
};

struct EnvironmentStore {
    int version = 1;
    std::string activeId;
    std::vector<Environment> environments;

    const Environment* active() const;
    Environment* active();
    VariableMap activeVariables() const;
    VariableMap variablesFor(const std::string& environmentId) const;
};

struct Settings {
    int version = 1;
    ThemeMode theme = ThemeMode::System;
    int windowX = -1; // -1 means "let Windows choose"
    int windowY = -1;
    int windowWidth = 1180;
    int windowHeight = 760;
    bool maximized = false;
    int sidebarWidth = 250;
    int responseHeight = 270;
    std::string activeCollectionId;
    std::string activeEnvironmentId;
    std::vector<KeyValue> globalVariables;
    std::vector<TabState> tabs;
    int activeTab = 0;
};

struct PreparedRequest {
    std::string method;
    std::string url;
    std::string body;
    std::vector<KeyValue> headers; // enabled only, variables resolved
};

std::string newId(const std::string& prefix);
std::string methodNormalized(const std::string& method);
bool requestHasBody(const RequestModel& request);
std::string defaultContentType(BodyMode mode, BodyLanguage language);
std::string findHeader(const std::vector<KeyValue>& headers, const std::string& name);
bool setHeader(std::vector<KeyValue>& headers, const std::string& name, const std::string& value);
void removeHeader(std::vector<KeyValue>& headers, const std::string& name);

bool prepareRequest(const RequestModel& request, const VariableContext& variables, PreparedRequest& prepared, std::string* error);

std::string base64Encode(const std::string& input);
bool base64Decode(const std::string& input, std::string& output);

Json requestToJson(const RequestModel& request);
RequestModel requestFromJson(const Json& json);
Json collectionToJson(const Collection& collection);
Collection collectionFromJson(const Json& json);
Json environmentsToJson(const EnvironmentStore& store);
EnvironmentStore environmentsFromJson(const Json& json);
Json settingsToJson(const Settings& settings);
Settings settingsFromJson(const Json& json);
Json historyToJson(const std::vector<HistoryEntry>& history);
std::vector<HistoryEntry> historyFromJson(const Json& json);

CollectionNode* findNode(Collection& collection, const std::string& id);
const CollectionNode* findNode(const Collection& collection, const std::string& id);
CollectionNode* findParent(Collection& collection, const std::string& id);
bool removeNode(Collection& collection, const std::string& id);
CollectionNode* insertNode(Collection& collection, const std::string& parentId, CollectionNode node, int index = -1);
bool moveNode(Collection& collection, const std::string& nodeId, const std::string& newParentId, int index = -1);
CollectionNode duplicateNode(const CollectionNode& node);

VariableMap variablesToMap(const std::vector<KeyValue>& values);
VariableContext makeVariableContext(const VariableMap& global, const VariableMap& collection, const EnvironmentStore& environments);
std::string themeName(ThemeMode theme);
ThemeMode themeFromName(const std::string& name);
std::string bodyModeName(BodyMode mode);
BodyMode bodyModeFromName(const std::string& name);
std::string bodyLanguageName(BodyLanguage language);
BodyLanguage bodyLanguageFromName(const std::string& name);
std::string authTypeName(AuthType type);
AuthType authTypeFromName(const std::string& name);

} // namespace lite
