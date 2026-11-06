#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/http_client.h"
#include "core/models.h"
#include "core/storage.h"
#include "core/variables.h"
#include "ui/request_view.h"
#include "ui/response_view.h"
#include "ui/sidebar.h"

namespace lite {
namespace ui {

class MainWindow {
public:
    explicit MainWindow(HINSTANCE instance);
    ~MainWindow();

    bool create();
    void show(int cmdShow);
    bool preTranslate(MSG* message);
    std::vector<std::pair<std::wstring, std::wstring>> collectVariablesForMenu() const;

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void loadState();
    void saveState();
    void updateTitle();
    void layout();
    void paint();
    void applyChrome();
    void toggleTheme();
    void broadcastTheme();
    bool darkFromSettings() const;

    void onSend();
    void onRequestFinished(HttpResponse* response);
    void onSaveRequest();
    void onNewRequest();
    void onSidebarSelect(const std::string& collectionId, const std::string& nodeId);
    static void sidebarSelectTrampoline(void* context, const std::string& collectionId, const std::string& nodeId);
    void refreshSidebar();
    bool saveCurrentRequest(bool createIfMissing);
    Collection* activeCollection();
    void onImportCurl();
    void onExportCurl();
    void onManageEnvs();
    void onEnvChipClick();
    void onImportClick();
    void onExportClick();
    void onNewCollection();
    void onRenameCollection();
    void onDeleteCollection();
    void onExportCollection();
    void onNewFolder();
    void onRenameFolder();
    void onDeleteFolder();
    void onMoveNode();
    void onEditAuth();
    void onRenameRequest();
    void onDuplicateRequest();
    void onDeleteRequest();
    void onImportCollectionJson();
    void toggleResponseExpand();
    void joinWorker();

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HACCEL accel_ = nullptr;

    Storage storage_;
    std::vector<Collection> collections_;
    EnvironmentStore environments_;
    Settings settings_;
    std::vector<HistoryEntry> history_;
    std::vector<std::string> warnings_;

    Sidebar sidebar_;
    RequestView requestView_;
    ResponseView responseView_;

    HWND envChip_ = nullptr;
    HWND importBtn_ = nullptr;
    HWND exportBtn_ = nullptr;
    HWND themeBtn_ = nullptr;

    std::thread worker_;
    std::shared_ptr<CancelToken> cancel_;
    bool busy_ = false;

    std::string currentCollectionId_;
    std::string currentNodeId_;
    HWND lastFocusedChild_ = nullptr;

    bool draggingResponseSplitter_ = false;
    int dragStartY_ = 0;
    int dragStartResponseHeight_ = 0;
    bool responseExpanded_ = false;
    int preExpandResponseHeight_ = 0;
    RECT splitterRect_ = {};
};

} // namespace ui
} // namespace lite
