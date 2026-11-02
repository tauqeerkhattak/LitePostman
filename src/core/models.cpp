#include "core/models.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#include "core/url.h"

namespace lite {
namespace {

std::string lowerAscii(std::string value) {
    for (char& c : value) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return value;
}

Json keyValuesToJson(const std::vector<KeyValue>& values) {
    Json array = Json::array();
    for (const auto& value : values) {
        Json item = Json::object();
        item["key"] = value.key;
        item["value"] = value.value;
        item["enabled"] = value.enabled;
        if (value.isFile) item["isFile"] = true;
        if (value.secret) item["secret"] = true;
        array.push(std::move(item));
    }
    return array;
}

std::vector<KeyValue> keyValuesFromJson(const Json& array, bool includeSecrets = false) {
    std::vector<KeyValue> result;
    if (!array.isArray()) return result;
    for (const auto& item : array.items()) {
        if (!item.isObject()) continue;
        KeyValue value;
        value.key = item.getString("key");
        value.value = item.getString("value");
        value.enabled = item.getBool("enabled", true);
        value.isFile = item.getBool("isFile", false);
        value.secret = includeSecrets && item.getBool("secret", false);
        result.push_back(std::move(value));
    }
    return result;
}

std::string extensionMime(const std::string& path) {
    const std::size_t dot = path.rfind('.');
    const std::string ext = dot == std::string::npos ? std::string() : lowerAscii(path.substr(dot + 1));
    if (ext == "json") return "application/json";
    if (ext == "xml") return "application/xml";
    if (ext == "txt" || ext == "log") return "text/plain";
    if (ext == "html" || ext == "htm") return "text/html";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "pdf") return "application/pdf";
    if (ext == "zip") return "application/zip";
    if (ext == "csv") return "text/csv";
    return "application/octet-stream";
}

std::string fileNameOf(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string multipartBoundary() {
    static std::atomic<unsigned int> counter{0};
    const auto now = static_cast<unsigned long long>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    char buffer[80];
    std::snprintf(buffer, sizeof(buffer), "----LitePostmanBoundary%llx%x", now, counter.fetch_add(1));
    return buffer;
}

std::string readBinaryFile(const std::string& utf8Path, bool& ok) {
    std::error_code ec;
    const std::filesystem::path path = std::filesystem::u8path(utf8Path);
    if (!std::filesystem::exists(path, ec) || ec) {
        ok = false;
        return {};
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        ok = false;
        return {};
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    ok = input.good() || input.eof();
    return buffer.str();
}

std::string escapeMultipart(const std::string& value) {
    std::string out;
    for (const char c : value) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

RequestOptions optionsFromJson(const Json& json) {
    RequestOptions options;
    if (!json.isObject()) return options;
    options.followRedirects = json.getBool("followRedirects", true);
    options.ignoreTlsErrors = json.getBool("ignoreTlsErrors", false);
    options.acceptCompressed = json.getBool("acceptCompressed", true);
    options.timeoutMs = static_cast<int>(json.getInt("timeoutMs", 30000));
    if (options.timeoutMs < 100) options.timeoutMs = 100;
    options.useCustomProxy = json.getBool("useCustomProxy", false);
    options.proxy = json.getString("proxy");
    return options;
}

Json optionsToJson(const RequestOptions& options) {
    Json json = Json::object();
    json["followRedirects"] = options.followRedirects;
    json["ignoreTlsErrors"] = options.ignoreTlsErrors;
    json["acceptCompressed"] = options.acceptCompressed;
    json["timeoutMs"] = options.timeoutMs;
    json["useCustomProxy"] = options.useCustomProxy;
    json["proxy"] = options.proxy;
    return json;
}

void addNodeToJson(const CollectionNode& node, Json& array) {
    Json item = Json::object();
    item["id"] = node.id;
    item["name"] = node.name;
    item["type"] = node.type == CollectionNode::Type::Folder ? "folder" : "request";
    if (node.type == CollectionNode::Type::Request) item["request"] = requestToJson(node.request);
    if (!node.children.empty()) {
        Json children = Json::array();
        for (const auto& child : node.children) addNodeToJson(child, children);
        item["children"] = std::move(children);
    }
    array.push(std::move(item));
}

CollectionNode nodeFromJson(const Json& json) {
    CollectionNode node;
    node.id = json.getString("id");
    if (node.id.empty()) node.id = newId("node");
    node.name = json.getString("name", "Untitled");
    node.type = json.getString("type") == "request" ? CollectionNode::Type::Request : CollectionNode::Type::Folder;
    node.request = requestFromJson(json.getObject("request"));
    if (node.request.name.empty() || node.request.name == "Untitled Request") node.request.name = node.name;
    const Json children = json.getArray("children");
    for (const auto& child : children.items()) node.children.push_back(nodeFromJson(child));
    return node;
}

static CollectionNode parsePostmanItem(const Json& item) {
    CollectionNode node;
    node.id = newId("item");
    node.name = item.getString("name", "Untitled");

    if (item.find("item")) {
        node.type = CollectionNode::Type::Folder;
        const Json subItems = item.getArray("item");
        for (const auto& sub : subItems.items()) {
            node.children.push_back(parsePostmanItem(sub));
        }
    } else {
        node.type = CollectionNode::Type::Request;
        node.request.name = node.name;
        if (item.find("request")) {
            const Json req = item.getObject("request");
            node.request.method = methodNormalized(req.getString("method", "GET"));

            if (const Json* u = req.find("url")) {
                if (u->isString()) {
                    node.request.url = u->asString();
                } else if (u->isObject()) {
                    node.request.url = u->getString("raw");
                }
            }

            if (req.find("header")) {
                const Json headers = req.getArray("header");
                for (const auto& h : headers.items()) {
                    KeyValue kv;
                    kv.key = h.getString("key");
                    kv.value = h.getString("value");
                    kv.enabled = !h.getBool("disabled", false);
                    if (!kv.key.empty()) node.request.headers.push_back(kv);
                }
            }

            if (req.find("body")) {
                const Json b = req.getObject("body");
                std::string mode = b.getString("mode");
                if (mode == "raw") {
                    node.request.bodyMode = BodyMode::Raw;
                    node.request.rawBody = b.getString("raw");
                } else if (mode == "urlencoded") {
                    node.request.bodyMode = BodyMode::FormUrlEncoded;
                    const Json fields = b.getArray("urlencoded");
                    for (const auto& f : fields.items()) {
                        KeyValue kv;
                        kv.key = f.getString("key");
                        kv.value = f.getString("value");
                        kv.enabled = !f.getBool("disabled", false);
                        node.request.formFields.push_back(kv);
                    }
                }
            }

            if (req.find("auth")) {
                const Json a = req.getObject("auth");
                std::string type = a.getString("type");
                if (type == "bearer") {
                    node.request.authType = AuthType::Bearer;
                    const Json bearer = a.getArray("bearer");
                    for (const auto& bp : bearer.items()) {
                        if (bp.getString("key") == "token") node.request.token = bp.getString("value");
                    }
                } else if (type == "basic") {
                    node.request.authType = AuthType::Basic;
                    const Json basic = a.getArray("basic");
                    for (const auto& bp : basic.items()) {
                        if (bp.getString("key") == "username") node.request.username = bp.getString("value");
                        if (bp.getString("key") == "password") node.request.password = bp.getString("value");
                    }
                }
            }
        }
    }
    return node;
}

void collectHistoryRequestJson(const HistoryEntry& entry, Json& array) {
    Json item = Json::object();
    item["request"] = requestToJson(entry.request);
    item["sentAt"] = entry.sentAt;
    item["statusCode"] = entry.statusCode;
    item["elapsedMs"] = entry.elapsedMs;
    array.push(std::move(item));
}

bool containsNode(const CollectionNode& parent, const std::string& id) {
    if (parent.id == id) return true;
    for (const auto& child : parent.children) {
        if (containsNode(child, id)) return true;
    }
    return false;
}

} // namespace

std::string newId(const std::string& prefix) {
    static std::atomic<unsigned long long> counter{0};
    static std::mt19937_64 engine([] {
        std::random_device device;
        return std::mt19937_64((static_cast<unsigned long long>(device()) << 32) ^ static_cast<unsigned long long>(device()));
    }());
    const auto now = static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count());
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%s_%llx%llx", prefix.c_str(), now, static_cast<unsigned long long>(engine()) ^ counter.fetch_add(1));
    return buffer;
}

std::string methodNormalized(const std::string& method) {
    std::string upper = method;
    for (char& c : upper) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    if (upper.empty()) return "GET";
    return upper;
}

bool requestHasBody(const RequestModel& request) {
    if (request.bodyMode == BodyMode::None) return false;
    if (request.bodyMode == BodyMode::Raw) return !request.rawBody.empty();
    for (const auto& field : request.formFields) {
        if (field.enabled && !field.key.empty()) return true;
    }
    return false;
}

std::string defaultContentType(BodyMode mode, BodyLanguage language) {
    switch (mode) {
    case BodyMode::Raw:
        switch (language) {
        case BodyLanguage::Json: return "application/json";
        case BodyLanguage::Xml: return "application/xml";
        case BodyLanguage::Text: return "text/plain";
        }
        break;
    case BodyMode::FormUrlEncoded: return "application/x-www-form-urlencoded";
    default: break;
    }
    return {};
}

std::string findHeader(const std::vector<KeyValue>& headers, const std::string& name) {
    const std::string wanted = lowerAscii(name);
    for (const auto& header : headers) {
        if (header.enabled && lowerAscii(header.key) == wanted) return header.value;
    }
    return {};
}

bool setHeader(std::vector<KeyValue>& headers, const std::string& name, const std::string& value) {
    const std::string wanted = lowerAscii(name);
    bool found = false;
    for (auto& header : headers) {
        if (lowerAscii(header.key) == wanted) {
            header.value = value;
            header.enabled = true;
            found = true;
            break;
        }
    }
    if (!found) headers.push_back(KeyValue{name, value, true, false, false});
    return found;
}

void removeHeader(std::vector<KeyValue>& headers, const std::string& name) {
    const std::string wanted = lowerAscii(name);
    headers.erase(std::remove_if(headers.begin(), headers.end(), [&](const KeyValue& header) {
        return lowerAscii(header.key) == wanted;
    }), headers.end());
}

bool prepareRequest(const RequestModel& request, const VariableContext& variables, PreparedRequest& prepared, std::string* error) {
    prepared = PreparedRequest{};
    prepared.method = methodNormalized(request.method);
    const ResolutionResult urlResolution = resolveVariables(variables, request.url);
    prepared.url = urlResolution.text;

    for (const auto& header : request.headers) {
        if (!header.enabled || header.key.empty()) continue;
        KeyValue out;
        out.key = resolveVariables(variables, header.key).text;
        out.value = resolveVariables(variables, header.value).text;
        out.enabled = true;
        prepared.headers.push_back(std::move(out));
    }

    const std::string existingContentType = findHeader(prepared.headers, "Content-Type");
    const bool hasContentType = !existingContentType.empty();

    if (request.bodyMode == BodyMode::Raw) {
        prepared.body = resolveVariables(variables, request.rawBody).text;
        if (!prepared.body.empty() && !hasContentType) {
            const std::string contentType = defaultContentType(request.bodyMode, request.bodyLanguage);
            if (!contentType.empty()) prepared.headers.push_back(KeyValue{"Content-Type", contentType, true, false, false});
        }
    } else if (request.bodyMode == BodyMode::FormUrlEncoded) {
        for (const auto& field : request.formFields) {
            if (!field.enabled || field.key.empty()) continue;
            if (!prepared.body.empty()) prepared.body.push_back('&');
            prepared.body += formEncode(resolveVariables(variables, field.key).text);
            prepared.body.push_back('=');
            prepared.body += formEncode(resolveVariables(variables, field.value).text);
        }
        if (!prepared.body.empty() && !hasContentType) {
            prepared.headers.push_back(KeyValue{"Content-Type", "application/x-www-form-urlencoded", true, false, false});
        }
    } else if (request.bodyMode == BodyMode::Multipart) {
        const std::string boundary = multipartBoundary();
        for (const auto& field : request.formFields) {
            if (!field.enabled || field.key.empty()) continue;
            const std::string key = resolveVariables(variables, field.key).text;
            const std::string value = resolveVariables(variables, field.value).text;
            prepared.body += "--" + boundary + "\r\n";
            if (field.isFile) {
                bool fileOk = false;
                const std::string contents = readBinaryFile(value, fileOk);
                if (!fileOk) {
                    if (error) *error = "Cannot read file: " + value;
                    return false;
                }
                const std::string filename = fileNameOf(value);
                prepared.body += "Content-Disposition: form-data; name=\"" + escapeMultipart(key) + "\"; filename=\"" + escapeMultipart(filename) + "\"\r\n";
                prepared.body += "Content-Type: " + extensionMime(filename) + "\r\n\r\n";
                prepared.body += contents;
                prepared.body += "\r\n";
            } else {
                prepared.body += "Content-Disposition: form-data; name=\"" + escapeMultipart(key) + "\"\r\n\r\n";
                prepared.body += value;
                prepared.body += "\r\n";
            }
        }
        prepared.body += "--" + boundary + "--\r\n";
        if (!hasContentType) {
            prepared.headers.push_back(KeyValue{"Content-Type", "multipart/form-data; boundary=" + boundary, true, false, false});
        }
    }

    if (request.authType == AuthType::Basic) {
        const std::string credentials = resolveVariables(variables, request.username).text + ":" + resolveVariables(variables, request.password).text;
        setHeader(prepared.headers, "Authorization", "Basic " + base64Encode(credentials));
    } else if (request.authType == AuthType::Bearer) {
        setHeader(prepared.headers, "Authorization", "Bearer " + resolveVariables(variables, request.token).text);
    }
    return true;
}

std::string base64Encode(const std::string& input) {
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((input.size() + 2) / 3 * 4);
    std::size_t i = 0;
    while (i + 2 < input.size()) {
        const unsigned int value = (static_cast<unsigned char>(input[i]) << 16) |
                                   (static_cast<unsigned char>(input[i + 1]) << 8) |
                                   static_cast<unsigned char>(input[i + 2]);
        out.push_back(alphabet[(value >> 18) & 0x3F]);
        out.push_back(alphabet[(value >> 12) & 0x3F]);
        out.push_back(alphabet[(value >> 6) & 0x3F]);
        out.push_back(alphabet[value & 0x3F]);
        i += 3;
    }
    const std::size_t remaining = input.size() - i;
    if (remaining == 1) {
        const unsigned int value = static_cast<unsigned char>(input[i]) << 16;
        out.push_back(alphabet[(value >> 18) & 0x3F]);
        out.push_back(alphabet[(value >> 12) & 0x3F]);
        out += "==";
    } else if (remaining == 2) {
        const unsigned int value = (static_cast<unsigned char>(input[i]) << 16) |
                                   (static_cast<unsigned char>(input[i + 1]) << 8);
        out.push_back(alphabet[(value >> 18) & 0x3F]);
        out.push_back(alphabet[(value >> 12) & 0x3F]);
        out.push_back(alphabet[(value >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

bool base64Decode(const std::string& input, std::string& output) {
    output.clear();
    int value = 0;
    int bits = -8;
    for (const char c : input) {
        if (c == '=') break;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        int digit = -1;
        if (c >= 'A' && c <= 'Z') digit = c - 'A';
        else if (c >= 'a' && c <= 'z') digit = c - 'a' + 26;
        else if (c >= '0' && c <= '9') digit = c - '0' + 52;
        else if (c == '+') digit = 62;
        else if (c == '/') digit = 63;
        if (digit < 0) return false;
        value = (value << 6) | digit;
        bits += 6;
        if (bits >= 0) {
            output.push_back(static_cast<char>((value >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return true;
}

Json requestToJson(const RequestModel& request) {
    Json json = Json::object();
    json["name"] = request.name;
    json["method"] = request.method;
    json["url"] = request.url;
    json["params"] = keyValuesToJson(request.params);
    json["headers"] = keyValuesToJson(request.headers);
    Json body = Json::object();
    body["mode"] = bodyModeName(request.bodyMode);
    body["language"] = bodyLanguageName(request.bodyLanguage);
    body["raw"] = request.rawBody;
    body["fields"] = keyValuesToJson(request.formFields);
    json["body"] = std::move(body);
    Json auth = Json::object();
    auth["type"] = authTypeName(request.authType);
    auth["username"] = request.username;
    auth["password"] = request.password;
    auth["token"] = request.token;
    json["auth"] = std::move(auth);
    json["options"] = optionsToJson(request.options);
    return json;
}

RequestModel requestFromJson(const Json& json) {
    RequestModel request;
    if (!json.isObject()) return request;
    request.name = json.getString("name", "Untitled Request");
    request.method = methodNormalized(json.getString("method", "GET"));
    request.url = json.getString("url");
    request.params = keyValuesFromJson(json.getArray("params"));
    request.headers = keyValuesFromJson(json.getArray("headers"));
    const Json body = json.getObject("body");
    request.bodyMode = bodyModeFromName(body.getString("mode", "none"));
    request.bodyLanguage = bodyLanguageFromName(body.getString("language", "json"));
    request.rawBody = body.getString("raw");
    request.formFields = keyValuesFromJson(body.getArray("fields"));
    const Json auth = json.getObject("auth");
    request.authType = authTypeFromName(auth.getString("type", "none"));
    request.username = auth.getString("username");
    request.password = auth.getString("password");
    request.token = auth.getString("token");
    request.options = optionsFromJson(json.getObject("options"));
    return request;
}

Json collectionToJson(const Collection& collection) {
    Json root = Json::object();
    root["version"] = collection.version;
    Json item = Json::object();
    item["id"] = collection.id;
    item["name"] = collection.name;
    item["variables"] = keyValuesToJson(collection.variables);
    Json children = Json::array();
    for (const auto& child : collection.children) addNodeToJson(child, children);
    item["children"] = std::move(children);
    root["collection"] = std::move(item);
    return root;
}

Collection collectionFromJson(const Json& json) {
    Collection collection;
    if (!json.isObject()) {
        collection.id = newId("collection");
        collection.name = "Imported Collection";
        return collection;
    }
    if (json.find("info")) {
        const Json info = json.getObject("info");
        collection.id = newId("collection");
        collection.name = info.getString("name", "Postman Collection");
        if (json.find("item")) {
            const Json items = json.getArray("item");
            for (const auto& it : items.items()) {
                collection.children.push_back(parsePostmanItem(it));
            }
        }
        return collection;
    }
    const Json root = json.find("collection") ? json.getObject("collection") : json;
    collection.version = static_cast<int>(json.getInt("version", 1));
    collection.id = root.getString("id");
    if (collection.id.empty()) collection.id = newId("collection");
    collection.name = root.getString("name", "Collection");
    collection.variables = keyValuesFromJson(root.getArray("variables"), true);
    const Json children = root.getArray("children");
    for (const auto& child : children.items()) collection.children.push_back(nodeFromJson(child));
    return collection;
}

Json environmentsToJson(const EnvironmentStore& store) {
    Json root = Json::object();
    root["version"] = store.version;
    root["activeId"] = store.activeId;
    Json environments = Json::array();
    for (const auto& environment : store.environments) {
        Json item = Json::object();
        item["id"] = environment.id;
        item["name"] = environment.name;
        Json vars = Json::array();
        for (const auto& variable : environment.variables) {
            Json entry = Json::object();
            entry["key"] = variable.key;
            entry["value"] = variable.value;
            entry["enabled"] = variable.enabled;
            entry["secret"] = variable.secret;
            vars.push(std::move(entry));
        }
        item["variables"] = std::move(vars);
        environments.push(std::move(item));
    }
    root["environments"] = std::move(environments);
    return root;
}

EnvironmentStore environmentsFromJson(const Json& json) {
    EnvironmentStore store;
    if (!json.isObject()) return store;
    store.version = static_cast<int>(json.getInt("version", 1));
    store.activeId = json.getString("activeId");
    const Json environments = json.getArray("environments");
    for (const auto& item : environments.items()) {
        if (!item.isObject()) continue;
        Environment environment;
        environment.id = item.getString("id");
        if (environment.id.empty()) environment.id = newId("environment");
        environment.name = item.getString("name", "Environment");
        const Json vars = item.getArray("variables");
        for (const auto& entry : vars.items()) {
            if (!entry.isObject()) continue;
            EnvironmentVar variable;
            variable.key = entry.getString("key");
            variable.value = entry.getString("value");
            variable.enabled = entry.getBool("enabled", true);
            variable.secret = entry.getBool("secret", false);
            environment.variables.push_back(std::move(variable));
        }
        store.environments.push_back(std::move(environment));
    }
    return store;
}

Json settingsToJson(const Settings& settings) {
    Json root = Json::object();
    root["version"] = settings.version;
    root["theme"] = themeName(settings.theme);
    Json window = Json::object();
    window["x"] = settings.windowX;
    window["y"] = settings.windowY;
    window["width"] = settings.windowWidth;
    window["height"] = settings.windowHeight;
    window["maximized"] = settings.maximized;
    root["window"] = std::move(window);
    root["sidebarWidth"] = settings.sidebarWidth;
    root["responseHeight"] = settings.responseHeight;
    root["activeCollectionId"] = settings.activeCollectionId;
    root["activeEnvironmentId"] = settings.activeEnvironmentId;
    root["globalVariables"] = keyValuesToJson(settings.globalVariables);
    root["activeTab"] = settings.activeTab;
    Json tabs = Json::array();
    for (const auto& tab : settings.tabs) {
        Json item = Json::object();
        item["request"] = requestToJson(tab.request);
        item["collectionId"] = tab.collectionId;
        item["nodeId"] = tab.nodeId;
        item["dirty"] = tab.dirty;
        tabs.push(std::move(item));
    }
    root["tabs"] = std::move(tabs);
    return root;
}

Settings settingsFromJson(const Json& json) {
    Settings settings;
    if (!json.isObject()) return settings;
    settings.version = static_cast<int>(json.getInt("version", 1));
    settings.theme = themeFromName(json.getString("theme", "system"));
    const Json window = json.getObject("window");
    settings.windowX = static_cast<int>(window.getInt("x", -1));
    settings.windowY = static_cast<int>(window.getInt("y", -1));
    settings.windowWidth = static_cast<int>(window.getInt("width", 1180));
    settings.windowHeight = static_cast<int>(window.getInt("height", 760));
    settings.maximized = window.getBool("maximized", false);
    settings.sidebarWidth = static_cast<int>(json.getInt("sidebarWidth", 250));
    settings.responseHeight = static_cast<int>(json.getInt("responseHeight", 270));
    settings.activeCollectionId = json.getString("activeCollectionId");
    settings.activeEnvironmentId = json.getString("activeEnvironmentId");
    settings.globalVariables = keyValuesFromJson(json.getArray("globalVariables"), true);
    settings.activeTab = static_cast<int>(json.getInt("activeTab", 0));
    const Json tabs = json.getArray("tabs");
    for (const auto& item : tabs.items()) {
        if (!item.isObject()) continue;
        TabState tab;
        tab.request = requestFromJson(item.getObject("request"));
        tab.collectionId = item.getString("collectionId");
        tab.nodeId = item.getString("nodeId");
        tab.dirty = item.getBool("dirty", false);
        settings.tabs.push_back(std::move(tab));
    }
    return settings;
}

Json historyToJson(const std::vector<HistoryEntry>& history) {
    Json root = Json::object();
    root["version"] = 1;
    Json entries = Json::array();
    for (const auto& entry : history) collectHistoryRequestJson(entry, entries);
    root["entries"] = std::move(entries);
    return root;
}

std::vector<HistoryEntry> historyFromJson(const Json& json) {
    std::vector<HistoryEntry> history;
    if (!json.isObject()) return history;
    const Json entries = json.getArray("entries");
    for (const auto& item : entries.items()) {
        if (!item.isObject()) continue;
        HistoryEntry entry;
        entry.request = requestFromJson(item.getObject("request"));
        entry.sentAt = item.getInt("sentAt", 0);
        entry.statusCode = static_cast<int>(item.getInt("statusCode", 0));
        entry.elapsedMs = static_cast<int>(item.getInt("elapsedMs", 0));
        history.push_back(std::move(entry));
    }
    return history;
}

CollectionNode* findNode(Collection& collection, const std::string& id) {
    struct Finder {
        static CollectionNode* visit(std::vector<CollectionNode>& nodes, const std::string& wanted) {
            for (auto& node : nodes) {
                if (node.id == wanted) return &node;
                if (CollectionNode* found = visit(node.children, wanted)) return found;
            }
            return nullptr;
        }
    };
    return Finder::visit(collection.children, id);
}

const CollectionNode* findNode(const Collection& collection, const std::string& id) {
    struct Finder {
        static const CollectionNode* visit(const std::vector<CollectionNode>& nodes, const std::string& wanted) {
            for (const auto& node : nodes) {
                if (node.id == wanted) return &node;
                if (const CollectionNode* found = visit(node.children, wanted)) return found;
            }
            return nullptr;
        }
    };
    return Finder::visit(collection.children, id);
}

CollectionNode* findParent(Collection& collection, const std::string& id) {
    struct Finder {
        static CollectionNode* visit(std::vector<CollectionNode>& nodes, const std::string& wanted) {
            for (auto& node : nodes) {
                for (auto& child : node.children) {
                    if (child.id == wanted) return &node;
                }
                if (CollectionNode* found = visit(node.children, wanted)) return found;
            }
            return nullptr;
        }
    };
    return Finder::visit(collection.children, id);
}

bool removeNode(Collection& collection, const std::string& id) {
    struct Remover {
        static bool visit(std::vector<CollectionNode>& nodes, const std::string& wanted) {
            for (auto it = nodes.begin(); it != nodes.end(); ++it) {
                if (it->id == wanted) {
                    nodes.erase(it);
                    return true;
                }
                if (visit(it->children, wanted)) return true;
            }
            return false;
        }
    };
    return Remover::visit(collection.children, id);
}

CollectionNode* insertNode(Collection& collection, const std::string& parentId, CollectionNode node, int index) {
    std::vector<CollectionNode>* target = &collection.children;
    if (!parentId.empty()) {
        CollectionNode* parent = findNode(collection, parentId);
        if (!parent || parent->type != CollectionNode::Type::Folder) return nullptr;
        target = &parent->children;
    }
    if (index < 0 || static_cast<std::size_t>(index) > target->size()) index = static_cast<int>(target->size());
    auto it = target->begin() + index;
    auto inserted = target->insert(it, std::move(node));
    return &*inserted;
}

bool moveNode(Collection& collection, const std::string& nodeId, const std::string& newParentId, int index) {
    CollectionNode* node = findNode(collection, nodeId);
    if (!node) return false;
    if (newParentId == nodeId) return false;
    if (!newParentId.empty()) {
        CollectionNode* parent = findNode(collection, newParentId);
        if (!parent || parent->type != CollectionNode::Type::Folder) return false;
        for (const auto& child : node->children) {
            if (containsNode(child, newParentId)) return false;
        }
    }
    CollectionNode copy = *node;
    if (!removeNode(collection, nodeId)) return false;
    return insertNode(collection, newParentId, std::move(copy), index) != nullptr;
}

CollectionNode duplicateNode(const CollectionNode& node) {
    CollectionNode copy = node;
    copy.id = newId(node.type == CollectionNode::Type::Folder ? "folder" : "request");
    copy.name += " copy";
    copy.children.clear();
    for (const auto& child : node.children) copy.children.push_back(duplicateNode(child));
    return copy;
}

VariableMap variablesToMap(const std::vector<KeyValue>& values) {
    VariableMap map;
    for (const auto& value : values) {
        if (!value.enabled || value.key.empty()) continue;
        map[value.key] = Variable{value.value, value.secret};
    }
    return map;
}

VariableContext makeVariableContext(const VariableMap& global, const VariableMap& collection, const EnvironmentStore& environments) {
    VariableContext context;
    context.environment = environments.activeVariables();
    context.collection = collection;
    context.global = global;
    if (const Environment* active = environments.active()) context.activeEnvironmentName = active->name;
    return context;
}

const Environment* EnvironmentStore::active() const {
    for (const auto& environment : environments) {
        if (environment.id == activeId) return &environment;
    }
    return nullptr;
}

Environment* EnvironmentStore::active() {
    for (auto& environment : environments) {
        if (environment.id == activeId) return &environment;
    }
    return nullptr;
}

VariableMap EnvironmentStore::activeVariables() const {
    return variablesFor(activeId);
}

VariableMap EnvironmentStore::variablesFor(const std::string& environmentId) const {
    VariableMap map;
    for (const auto& environment : environments) {
        if (environment.id != environmentId) continue;
        for (const auto& variable : environment.variables) {
            if (!variable.enabled || variable.key.empty()) continue;
            map[variable.key] = Variable{variable.value, variable.secret};
        }
        break;
    }
    return map;
}

std::string themeName(ThemeMode theme) {
    switch (theme) {
    case ThemeMode::Light: return "light";
    case ThemeMode::Dark: return "dark";
    default: return "system";
    }
}

ThemeMode themeFromName(const std::string& name) {
    if (name == "light") return ThemeMode::Light;
    if (name == "dark") return ThemeMode::Dark;
    return ThemeMode::System;
}

std::string bodyModeName(BodyMode mode) {
    switch (mode) {
    case BodyMode::Raw: return "raw";
    case BodyMode::FormUrlEncoded: return "urlencoded";
    case BodyMode::Multipart: return "multipart";
    default: return "none";
    }
}

BodyMode bodyModeFromName(const std::string& name) {
    if (name == "raw") return BodyMode::Raw;
    if (name == "urlencoded" || name == "form") return BodyMode::FormUrlEncoded;
    if (name == "multipart" || name == "form-data") return BodyMode::Multipart;
    return BodyMode::None;
}

std::string bodyLanguageName(BodyLanguage language) {
    switch (language) {
    case BodyLanguage::Text: return "text";
    case BodyLanguage::Xml: return "xml";
    default: return "json";
    }
}

BodyLanguage bodyLanguageFromName(const std::string& name) {
    if (name == "text") return BodyLanguage::Text;
    if (name == "xml") return BodyLanguage::Xml;
    return BodyLanguage::Json;
}

std::string authTypeName(AuthType type) {
    switch (type) {
    case AuthType::Basic: return "basic";
    case AuthType::Bearer: return "bearer";
    default: return "none";
    }
}

AuthType authTypeFromName(const std::string& name) {
    if (name == "basic") return AuthType::Basic;
    if (name == "bearer") return AuthType::Bearer;
    return AuthType::None;
}

} // namespace lite
