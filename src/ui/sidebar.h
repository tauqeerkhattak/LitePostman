#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "core/models.h"

namespace lite {
namespace ui {

class Sidebar {
public:
    using SelectHandler = void (*)(void* context, const std::string& collectionId, const std::string& nodeId);

    Sidebar();
    ~Sidebar();

    HWND create(HWND parent, int id);
    void resize(int x, int y, int width, int height);
    void setCollections(const std::vector<Collection>& collections);
    void setSelectHandler(SelectHandler handler, void* context);
    void setEnvironment(const std::wstring& name, bool active);
    void selectNode(const std::string& collectionId, const std::string& nodeId);
    void getContextTarget(std::string& collectionId, std::string& nodeId) const {
        collectionId = contextCollection_;
        nodeId = contextNode_;
    }

private:
    struct Row {
        std::string collectionId;
        std::string nodeId;
        std::wstring label;
        std::string method;
        int depth = 0;
        bool folder = false;
        bool collection = false;
        bool expanded = false;
    };

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void rebuild();
    void flatten(const Collection& collection, const CollectionNode& node, int depth);
    bool isExpanded(const std::string& id, bool defaultValue) const;
    void toggle(const std::string& id, bool defaultValue);
    int rowAt(int y) const;
    void ensureVisible(int index);
    void selectIndex(int index, bool activate);
    void paint();
    int bodyTop() const;
    int bodyBottom() const;
    RECT plusRect() const;

    HWND hwnd_ = nullptr;
    std::vector<Collection> collections_;
    std::vector<Row> rows_;
    std::map<std::string, bool> expanded_;
    SelectHandler handler_ = nullptr;
    void* context_ = nullptr;

    std::string selectedCollection_;
    std::string selectedNode_;
    std::string contextCollection_;
    std::string contextNode_;
    std::wstring environment_ = L"No environment";
    bool environmentActive_ = false;

    int hover_ = -1;
    int scroll_ = 0;
    bool tracking_ = false;
    bool footerHover_ = false;
    bool plusHover_ = false;
};

} // namespace ui
} // namespace lite
