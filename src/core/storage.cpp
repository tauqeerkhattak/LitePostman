#include "core/storage.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace lite {
namespace {

std::string wideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string out(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length, nullptr, nullptr);
    return out;
}

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring out(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length);
    return out;
}

std::string windowsError(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    if (length == 0 || !buffer) return "Windows error " + std::to_string(code);
    std::string text = wideToUtf8(std::wstring(buffer, length));
    LocalFree(buffer);
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) text.pop_back();
    return text;
}

std::string pathToUtf8(const std::filesystem::path& path) {
    return wideToUtf8(path.wstring());
}

std::string sanitizeFilePart(const std::string& value) {
    std::string out;
    for (const char c : value) {
        const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        out.push_back(safe ? c : '_');
    }
    if (out.empty()) out = "unnamed";
    if (out.size() > 80) out.resize(80);
    return out;
}

std::string readAll(const std::filesystem::path& path, bool& ok) {
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

} // namespace

Storage::Storage(std::string dataDirectory) : directory_(std::move(dataDirectory)) {
    if (!directory_.empty()) return;
    wchar_t buffer[MAX_PATH * 2];
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
    if (length > 0 && length < std::size(buffer)) {
        directory_ = wideToUtf8(std::wstring(buffer, length)) + "\\LitePostman";
    } else {
        directory_ = "LitePostmanData";
    }
}

bool Storage::initialize(std::string* error) {
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::u8path(directory_);
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        if (error) *error = "Cannot create data directory: " + ec.message();
        return false;
    }
    initialized_ = true;
    return true;
}

std::string Storage::collectionPath(const std::string& id) const {
    return directory_ + "\\collection_" + sanitizeFilePart(id) + ".json";
}

std::string Storage::environmentsPath() const { return directory_ + "\\environments.json"; }
std::string Storage::settingsPath() const { return directory_ + "\\settings.json"; }
std::string Storage::historyPath() const { return directory_ + "\\history.json"; }

std::string Storage::readFile(const std::string& path, bool& ok) const {
    return readAll(std::filesystem::u8path(path), ok);
}

bool Storage::writeAtomic(const std::string& path, const std::string& data, std::string* error) {
    static std::atomic<unsigned int> counter{0};
    const std::string temp = path + ".tmp." + std::to_string(GetCurrentProcessId()) + "." + std::to_string(counter.fetch_add(1));
    const std::wstring tempWide = utf8ToWide(temp);
    const std::wstring targetWide = utf8ToWide(path);
    if (tempWide.empty() || targetWide.empty()) {
        if (error) *error = "Invalid data file path";
        return false;
    }
    HANDLE file = CreateFileW(tempWide.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (error) *error = "Cannot create temporary file: " + windowsError(GetLastError());
        return false;
    }
    bool ok = true;
    std::size_t offset = 0;
    while (offset < data.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(data.size() - offset, 1024u * 1024u));
        DWORD written = 0;
        if (!WriteFile(file, data.data() + offset, chunk, &written, nullptr) || written == 0) {
            ok = false;
            break;
        }
        offset += written;
    }
    if (ok) {
        ok = FlushFileBuffers(file) != FALSE;
    }
    CloseHandle(file);
    if (!ok) {
        DeleteFileW(tempWide.c_str());
        if (error) *error = "Cannot write data file: " + windowsError(GetLastError());
        return false;
    }
    if (!MoveFileExW(tempWide.c_str(), targetWide.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD code = GetLastError();
        DeleteFileW(tempWide.c_str());
        if (error) *error = "Cannot replace data file: " + windowsError(code);
        return false;
    }
    return true;
}

std::vector<Collection> Storage::loadCollections(std::vector<std::string>& warnings) {
    std::vector<Collection> collections;
    if (!initialized_ && !initialize(nullptr)) return collections;
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::u8path(directory_);
    std::vector<std::filesystem::path> files;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const auto& path = it->path();
        if (!it->is_regular_file(ec)) continue;
        const std::string name = pathToUtf8(path.filename());
        if (name.rfind("collection_", 0) == 0 && path.extension() == ".json") files.push_back(path);
    }
    if (ec) warnings.push_back("Cannot list collections: " + ec.message());
    std::sort(files.begin(), files.end());
    for (const auto& path : files) {
        bool ok = false;
        const std::string text = readAll(path, ok);
        if (!ok) {
            warnings.push_back("Cannot read " + pathToUtf8(path.filename()));
            continue;
        }
        std::string parseError;
        const Json root = Json::parse(text, &parseError);
        if (!parseError.empty() || !root.isObject()) {
            warnings.push_back("Skipping corrupt collection " + pathToUtf8(path.filename()) + ": " + parseError);
            continue;
        }
        const int version = static_cast<int>(root.getInt("version", 1));
        if (version > 1) {
            warnings.push_back("Skipping " + pathToUtf8(path.filename()) + ": file was created by a newer version");
            continue;
        }
        collections.push_back(collectionFromJson(root));
    }
    return collections;
}

