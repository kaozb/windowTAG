// ============================================================
// 快速启动面板 · M1
// 纯 Win32 + GDI 自绘，零第三方依赖；数据存 INI（系统 API）
// 数据模型：分组(NT_GROUP) → 标签(NT_TAG) → 链接(NT_LINK) 自引用树
// ============================================================
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cwctype>
#include "version.h"

// ---------------- 数据模型 ----------------
enum { NT_GROUP = 0, NT_TAG = 1, NT_LINK = 2 };
enum { R_NONE = 0, R_TITLE, R_SEARCH, R_GROUP, R_TAG, R_ADDTAG, R_CELL,
       R_BTN_CLOSE, R_BTN_MENU, R_BTN_PIN };

struct Node {
    int id = 0, type = 0, parent = 0, count = 0;
    std::wstring name, path, params;
};

// 菜单命令 ID
enum {
    M_ADDGROUP = 101, M_ADDLINK, M_SORTCOUNT, M_SORTMANUAL, M_PIN, M_HIDEFOCUS, M_EXIT,
    L_OPEN = 201, L_ADMIN, L_LOC, L_EDIT, L_DEL,
    T_REN = 301, T_DEL, T_ADDLINK,
    G_ADDTAG = 401, G_ADDTAG2, G_ADDLINK, G_REN, G_DEL,
    BG_ADDLINK = 501, BG_ADDTAG,
    TR_SHOW = 601, TR_EXIT
};

// 编辑框控件 ID
enum { IDC_NAME = 1001, IDC_PATH, IDC_PARAMS, IDC_BROWSE };

// ---------------- 全局状态 ----------------
static const wchar_t MAIN_CLASS[] = L"LNP_MainWnd";
static const wchar_t DLG_CLASS[]  = L"LNP_EditWnd";
static const UINT    WM_TRAY = WM_APP + 1;

static HINSTANCE g_hInst;
static HWND      g_hwnd, g_hEdit, g_hDlg, g_hName, g_hPath, g_hParams;
static WNDPROC   g_editOldProc;
static int       g_dpi = 96;

static std::vector<Node> g_nodes;
static int  g_nextId = 1;
static int  g_curGroup = 0, g_curTag = 0;   // 节点 id；g_curTag==0 表示"全部"
static std::wstring g_filter;
static std::vector<const Node*> g_view;
static std::map<int, HICON> g_icons;
static int  g_scroll = 0, g_cols = 4, g_maxScroll = 0;

static bool g_pinned = false, g_hideOnFocus = true, g_hideAfterLaunch = true;
static bool g_sortByCount = false, g_ph = true, g_tracking = false, g_startHidden = false;
static int  g_hoverReg = R_NONE, g_hoverIdx = -1;

static std::wstring g_iniConf, g_iniLinks;
static NOTIFYICONDATAW g_nid = {};

// 布局区域
static RECT g_rcTitle, g_rcGroup, g_rcTag, g_rcGrid;
static std::vector<RECT> g_rcTabs;   // [0]="全部"，之后为各标签
static RECT g_rcAddTag;
static RECT g_rcBtns[3];             // 0=关闭 1=菜单 2=钉住

// GDI 资源
static HFONT g_fUi, g_fUiB, g_fSmall, g_fGlyph;
static HBRUSH g_brWhite, g_brBg, g_brGroupBg, g_brHover, g_brSplit, g_brAccent;
static HPEN  g_penSplit, g_penAccent;
static HDC g_memDC = nullptr; static HBITMAP g_memBmp = nullptr, g_memOld = nullptr;
static int g_memW = 0, g_memH = 0;

// 配色（浅色，对照截图）
static const COLORREF C_BG    = RGB(246, 247, 249);
static const COLORREF C_GRPB  = RGB(242, 243, 245);
static const COLORREF C_WHITE = RGB(255, 255, 255);
static const COLORREF C_TEXT  = RGB(38, 38, 38);
static const COLORREF C_MUT   = RGB(150, 152, 156);
static const COLORREF C_ACC   = RGB(22, 119, 255);
static const COLORREF C_HOVER = RGB(238, 242, 247);
static const COLORREF C_SPLIT = RGB(228, 230, 233);

// ---------------- 前向声明 ----------------
static void Refresh();
static void LayoutNow();
static void LaunchLink(Node* n, bool admin);
static void OpenEditor(int type, int parent, const Node* src);
static void HideWin();
static void ShowMainMenu();

// ---------------- 基础工具 ----------------
static int S(int v) { return MulDiv(v, g_dpi, 96); }

static std::wstring ExeDir() {
    wchar_t buf[MAX_PATH]; GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    size_t i = p.find_last_of(L"\\/");
    return (i == std::wstring::npos) ? std::wstring() : p.substr(0, i + 1);
}

// INI 预写 UTF-16 BOM，保证中文在任意 ACP 系统下不乱码
static void EnsureUnicodeIni(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER sz;
    if (GetFileSizeEx(h, &sz) && sz.QuadPart == 0) {
        DWORD wr; const BYTE bom[2] = { 0xFF, 0xFE };
        WriteFile(h, bom, 2, &wr, nullptr);
    }
    CloseHandle(h);
}

// ---------------- 配置 ----------------
static void SaveWinPos() {
    if (!g_hwnd) return;
    RECT r; GetWindowRect(g_hwnd, &r);
    WritePrivateProfileStringW(L"ui", L"x", std::to_wstring(r.left).c_str(), g_iniConf.c_str());
    WritePrivateProfileStringW(L"ui", L"y", std::to_wstring(r.top).c_str(), g_iniConf.c_str());
    WritePrivateProfileStringW(L"ui", L"w", std::to_wstring(r.right - r.left).c_str(), g_iniConf.c_str());
    WritePrivateProfileStringW(L"ui", L"h", std::to_wstring(r.bottom - r.top).c_str(), g_iniConf.c_str());
    WritePrivateProfileStringW(L"ui", L"pinned", std::to_wstring(g_pinned ? 1 : 0).c_str(), g_iniConf.c_str());
    WritePrivateProfileStringW(L"ui", L"sortByCount", std::to_wstring(g_sortByCount ? 1 : 0).c_str(), g_iniConf.c_str());
    WritePrivateProfileStringW(L"behavior", L"hideOnFocus", std::to_wstring(g_hideOnFocus ? 1 : 0).c_str(), g_iniConf.c_str());
    WritePrivateProfileStringW(L"behavior", L"hideAfterLaunch", std::to_wstring(g_hideAfterLaunch ? 1 : 0).c_str(), g_iniConf.c_str());
}

static void Quit() {
    SaveWinPos();
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    DestroyWindow(g_hwnd);
}

// ---------------- 节点持久化 ----------------
static Node* Find(int id) {
    for (auto& n : g_nodes) if (n.id == id) return &n;
    return nullptr;
}

