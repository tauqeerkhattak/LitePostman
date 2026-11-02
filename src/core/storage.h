#pragma once

#include <string>
#include <vector>

#include "core/models.h"

namespace lite {

class Storage {
public:
    // An empty dataDirectory uses %APPDATA%\LitePostman.
    explicit Storage(std::string dataDirectory = {});

    bool initialize(std::string* error = nullptr);
    const std::string& directory() const { return directory_; }

    std::vector<Collection> loadCollections(std::vector<std::string>& warnings);
    bool saveCollection(const Collection& collection, std::string* error = nullptr);
    bool deleteCollection(const std::string& id, std::string* error = nullptr);

    EnvironmentStore loadEnvironments(std::vector<std::string>& warnings);
    bool saveEnvironments(const EnvironmentStore& environments, std::string* error = nullptr);

    Settings loadSettings(std::vector<std::string>& warnings);
    bool saveSettings(const Settings& settings, std::string* error = nullptr);

    std::vector<HistoryEntry> loadHistory(std::vector<std::string>& warnings);
    bool saveHistory(const std::vector<HistoryEntry>& history, std::string* error = nullptr);

    std::string collectionPath(const std::string& id) const;
    std::string environmentsPath() const;
    std::string settingsPath() const;
    std::string historyPath() const;

private:
    bool writeAtomic(const std::string& path, const std::string& data, std::string* error);
    std::string readFile(const std::string& path, bool& ok) const;

    std::string directory_;
    bool initialized_ = false;
};

} // namespace lite