bool Storage::saveCollection(const Collection& collection, std::string* error) {
    if (!initialized_ && !initialize(error)) return false;
    return writeAtomic(collectionPath(collection.id), collectionToJson(collection).dump(2) + "\n", error);
}

bool Storage::deleteCollection(const std::string& id, std::string* error) {
    const std::wstring path = utf8ToWide(collectionPath(id));
    if (!DeleteFileW(path.c_str())) {
        const DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND) return true;
        if (error) *error = "Cannot delete collection: " + windowsError(code);
        return false;
    }
    return true;
}

EnvironmentStore Storage::loadEnvironments(std::vector<std::string>& warnings) {
    EnvironmentStore defaults;
    std::error_code ec;
    if (!std::filesystem::exists(std::filesystem::u8path(environmentsPath()), ec)) return defaults;
    bool ok = false;
    const std::string text = readFile(environmentsPath(), ok);
    if (!ok) {
        warnings.push_back("Cannot read environments.json; using defaults");
        return defaults;
    }
    std::string parseError;
    const Json root = Json::parse(text, &parseError);
    if (!parseError.empty() || !root.isObject()) {
        warnings.push_back("Corrupt environments.json was ignored: " + parseError);
        return defaults;
    }
    const int version = static_cast<int>(root.getInt("version", 1));
    if (version > 1) {
        warnings.push_back("environments.json uses a newer schema version; using defaults");
        return defaults;
    }
    return environmentsFromJson(root);
}

bool Storage::saveEnvironments(const EnvironmentStore& environments, std::string* error) {
    if (!initialized_ && !initialize(error)) return false;
    return writeAtomic(environmentsPath(), environmentsToJson(environments).dump(2) + "\n", error);
}

Settings Storage::loadSettings(std::vector<std::string>& warnings) {
    Settings defaults;
    std::error_code ec;
    if (!std::filesystem::exists(std::filesystem::u8path(settingsPath()), ec)) return defaults;
    bool ok = false;
    const std::string text = readFile(settingsPath(), ok);
    if (!ok) {
        warnings.push_back("Cannot read settings.json; using defaults");
        return defaults;
    }
    std::string parseError;
    const Json root = Json::parse(text, &parseError);
    if (!parseError.empty() || !root.isObject()) {
        warnings.push_back("Corrupt settings.json was ignored: " + parseError);
        return defaults;
    }
    return settingsFromJson(root);
}

bool Storage::saveSettings(const Settings& settings, std::string* error) {
    if (!initialized_ && !initialize(error)) return false;
    return writeAtomic(settingsPath(), settingsToJson(settings).dump(2) + "\n", error);
}

std::vector<HistoryEntry> Storage::loadHistory(std::vector<std::string>& warnings) {
    std::vector<HistoryEntry> history;
    std::error_code ec;
    if (!std::filesystem::exists(std::filesystem::u8path(historyPath()), ec)) return history;
    bool ok = false;
    const std::string text = readFile(historyPath(), ok);
    if (!ok) {
        warnings.push_back("Cannot read history.json");
        return history;
    }
    std::string parseError;
    const Json root = Json::parse(text, &parseError);
    if (!parseError.empty() || !root.isObject()) {
        warnings.push_back("Corrupt history.json was ignored: " + parseError);
        return history;
    }
    history = historyFromJson(root);
    if (history.size() > 100) history.resize(100);
    return history;
}

bool Storage::saveHistory(const std::vector<HistoryEntry>& history, std::string* error) {
    if (!initialized_ && !initialize(error)) return false;
    std::vector<HistoryEntry> limited = history;
    if (limited.size() > 100) limited.resize(100);
    return writeAtomic(historyPath(), historyToJson(limited).dump(2) + "\n", error);
}

} // namespace lite