static void SaveNode(const Node& n) {
    wchar_t sec[32]; wsprintfW(sec, L"node.%d", n.id);
    WritePrivateProfileStringW(sec, L"type",   std::to_wstring(n.type).c_str(),   g_iniLinks.c_str());
    WritePrivateProfileStringW(sec, L"parent", std::to_wstring(n.parent).c_str(), g_iniLinks.c_str());
    WritePrivateProfileStringW(sec, L"name",   n.name.c_str(),   g_iniLinks.c_str());
    WritePrivateProfileStringW(sec, L"path",   n.path.c_str(),   g_iniLinks.c_str());
    WritePrivateProfileStringW(sec, L"params", n.params.c_str(), g_iniLinks.c_str());
    WritePrivateProfileStringW(sec, L"count",  std::to_wstring(n.count).c_str(), g_iniLinks.c_str());
}

static void SaveNextId() {
    WritePrivateProfileStringW(L"meta", L"next", std::to_wstring(g_nextId).c_str(), g_iniLinks.c_str());
}

static void DeleteNodeCascade(int id) {
    bool again = true;
    while (again) {
        again = false;
        for (auto& n : g_nodes)
            if (n.parent == id) { DeleteNodeCascade(n.id); again = true; break; }
    }
    auto it = std::find_if(g_nodes.begin(), g_nodes.end(),
                           [id](const Node& n) { return n.id == id; });
    if (it == g_nodes.end()) return;
    auto ic = g_icons.find(id);
    if (ic != g_icons.end()) {
        if (ic->second) DestroyIcon(ic->second);
        g_icons.erase(ic);
    }
    wchar_t sec[32]; wsprintfW(sec, L"node.%d", id);
    WritePrivateProfileStringW(sec, nullptr, nullptr, g_iniLinks.c_str());
    g_nodes.erase(it);
}

static void Seed() {
    g_nodes.clear();
    Node g; g.id = 1; g.type = NT_GROUP; g.name = L"常用";
    Node t; t.id = 2; t.type = NT_TAG; t.parent = 1; t.name = L"工具";
    Node l; l.id = 3; l.type = NT_LINK; l.parent = 2; l.name = L"腾讯文档"; l.path = L"https://docs.qq.com";
    g_nodes = { g, t, l };
    g_nextId = 4;
    for (auto& n : g_nodes) SaveNode(n);
    SaveNextId();
}

static void LoadNodes() {
    g_nodes.clear();
    g_nextId = GetPrivateProfileIntW(L"meta", L"next", 1, g_iniLinks.c_str());
    wchar_t buf[1024];
    for (int i = 1; i < g_nextId; i++) {
        wchar_t sec[32]; wsprintfW(sec, L"node.%d", i);
        GetPrivateProfileStringW(sec, L"type", L"\x1", buf, 1024, g_iniLinks.c_str());
        if (!wcscmp(buf, L"\x1")) continue;              // 节不存在 = 已删除
        Node n;
        n.id = i;
        n.type = _wtoi(buf);
        n.parent = GetPrivateProfileIntW(sec, L"parent", 0, g_iniLinks.c_str());
        GetPrivateProfileStringW(sec, L"name", L"", buf, 1024, g_iniLinks.c_str());   n.name = buf;
        GetPrivateProfileStringW(sec, L"path", L"", buf, 1024, g_iniLinks.c_str());   n.path = buf;
        GetPrivateProfileStringW(sec, L"params", L"", buf, 1024, g_iniLinks.c_str()); n.params = buf;
        n.count = GetPrivateProfileIntW(sec, L"count", 0, g_iniLinks.c_str());
        g_nodes.push_back(n);
    }
    if (g_nodes.empty()) Seed();
}

// ---------------- 视图 ----------------
static std::wstring Lower(std::wstring r) {
    for (auto& c : r) c = (wchar_t)towlower(c);
    return r;
}

static std::vector<std::wstring> Tokens(const std::wstring& s) {
    std::vector<std::wstring> out; std::wstring cur;
    for (wchar_t c : s) {
        if (c == L' ' || c == L'\t') { if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
        else cur += (wchar_t)towlower(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

static bool MatchAll(const Node& n, const std::vector<std::wstring>& toks) {
    std::wstring hay = Lower(n.name + L" " + n.path + L" " + n.params);
    for (auto& t : toks)
        if (hay.find(t) == std::wstring::npos) return false;
    return true;
}

static void RebuildView() {
    g_view.clear(); g_scroll = 0;
    auto toks = Tokens(g_filter);
    if (!toks.empty()) {                                  // 搜索：全局匹配
        for (auto& n : g_nodes)
            if (n.type == NT_LINK && MatchAll(n, toks)) g_view.push_back(&n);
    } else if (g_curGroup) {
        for (auto& n : g_nodes) {
            if (n.type != NT_LINK) continue;
            bool show = false;
            if (g_curTag == 0) {                          // "全部"：组内直挂 + 标签下
                if (n.parent == g_curGroup) show = true;
                else {
                    const Node* p = Find(n.parent);
                    show = p && p->type == NT_TAG && p->parent == g_curGroup;
                }
            } else show = (n.parent == g_curTag);
            if (show) g_view.push_back(&n);
        }
    }
    if (g_sortByCount)
        std::stable_sort(g_view.begin(), g_view.end(),
                         [](const Node* a, const Node* b) { return a->count > b->count; });
}

static std::vector<Node*> GroupList() {
    std::vector<Node*> v;
    for (auto& n : g_nodes) if (n.type == NT_GROUP && n.parent == 0) v.push_back(&n);
    return v;
}

static std::vector<Node*> TagListOf(int group) {
    std::vector<Node*> v;
    for (auto& n : g_nodes) if (n.type == NT_TAG && n.parent == group) v.push_back(&n);
    return v;
}

static void Refresh() {
    RebuildView();
    LayoutNow();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

// ---------------- 图标 ----------------
static HICON IconFor(const Node& n) {
    auto it = g_icons.find(n.id);
    if (it != g_icons.end()) return it->second;
    HICON h = nullptr;
    SHFILEINFOW fi = {};
    bool isUrl = !wcsncmp(n.path.c_str(), L"http://", 7) || !wcsncmp(n.path.c_str(), L"https://", 8);
    if (isUrl) {                                          // 借 .url 注册表类型取地球图标
        if (SHGetFileInfoW(L"x.url", FILE_ATTRIBUTE_NORMAL, &fi, sizeof fi,
                           SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES))
            h = fi.hIcon;
    } else if (SHGetFileInfoW(n.path.c_str(), 0, &fi, sizeof fi, SHGFI_ICON | SHGFI_LARGEICON)) {
        h = fi.hIcon;
    }
    if (!h) {                                             // 路径失效的兜底图标
        if (SHGetFileInfoW(L"file", FILE_ATTRIBUTE_NORMAL, &fi, sizeof fi,
                           SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES))
            h = fi.hIcon;
    }
    g_icons[n.id] = h;
    return h;
}

// ---------------- 启动链接 ----------------
static void LaunchLink(Node* n, bool admin) {
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof sei;
    sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_UNICODE;
    sei.hwnd = g_hwnd;
    sei.lpVerb = admin ? L"runas" : nullptr;
    sei.lpFile = n->path.c_str();
    if (!n->params.empty()) sei.lpParameters = n->params.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&sei)) {
        Node* real = Find(n->id);
        if (real) { real->count++; SaveNode(*real); }
        Refresh();
        if (g_hideAfterLaunch) HideWin();
    } else if (GetLastError() != ERROR_CANCELLED) {
        MessageBoxW(g_hwnd, L"启动失败：路径可能已失效。", L"启动器", MB_OK | MB_ICONWARNING);
    }
}

// ---------------- 显隐 ----------------
static void ClampToWorkArea() {
    HMONITOR mon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof mi };
    if (!GetMonitorInfoW(mon, &mi)) return;
    RECT r; GetWindowRect(g_hwnd, &r);
    int dx = 0, dy = 0;
    if (r.right > mi.rcWork.right) dx = mi.rcWork.right - r.right;
    if (r.bottom > mi.rcWork.bottom) dy = mi.rcWork.bottom - r.bottom;
    if (r.left + dx < mi.rcWork.left) dx = mi.rcWork.left - r.left;
    if (r.top + dy < mi.rcWork.top) dy = mi.rcWork.top - r.top;
    if (dx || dy) SetWindowPos(g_hwnd, nullptr, r.left + dx, r.top + dy, 0, 0,
                               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void ApplyPin() {
    SetWindowPos(g_hwnd, g_pinned ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void ShowWin() {
    ShowWindow(g_hwnd, SW_SHOW);
    ClampToWorkArea();
    SetForegroundWindow(g_hwnd);
}

static void HideWin() { SaveWinPos(); ShowWindow(g_hwnd, SW_HIDE); }
static void ToggleWin() { IsWindowVisible(g_hwnd) ? HideWin() : ShowWin(); }

// ---------------- GDI 资源 ----------------
static void FreeGdiRes() {
    for (HFONT f : { g_fUi, g_fUiB, g_fSmall, g_fGlyph }) if (f) DeleteObject(f);
    g_fUi = g_fUiB = g_fSmall = g_fGlyph = nullptr;
}

static void MakeFonts() {
    FreeGdiRes();
    g_fUi    = CreateFontW(-S(13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    g_fUiB   = CreateFontW(-S(13), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    g_fSmall = CreateFontW(-S(11), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    g_fGlyph = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
}

static void CreateGdiRes() {
    g_brWhite   = CreateSolidBrush(C_WHITE);
    g_brBg      = CreateSolidBrush(C_BG);
    g_brGroupBg = CreateSolidBrush(C_GRPB);
    g_brHover   = CreateSolidBrush(C_HOVER);
    g_brSplit   = CreateSolidBrush(C_SPLIT);
    g_brAccent  = CreateSolidBrush(C_ACC);
    g_penSplit  = CreatePen(PS_SOLID, 1, C_SPLIT);
    g_penAccent = CreatePen(PS_SOLID, 1, C_ACC);
}

static void FreeMemDC() {
    if (g_memBmp) { SelectObject(g_memDC, g_memOld); DeleteObject(g_memBmp); g_memBmp = nullptr; }
    if (g_memDC) { DeleteDC(g_memDC); g_memDC = nullptr; }
}

// ---------------- 布局与命中测试 ----------------
static int TitleH() { return S(46); }
static int GroupW() { return S(148); }
static int TagH()   { return S(40); }
static int CellW()  { return S(104); }
static int CellH()  { return S(98); }
static int Gap()    { return S(8); }

static void LayoutNow() {
    RECT rc; GetClientRect(g_hwnd, &rc);
    g_rcTitle = { 0, 0, rc.right, TitleH() };
    g_rcGroup = { 0, TitleH(), GroupW(), rc.bottom };
    g_rcTag   = { GroupW(), TitleH(), rc.right, TitleH() + TagH() };
    g_rcGrid  = { GroupW(), TitleH() + TagH(), rc.right, rc.bottom };

    // 标题栏按钮：0=关闭 1=菜单 2=钉住
    int b = S(34);
    for (int i = 0; i < 3; i++) {
        int x = g_rcTitle.right - S(10) - b * (i + 1) - S(2) * i;
        g_rcBtns[i] = { x, (TitleH() - b) / 2, x + b, (TitleH() + b) / 2 };
    }

    // 标签宽度按文本实测
    g_rcTabs.clear();
    HDC dc = GetDC(g_hwnd);
    HFONT of = (HFONT)SelectObject(dc, g_fUi);
    auto tags = TagListOf(g_curGroup);
    int x = g_rcTag.left + Gap();
    auto addTab = [&](const wchar_t* s) {
        SIZE sz; GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz);
        RECT r{ x, g_rcTag.top, x + sz.cx + S(24), g_rcTag.bottom };
        g_rcTabs.push_back(r);
        x = r.right;
    };
    addTab(L"全部");
    for (auto* t : tags) addTab(t->name.c_str());
    SelectObject(dc, of);
    ReleaseDC(g_hwnd, dc);
    g_rcAddTag = { g_rcTag.right - S(40), g_rcTag.top, g_rcTag.right, g_rcTag.bottom };

    // 网格列数与滚动范围
    int avail = g_rcGrid.right - g_rcGrid.left;
    g_cols = std::max(1, (avail - Gap()) / (CellW() + Gap()));
    int rows = (int)((g_view.size() + g_cols - 1) / g_cols);
    int content = rows * (CellH() + Gap()) + Gap();
    g_maxScroll = std::max(0, content - (int)(g_rcGrid.bottom - g_rcGrid.top));
    g_scroll = std::min(g_scroll, g_maxScroll);
}

static RECT CellRect(int i) {
    int col = i % g_cols, row = i / g_cols;
    int x = g_rcGrid.left + Gap() + col * (CellW() + Gap());
    int y = g_rcGrid.top + Gap() + row * (CellH() + Gap()) - g_scroll;
    return { x, y, x + CellW(), y + CellH() };
}

static RECT GroupItemRect(int i) {
    return { 0, g_rcGroup.top + S(8) + i * S(36), g_rcGroup.right,
             g_rcGroup.top + S(8) + (i + 1) * S(36) };
}

struct Hit { int reg = R_NONE; int idx = -1; };

static Hit HitTest(POINT p) {
    Hit h;
    if (g_rcBtns[0].right == 0) return h;
    if (PtInRect(&g_rcTitle, p)) {
        for (int i = 0; i < 3; i++)
            if (PtInRect(&g_rcBtns[i], p)) { h.reg = R_BTN_CLOSE + i; return h; }
        RECT sr{ S(12), S(9), S(12) + S(200), S(9) + S(28) };
        h.reg = PtInRect(&sr, p) ? R_SEARCH : R_TITLE;
        return h;
    }
    if (PtInRect(&g_rcGroup, p)) {
        auto gl = GroupList();
        for (int i = 0; i < (int)gl.size(); i++) {
            RECT gr = GroupItemRect(i);
            if (PtInRect(&gr, p)) { h.reg = R_GROUP; h.idx = i; return h; }
        }
        return h;
    }
    if (PtInRect(&g_rcTag, p)) {
        if (PtInRect(&g_rcAddTag, p)) { h.reg = R_ADDTAG; return h; }
        for (int i = 0; i < (int)g_rcTabs.size(); i++)
            if (PtInRect(&g_rcTabs[i], p)) { h.reg = R_TAG; h.idx = i; return h; }
        return h;
    }
    if (PtInRect(&g_rcGrid, p)) {
        int n = (int)g_view.size();
        for (int i = 0; i < n; i++) {
            RECT cr = CellRect(i);
            if (PtInRect(&cr, p)) { h.reg = R_CELL; h.idx = i; return h; }
        }
    }
    return h;
}

// ---------------- 编辑对话框 ----------------
struct EditCtx {
    bool isNew = false; int type = NT_LINK, parent = 0, id = 0;
    std::wstring name, path, params;
};
static EditCtx g_ctx;

static void EditorApply() {
    wchar_t buf[1024];
    GetWindowTextW(g_hName, buf, 1024); g_ctx.name = buf;
    g_ctx.path.clear(); g_ctx.params.clear();
    if (g_ctx.type == NT_LINK) {
        GetWindowTextW(g_hPath, buf, 1024); g_ctx.path = buf;
        GetWindowTextW(g_hParams, buf, 1024); g_ctx.params = buf;
        if (g_ctx.path.empty()) { MessageBoxW(g_hDlg, L"请填写目标路径或网址。", L"启动器", MB_OK | MB_ICONWARNING); return; }
    }
    if (g_ctx.name.empty()) { MessageBoxW(g_hDlg, L"请填写名称。", L"启动器", MB_OK | MB_ICONWARNING); return; }

    if (g_ctx.isNew) {
        Node n;
        n.id = g_nextId++; n.type = g_ctx.type; n.parent = g_ctx.parent;
        n.name = g_ctx.name; n.path = g_ctx.path; n.params = g_ctx.params;
        g_nodes.push_back(n); SaveNode(n); SaveNextId();
        if (n.type == NT_GROUP) g_curGroup = n.id;
        if (n.type == NT_TAG)   g_curTag = n.id;
    } else if (Node* n = Find(g_ctx.id)) {
        n->name = g_ctx.name; n->path = g_ctx.path; n->params = g_ctx.params;
        SaveNode(*n);
    }
    if (!g_filter.empty()) { g_filter.clear(); SetWindowTextW(g_hEdit, g_ph ? L"搜索" : L""); }
    Refresh();
    DestroyWindow(g_hDlg);
}

static LRESULT CALLBACK DlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        // 客户区：链接 380x210；分组/标签 380x140
        int type = (int)(INT_PTR)((CREATESTRUCTW*)l)->lpCreateParams;
        int cw = S(380), ch = (type == NT_LINK) ? S(210) : S(140);
        int x = S(16), lw = S(48), fw = cw - S(16) * 2 - lw;
        g_hName = g_hPath = g_hParams = nullptr;
        auto mk = [&](const wchar_t* cls, const wchar_t* txt, DWORD st,
                      int px, int py, int pw, int ph, int id) -> HWND {
            DWORD ex = wcscmp(cls, L"EDIT") ? 0 : WS_EX_CLIENTEDGE;
            return CreateWindowExW(ex, cls, txt, WS_CHILD | WS_VISIBLE | st,
                                   px, py, pw, ph, h, (HMENU)(INT_PTR)id,
                                   ((CREATESTRUCTW*)l)->hInstance, nullptr);
        };
        mk(L"STATIC", L"名称", 0, x, S(22), lw, S(18), 0);
        g_hName = mk(L"EDIT", g_ctx.name.c_str(), ES_AUTOHSCROLL | WS_TABSTOP,
                     x + lw, S(16), fw, S(24), IDC_NAME);
        if (type == NT_LINK) {
            mk(L"STATIC", L"目标", 0, x, S(58), lw, S(18), 0);
            g_hPath = mk(L"EDIT", g_ctx.path.c_str(), ES_AUTOHSCROLL | WS_TABSTOP,
                         x + lw, S(52), fw - S(76), S(24), IDC_PATH);
            mk(L"BUTTON", L"浏览…", BS_PUSHBUTTON | WS_TABSTOP,
               x + lw + fw - S(76) + S(8), S(52), S(68), S(24), IDC_BROWSE);
            mk(L"STATIC", L"参数", 0, x, S(94), lw, S(18), 0);
            g_hParams = mk(L"EDIT", g_ctx.params.c_str(), ES_AUTOHSCROLL | WS_TABSTOP,
                           x + lw, S(88), fw, S(24), IDC_PARAMS);
        }
        int by = (type == NT_LINK) ? S(140) : S(94);
        mk(L"BUTTON", L"确定", BS_DEFPUSHBUTTON | WS_TABSTOP,
           cw - S(16) - S(88) * 2 - S(8), by, S(88), S(26), IDOK);
        mk(L"BUTTON", L"取消", BS_PUSHBUTTON | WS_TABSTOP,
           cw - S(16) - S(88), by, S(88), S(26), IDCANCEL);
        EnumChildWindows(h, [](HWND c, LPARAM) -> BOOL {
            SendMessageW(c, WM_SETFONT, (WPARAM)g_fUi, TRUE); return TRUE; }, 0);
        SendMessageW(g_hName, EM_SETSEL, 0, -1);
        SetFocus(g_hName);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDC_BROWSE: {
            wchar_t file[1024] = L"";
            if (g_hPath) GetWindowTextW(g_hPath, file, 1024);
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof ofn;
            ofn.hwndOwner = h;
            ofn.lpstrFilter = L"程序/链接 (*.exe;*.lnk;*.url;*.bat)\0*.exe;*.lnk;*.url;*.bat\0所有文件\0*.*\0";
            ofn.lpstrFile = file; ofn.nMaxFile = 1024;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
            if (GetOpenFileNameW(&ofn)) SetWindowTextW(g_hPath, file);
            return 0;
        }
        case IDOK: EditorApply(); return 0;
        case IDCANCEL: DestroyWindow(h); return 0;
        }
        break;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_NCDESTROY: g_hDlg = nullptr; return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void OpenEditor(int type, int parent, const Node* src) {
    if (g_hDlg) { SetForegroundWindow(g_hDlg); return; }
    g_ctx = EditCtx();
    g_ctx.isNew = !src;
    if (src) {
        g_ctx.id = src->id; g_ctx.type = src->type; g_ctx.parent = src->parent;
        g_ctx.name = src->name; g_ctx.path = src->path; g_ctx.params = src->params;
    } else {
        g_ctx.type = type; g_ctx.parent = parent;
    }
    const wchar_t* title = g_ctx.isNew
        ? (type == NT_LINK ? L"添加链接" : type == NT_TAG ? L"添加标签" : L"添加分组")
        : L"编辑";
    int cw = S(380), ch = (g_ctx.type == NT_LINK) ? S(210) : S(140);
    RECT wr{ 0, 0, cw, ch };
    AdjustWindowRect(&wr, WS_CAPTION | WS_SYSMENU, FALSE);
    g_hDlg = CreateWindowExW(0, DLG_CLASS, title,
                             WS_POPUP | WS_CAPTION | WS_SYSMENU,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             wr.right - wr.left, wr.bottom - wr.top,
                             g_hwnd, nullptr, g_hInst, (LPVOID)(INT_PTR)g_ctx.type);
    if (g_hDlg) ShowWindow(g_hDlg, SW_SHOW);
}

// ---------------- 菜单 ----------------
static int PopupMenuScreen(HMENU m, POINT scr) {
    SetForegroundWindow(g_hwnd);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, scr.x, scr.y, 0, g_hwnd, nullptr);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
    return cmd;
}

static int PopupMenuClient(HMENU m, POINT ptClient) {
    POINT s = ptClient;
    ClientToScreen(g_hwnd, &s);
    return PopupMenuScreen(m, s);
}

static void ShowMainMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, M_ADDGROUP, L"添加分组");
    AppendMenuW(m, MF_STRING, M_ADDLINK, L"添加链接");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (g_sortByCount ? 0 : MF_CHECKED), M_SORTMANUAL, L"排序：手动");
    AppendMenuW(m, MF_STRING | (g_sortByCount ? MF_CHECKED : 0), M_SORTCOUNT, L"排序：按启动次数");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (g_pinned ? MF_CHECKED : 0), M_PIN, L"钉住");
    AppendMenuW(m, MF_STRING | (g_hideOnFocus ? MF_CHECKED : 0), M_HIDEFOCUS, L"失焦自动隐藏");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, M_EXIT, L"退出");
    RECT r = g_rcBtns[1];
    int cmd = PopupMenuClient(m, { r.left, r.bottom + S(4) });
    switch (cmd) {
    case M_ADDGROUP: OpenEditor(NT_GROUP, 0, nullptr); break;
    case M_ADDLINK:
        if (g_curGroup) OpenEditor(NT_LINK, g_curTag ? g_curTag : g_curGroup, nullptr);
        else MessageBoxW(g_hwnd, L"请先添加分组。", L"启动器", MB_OK | MB_ICONINFORMATION);
        break;
    case M_SORTMANUAL:
    case M_SORTCOUNT:
        g_sortByCount = (cmd == M_SORTCOUNT); SaveWinPos(); Refresh();
        break;
    case M_PIN: g_pinned = !g_pinned; ApplyPin(); SaveWinPos(); InvalidateRect(g_hwnd, nullptr, FALSE); break;
    case M_HIDEFOCUS: g_hideOnFocus = !g_hideOnFocus; SaveWinPos(); break;
    case M_EXIT: Quit(); break;
    }
}

static void ShowLinkMenu(Node* n, POINT scr) {
    bool hasDir = n->path.find(L'\\') != std::wstring::npos;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, L_OPEN, L"打开");
    AppendMenuW(m, MF_STRING, L_ADMIN, L"以管理员身份运行");
    if (hasDir) AppendMenuW(m, MF_STRING, L_LOC, L"打开所在位置");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, L_EDIT, L"编辑");
    AppendMenuW(m, MF_STRING, L_DEL, L"删除");
    switch (PopupMenuScreen(m, scr)) {
    case L_OPEN: LaunchLink(n, false); break;
    case L_ADMIN: LaunchLink(n, true); break;
    case L_LOC: {
        std::wstring dir = n->path.substr(0, n->path.find_last_of(L"\\/"));
        ShellExecuteW(g_hwnd, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    }
    case L_EDIT: OpenEditor(0, 0, n); break;
    case L_DEL:
        DeleteNodeCascade(n->id);
        if (g_curTag == n->id) g_curTag = 0;
        if (g_curGroup == n->id) {
            auto gl = GroupList();
            g_curGroup = gl.empty() ? 0 : gl[0]->id; g_curTag = 0;
        }
        Refresh();
        break;
    }
}

static void ShowTagMenu(Node* tag, POINT scr) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, T_ADDLINK, L"添加链接");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, T_REN, L"重命名");
    AppendMenuW(m, MF_STRING, T_DEL, L"删除");
    switch (PopupMenuScreen(m, scr)) {
    case T_ADDLINK: OpenEditor(NT_LINK, tag->id, nullptr); break;
    case T_REN: OpenEditor(0, 0, tag); break;
    case T_DEL:
        DeleteNodeCascade(tag->id);
        if (g_curTag == tag->id) g_curTag = 0;
        Refresh();
        break;
    }
}

static void ShowGroupMenu(Node* grp, POINT scr) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, G_ADDTAG, L"添加标签");
    AppendMenuW(m, MF_STRING, G_ADDLINK, L"添加链接");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, G_REN, L"重命名");
    AppendMenuW(m, MF_STRING, G_DEL, L"删除");
    switch (PopupMenuScreen(m, scr)) {
    case G_ADDTAG: OpenEditor(NT_TAG, grp->id, nullptr); break;
    case G_ADDLINK: OpenEditor(NT_LINK, g_curTag ? g_curTag : grp->id, nullptr); break;
    case G_REN: OpenEditor(0, 0, grp); break;
    case G_DEL:
        DeleteNodeCascade(grp->id);
        if (g_curGroup == grp->id) {
            auto gl = GroupList();
            g_curGroup = gl.empty() ? 0 : gl[0]->id; g_curTag = 0;
        }
        Refresh();
        break;
    }
}

static void ShowGridBgMenu(POINT scr) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, BG_ADDLINK, L"添加链接");
    AppendMenuW(m, MF_STRING, BG_ADDTAG, L"添加标签");
    switch (PopupMenuScreen(m, scr)) {
    case BG_ADDLINK:
        if (g_curGroup) OpenEditor(NT_LINK, g_curTag ? g_curTag : g_curGroup, nullptr);
        break;
    case BG_ADDTAG:
        if (g_curGroup) OpenEditor(NT_TAG, g_curGroup, nullptr);
        break;
    }
}

// ---------------- 绘制 ----------------
static void DrawTextRect(HDC dc, const RECT& r, const wchar_t* s, HFONT f, COLORREF c, UINT fmt) {
    HFONT of = (HFONT)SelectObject(dc, f);
    SetTextColor(dc, c); SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s, -1, (LPRECT)&r, fmt);
    SelectObject(dc, of);
}

static void DrawGlyph(HDC dc, const RECT& r, wchar_t g, COLORREF c) {
    wchar_t s[2] = { g, 0 };
    DrawTextRect(dc, r, s, g_fGlyph, c, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void PaintAll(HDC dc) {
    auto gl = GroupList();
    auto tl = TagListOf(g_curGroup);

    // 标题栏
    FillRect(dc, &g_rcTitle, g_brWhite);
    RECT lineT{ 0, g_rcTitle.bottom - 1, g_rcTitle.right, g_rcTitle.bottom };
    FillRect(dc, &lineT, g_brSplit);
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t db[32]; wsprintfW(db, L"%d月%d日", st.wMonth, st.wDay);
    RECT dr{ 0, 0, g_rcBtns[2].left - S(6), g_rcTitle.bottom };
    DrawTextRect(dc, dr, db, g_fUi, C_TEXT, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    const wchar_t glyphs[3] = { 0xE711, 0xE700, 0xE718 };   // 关闭 菜单 钉住
    for (int i = 0; i < 3; i++) {
        bool hot = (g_hoverReg == R_BTN_CLOSE + i);
        if (hot) FillRect(dc, &g_rcBtns[i], g_brHover);
        COLORREF c = (i == 2 && g_pinned) ? C_ACC : (hot ? C_TEXT : C_MUT);
        DrawGlyph(dc, g_rcBtns[i], glyphs[i], c);
    }

    // 分组栏
    FillRect(dc, &g_rcGroup, g_brGroupBg);
    RECT lineG{ g_rcGroup.right - 1, g_rcGroup.top, g_rcGroup.right, g_rcGroup.bottom };
    FillRect(dc, &lineG, g_brSplit);
    for (int i = 0; i < (int)gl.size(); i++) {
        RECT r = GroupItemRect(i);
        bool sel = (gl[i]->id == g_curGroup);
        bool hot = (g_hoverReg == R_GROUP && g_hoverIdx == i);
        if (sel) FillRect(dc, &r, g_brWhite);
        else if (hot) FillRect(dc, &r, g_brHover);
        if (sel) { RECT bar{ r.left, r.top, r.left + S(3), r.bottom }; FillRect(dc, &bar, g_brAccent); }
        RECT tr{ r.left + S(14), r.top, r.right - S(6), r.bottom };
        DrawTextRect(dc, tr, gl[i]->name.c_str(), sel ? g_fUiB : g_fUi,
                     sel ? C_ACC : (hot ? C_TEXT : C_MUT),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    if (gl.empty())
        DrawTextRect(dc, g_rcGroup, L"右键\n添加分组", g_fSmall, C_MUT,
                     DT_CENTER | DT_VCENTER | DT_WORDBREAK);

    // 标签栏
    FillRect(dc, &g_rcTag, g_brWhite);
    RECT lineTag{ 0, g_rcTag.bottom - 1, g_rcTag.right, g_rcTag.bottom };
    FillRect(dc, &lineTag, g_brSplit);
    for (int i = 0; i < (int)g_rcTabs.size(); i++) {
        bool sel = (i == 0) ? (g_curTag == 0)
                            : (i - 1 < (int)tl.size() && tl[i - 1]->id == g_curTag);
        bool hot = (g_hoverReg == R_TAG && g_hoverIdx == i);
        const wchar_t* txt = (i == 0) ? L"全部" : tl[i - 1]->name.c_str();
        DrawTextRect(dc, g_rcTabs[i], txt, sel ? g_fUiB : g_fUi,
                     sel ? C_ACC : (hot ? C_TEXT : C_MUT),
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (sel) { RECT bar{ g_rcTabs[i].left, g_rcTag.bottom - S(3),
                             g_rcTabs[i].right, g_rcTag.bottom - 1 }; FillRect(dc, &bar, g_brAccent); }
    }
    DrawGlyph(dc, g_rcAddTag, 0xE710, (g_hoverReg == R_ADDTAG) ? C_TEXT : C_MUT);

    // 网格
    FillRect(dc, &g_rcGrid, g_brBg);
    if (g_view.empty()) {
        const wchar_t* hint = !g_filter.empty() ? L"无匹配结果"
            : (g_curGroup ? L"右键空白处添加链接" : L"右键添加分组");
        DrawTextRect(dc, g_rcGrid, hint, g_fUi, C_MUT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        for (int i = 0; i < (int)g_view.size(); i++) {
            RECT r = CellRect(i);
            if (r.bottom < g_rcGrid.top || r.top > g_rcGrid.bottom) continue;
            bool hot = (g_hoverReg == R_CELL && g_hoverIdx == i);
            HPEN op = (HPEN)SelectObject(dc, hot ? g_penAccent : g_penSplit);
            HBRUSH ob = (HBRUSH)SelectObject(dc, hot ? g_brHover : g_brWhite);
            RoundRect(dc, r.left, r.top, r.right, r.bottom, S(8), S(8));
            SelectObject(dc, op); SelectObject(dc, ob);
            int isz = S(32);
            HICON ic = IconFor(*g_view[i]);
            if (ic) DrawIconEx(dc, r.left + (CellW() - isz) / 2, r.top + S(14), ic,
                               isz, isz, 0, nullptr, DI_NORMAL);
            RECT nr{ r.left + S(6), r.top + S(14) + isz + S(6), r.right - S(6),
                     r.top + S(14) + isz + S(6) + S(18) };
            DrawTextRect(dc, nr, g_view[i]->name.c_str(), g_fUi, C_TEXT,
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            if (g_view[i]->count > 0) {
                wchar_t cb[24]; wsprintfW(cb, L"%d次", g_view[i]->count);
                RECT cr{ nr.left, nr.bottom, nr.right, nr.bottom + S(16) };
                DrawTextRect(dc, cr, cb, g_fSmall, C_MUT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
    }
}

// ---------------- 交互辅助 ----------------
static void SelectGroup(int id) {
    g_curGroup = id; g_curTag = 0;
    if (!g_filter.empty()) { g_filter.clear(); SetWindowTextW(g_hEdit, g_ph ? L"搜索" : L""); }
    Refresh();
}

static void OnSearchCommand(WPARAM w, LPARAM l) {
    if ((HWND)l != g_hEdit) return;
    switch (HIWORD(w)) {
    case EN_SETFOCUS:
        if (g_ph) { g_ph = false; SetWindowTextW(g_hEdit, L""); }
        break;
    case EN_KILLFOCUS:
        if (!GetWindowTextLengthW(g_hEdit)) { g_ph = true; SetWindowTextW(g_hEdit, L"搜索"); }
        break;
    case EN_CHANGE: {
        if (g_ph) break;
        wchar_t b[256]; GetWindowTextW(g_hEdit, b, 256);
        g_filter = b; Refresh();
        break;
    }
    }
}

static void ClearFilter() {
    if (!g_filter.empty()) {
        g_filter.clear();
        SetWindowTextW(g_hEdit, g_ph ? L"搜索" : L"");
    }
}

// 搜索框子类化：Esc 隐藏面板，回车启动第一个结果
static LRESULT CALLBACK SearchEditProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_KEYDOWN) {
        if (w == VK_ESCAPE) { HideWin(); return 0; }
        if (w == VK_RETURN && !g_view.empty()) { LaunchLink(const_cast<Node*>(g_view[0]), false); return 0; }
    }
    return CallWindowProcW(g_editOldProc, h, m, w, l);
}

// ---------------- 主窗口过程 ----------------
static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        g_hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"搜索",
                                  WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                  S(12), S(9), S(200), S(28), h, nullptr, g_hInst, nullptr);
        SendMessageW(g_hEdit, WM_SETFONT, (WPARAM)g_fUi, TRUE);
        g_editOldProc = (WNDPROC)SetWindowLongPtrW(g_hEdit, GWLP_WNDPROC, (LONG_PTR)SearchEditProc);
        SetTimer(h, 1, 300, nullptr);
        // 全局热键（conf.ini 可覆盖：mod=MOD_* 组合，vk=虚拟键码）
        int mod = GetPrivateProfileIntW(L"hot", L"mod", MOD_CONTROL | MOD_ALT, g_iniConf.c_str());
        int vk  = GetPrivateProfileIntW(L"hot", L"vk", 'L', g_iniConf.c_str());
        if (!RegisterHotKey(h, 1, mod | MOD_NOREPEAT, (UINT)vk)) {
            // 冲突时静默降级，可在后续版本提示
        }
        // 托盘
        g_nid.cbSize = sizeof g_nid;
        g_nid.hWnd = h;
        g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        g_nid.uCallbackMessage = WM_TRAY;
        g_nid.hIcon = LoadIconW(g_hInst, MAKEINTRESOURCEW(1));
        lstrcpyW(g_nid.szTip, L"启动面板");
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        return 0;
    }
    case WM_CLOSE: HideWin(); return 0;
    case WM_NCCALCSIZE:
        if (w) return 0;                                      // 无边框：整个窗口为客户区
        break;
    case WM_NCHITTEST: {
        POINT pt{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        ScreenToClient(h, &pt);
        RECT rc; GetClientRect(h, &rc);
        int e = S(6);
        bool Lt = pt.x < e, Rt = pt.x >= rc.right - e, Tp = pt.y < e, Bt = pt.y >= rc.bottom - e;
        if (Tp && Lt) return HTTOPLEFT;
        if (Tp && Rt) return HTTOPRIGHT;
        if (Bt && Lt) return HTBOTTOMLEFT;
        if (Bt && Rt) return HTBOTTOMRIGHT;
        if (Lt) return HTLEFT; if (Rt) return HTRIGHT;
        if (Tp) return HTTOP;  if (Bt) return HTBOTTOM;
        Hit hh = HitTest(pt);
        if (PtInRect(&g_rcTitle, pt) &&
            hh.reg != R_SEARCH && !(hh.reg >= R_BTN_CLOSE && hh.reg <= R_BTN_PIN))
            return HTCAPTION;
        return HTCLIENT;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        if (!g_memDC || g_memW != rc.right || g_memH != rc.bottom) {
            FreeMemDC();
            g_memDC = CreateCompatibleDC(dc);
            g_memBmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            g_memOld = (HBITMAP)SelectObject(g_memDC, g_memBmp);
            g_memW = rc.right; g_memH = rc.bottom;
        }
        PaintAll(g_memDC);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, g_memDC, 0, 0, SRCCOPY);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: LayoutNow(); InvalidateRect(h, nullptr, FALSE); break;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)l;
        mmi->ptMinTrackSize = { S(430), S(340) };
        break;
    }
    case WM_MOUSEMOVE: {
        if (!g_tracking) {
            TRACKMOUSEEVENT t{ sizeof t, TME_LEAVE, h, 0 };
            TrackMouseEvent(&t); g_tracking = true;
        }
        POINT pt{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        Hit hh = HitTest(pt);
        if (hh.reg != g_hoverReg || hh.idx != g_hoverIdx) {
            g_hoverReg = hh.reg; g_hoverIdx = hh.idx;
            InvalidateRect(h, nullptr, FALSE);
        }
        break;
    }
    case WM_MOUSELEAVE:
        g_tracking = false;
        g_hoverReg = R_NONE; g_hoverIdx = -1;
        InvalidateRect(h, nullptr, FALSE);
        break;
    case WM_SETCURSOR: {
        if (LOWORD(l) == HTCLIENT) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(h, &pt);
            Hit hh = HitTest(pt);
            bool hand = hh.reg == R_CELL || hh.reg == R_TAG || hh.reg == R_ADDTAG ||
                        hh.reg == R_GROUP || (hh.reg >= R_BTN_CLOSE && hh.reg <= R_BTN_PIN);
            SetCursor(LoadCursorW(nullptr, hand ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    }
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA * S(48);
        int ns = std::max(0, std::min(g_maxScroll, g_scroll - delta));
        if (ns != g_scroll) { g_scroll = ns; InvalidateRect(h, nullptr, FALSE); }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT pt{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        Hit hh = HitTest(pt);
        switch (hh.reg) {
        case R_CELL: LaunchLink(const_cast<Node*>(g_view[hh.idx]), false); break;
        case R_BTN_CLOSE: HideWin(); break;
        case R_BTN_MENU: ShowMainMenu(); break;
        case R_BTN_PIN:
            g_pinned = !g_pinned; ApplyPin(); SaveWinPos();
            InvalidateRect(h, nullptr, FALSE);
            break;
        case R_TAG: {
            if (hh.idx == 0) g_curTag = 0;
            else {
                auto tl = TagListOf(g_curGroup);
                if (hh.idx - 1 < (int)tl.size()) g_curTag = tl[hh.idx - 1]->id;
            }
            ClearFilter(); Refresh();
            break;
        }
        case R_GROUP: {
            auto gl = GroupList();
            if (hh.idx < (int)gl.size()) SelectGroup(gl[hh.idx]->id);
            break;
        }
        case R_ADDTAG:
            if (g_curGroup) OpenEditor(NT_TAG, g_curGroup, nullptr);
            else MessageBoxW(h, L"请先添加分组。", L"启动器", MB_OK | MB_ICONINFORMATION);
            break;
        }
        return 0;
    }
    case WM_CONTEXTMENU: {
        if ((HWND)w != h) break;                              // 搜索框右键走系统默认
        POINT scr{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        if (scr.x == -1 && scr.y == -1) break;
        POINT pt = scr; ScreenToClient(h, &pt);
        Hit hh = HitTest(pt);
        switch (hh.reg) {
        case R_CELL: ShowLinkMenu(const_cast<Node*>(g_view[hh.idx]), scr); break;
        case R_TAG: {
            auto tl = TagListOf(g_curGroup);
            if (hh.idx >= 1 && hh.idx - 1 < (int)tl.size()) ShowTagMenu(tl[hh.idx - 1], scr);
            break;
        }
        case R_GROUP: {
            auto gl = GroupList();
            if (hh.idx < (int)gl.size()) ShowGroupMenu(gl[hh.idx], scr);
            else ShowGridBgMenu(scr);
            break;
        }
        case R_TITLE: ShowMainMenu(); break;
        default: ShowGridBgMenu(scr); break;
        }
        return 0;
    }
    case WM_COMMAND: OnSearchCommand(w, l); break;
    case WM_CTLCOLOREDIT:
        if ((HWND)l == g_hEdit && g_ph) {
            SetTextColor((HDC)w, RGB(165, 168, 172));
            SetBkColor((HDC)w, C_WHITE);
            return (LRESULT)g_brWhite;
        }
        break;
    case WM_TIMER:
        if (w == 1 && g_hideOnFocus && IsWindowVisible(h) && !g_pinned && !g_hDlg) {
            HWND fw = GetForegroundWindow();
            if (fw != h && (!fw || GetWindow(fw, GW_OWNER) != h)) HideWin();
        }
        break;
    case WM_HOTKEY:
        if (w == 1) ToggleWin();
        break;
    case WM_TRAY: {
        if (LOWORD(l) == WM_LBUTTONUP) ToggleWin();
        else if (LOWORD(l) == WM_RBUTTONUP) {
            POINT pt; GetCursorPos(&pt);
            HMENU mu = CreatePopupMenu();
            AppendMenuW(mu, MF_STRING, TR_SHOW, IsWindowVisible(h) ? L"隐藏" : L"显示");
            AppendMenuW(mu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(mu, MF_STRING, TR_EXIT, L"退出");
            SetForegroundWindow(h);
            int cmd = TrackPopupMenu(mu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, nullptr);
            DestroyMenu(mu);
            if (cmd == TR_SHOW) ToggleWin();
            if (cmd == TR_EXIT) Quit();
        }
        return 0;
    }
    case WM_DPICHANGED: {
        g_dpi = HIWORD(w);
        MakeFonts();
        RECT* r = (RECT*)l;
        SetWindowPos(h, nullptr, r->left, r->top,
                     r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(g_hEdit, nullptr, S(12), S(9), S(200), S(28), SWP_NOZORDER);
        SendMessageW(g_hEdit, WM_SETFONT, (WPARAM)g_fUi, TRUE);
        LayoutNow(); InvalidateRect(h, nullptr, FALSE);
        break;
    }
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        if (IsWindowVisible(h)) ClampToWorkArea();
        break;
    case WM_NCDESTROY:
        KillTimer(h, 1);
        UnregisterHotKey(h, 1);
        FreeMemDC();
        for (auto& kv : g_icons) if (kv.second) DestroyIcon(kv.second);
        FreeGdiRes();
        DeleteObject(g_brWhite); DeleteObject(g_brBg); DeleteObject(g_brGroupBg);
        DeleteObject(g_brHover); DeleteObject(g_brSplit); DeleteObject(g_brAccent);
        DeleteObject(g_penSplit); DeleteObject(g_penAccent);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------------- 入口 ----------------
typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);

static UINT WindowDpi(HWND h) {
    static PFN_GetDpiForWindow fn = []() -> PFN_GetDpiForWindow {
        HMODULE u = GetModuleHandleW(L"user32.dll");
        return u ? (PFN_GetDpiForWindow)GetProcAddress(u, "GetDpiForWindow") : nullptr;
    }();
    return (fn && h) ? fn(h) : 96;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    g_hInst = hInst;
    // 单实例
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\LNP_LauncherMutex");
    bool already = (GetLastError() == ERROR_ALREADY_EXISTS);

    std::wstring dir = ExeDir();
    g_iniConf  = dir + L"conf.ini";
    g_iniLinks = dir + L"data\\links.ini";
    EnsureUnicodeIni(g_iniConf);
    EnsureUnicodeIni(g_iniLinks);

    if (already) {
        HWND h = FindWindowW(MAIN_CLASS, nullptr);
        if (h) { ShowWindow(h, SW_SHOW); SetForegroundWindow(h); }
        return 0;
    }

    g_hideOnFocus      = GetPrivateProfileIntW(L"behavior", L"hideOnFocus", 1, g_iniConf.c_str()) != 0;
    g_hideAfterLaunch  = GetPrivateProfileIntW(L"behavior", L"hideAfterLaunch", 1, g_iniConf.c_str()) != 0;
    g_sortByCount      = GetPrivateProfileIntW(L"ui", L"sortByCount", 0, g_iniConf.c_str()) != 0;
    g_pinned           = GetPrivateProfileIntW(L"ui", L"pinned", 0, g_iniConf.c_str()) != 0;
    g_startHidden      = GetPrivateProfileIntW(L"behavior", L"startHidden", 0, g_iniConf.c_str()) != 0;
    int wx = GetPrivateProfileIntW(L"ui", L"x", -32000, g_iniConf.c_str());
    int wy = GetPrivateProfileIntW(L"ui", L"y", -32000, g_iniConf.c_str());
    int ww = std::max(S(430), (int)GetPrivateProfileIntW(L"ui", L"w", S(660), g_iniConf.c_str()));
    int wh = std::max(S(340), (int)GetPrivateProfileIntW(L"ui", L"h", S(460), g_iniConf.c_str()));

    LoadNodes();
    {
        auto gl = GroupList();
        g_curGroup = gl.empty() ? 0 : gl[0]->id;
    }

    WNDCLASSEXW wc = { sizeof wc };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = MAIN_CLASS;
    RegisterClassExW(&wc);

    WNDCLASSW dc = {};
    dc.lpfnWndProc = DlgProc;
    dc.hInstance = hInst;
    dc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    dc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    dc.lpszClassName = DLG_CLASS;
    RegisterClassW(&dc);

    MakeFonts();
    CreateGdiRes();

    RECT wr{ 0, 0, ww, wh };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    g_hwnd = CreateWindowExW(0, MAIN_CLASS, L"启动面板",
                             WS_OVERLAPPEDWINDOW,
                             wx == -32000 ? CW_USEDEFAULT : wx,
                             wy == -32000 ? CW_USEDEFAULT : wy,
                             wr.right - wr.left, wr.bottom - wr.top,
                             nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return 1;
    g_dpi = (int)WindowDpi(g_hwnd);
    MakeFonts();                                            // 按实际 DPI 重做字体
    Refresh();
    if (g_pinned) ApplyPin();

    if (!g_startHidden) ShowWin();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (g_hDlg && IsDialogMessageW(g_hDlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CloseHandle(mutex);
    return 0;
}
