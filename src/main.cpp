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
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cwctype>
#include "version.h"

namespace gp = Gdiplus;

// ---------------- 数据模型 ----------------
enum { NT_GROUP = 0, NT_TAG = 1, NT_LINK = 2 };
enum { R_NONE = 0, R_TITLE, R_TAG, R_ADDTAG, R_CELL, R_BTN_CLOSE, R_BTN_MENU, R_BTN_PIN };

struct Node {
    int id = 0, type = 0, parent = 0;
    std::wstring name, path, params;
};

// 菜单命令 ID
enum {
    M_ADDTAG = 101, M_ADDLINK, M_PIN, M_HIDEFOCUS, M_HOTKEY, M_EXIT,
    L_OPEN = 201, L_ADMIN, L_LOC, L_EDIT, L_DEL,
    T_ADDLINK = 301, T_REN, T_DEL,
    BG_ADDTAG = 501, BG_ADDLINK,
    TR_SHOW = 601, TR_EXIT
};

// 编辑框控件 ID
enum { IDC_NAME = 1001, IDC_PATH, IDC_PARAMS, IDC_BROWSE };

// ---------------- 全局状态 ----------------
static const wchar_t MAIN_CLASS[] = L"LNP_MainWnd";
static const wchar_t DLG_CLASS[]  = L"LNP_EditWnd";
static const UINT    WM_TRAY = WM_APP + 1;

static HINSTANCE g_hInst;
static HWND      g_hwnd, g_hDlg, g_hName, g_hPath, g_hParams;
static int       g_dpi = 96;

static std::vector<Node> g_nodes;
static int  g_nextId = 1;
static int  g_curTag = 0;               // 节点 id；0 = "全部"
static std::vector<const Node*> g_view;
static std::map<int, HICON> g_icons;
static int  g_scroll = 0, g_cols = 4, g_maxScroll = 0;

static bool g_pinned = false, g_hideOnFocus = true, g_hideAfterLaunch = true;
static bool g_tracking = false, g_startHidden = false;
static int  g_hoverReg = R_NONE, g_hoverIdx = -1;

static std::wstring g_iniConf, g_iniLinks;
static NOTIFYICONDATAW g_nid = {};

// 布局区域
static RECT g_rcTitle, g_rcTag, g_rcGrid;
static std::vector<RECT> g_rcTabs;   // [0]="全部"，之后为各标签
static RECT g_rcAddTag;
static RECT g_rcBtns[3];             // 0=关闭 1=菜单 2=钉住
static RECT g_rcDlgX;                // 编辑对话框右上角关闭区
struct DlgBtn { RECT rc; int id; const wchar_t* txt; bool primary; };
static std::vector<DlgBtn> g_dlgBtns;
static std::vector<RECT>   g_dlgEdits;
static HWND g_hHK = nullptr;         // 热键对话框（编辑对话框句柄见 g_hDlg）
// GDI 资源
static HFONT g_fUi, g_fUiB, g_fSmall, g_fGlyph, g_fTitle;
static HBRUSH g_brBg, g_brCard, g_brInset;
static HDC g_memDC = nullptr; static HBITMAP g_memBmp = nullptr, g_memOld = nullptr;
static int g_memW = 0, g_memH = 0;
static ULONG_PTR g_gdipToken;

// 配色（米白轻拟物，与 kaozb/app 同源：卡片浮起、输入内凹、胶囊标签）
static const COLORREF C_BG    = RGB(240, 234, 220);   // 米白背景
static const COLORREF C_WHITE = RGB(255, 255, 255);
static const COLORREF C_CARD  = RGB(247, 242, 231);   // 卡片面
static const COLORREF C_INSET = RGB(236, 229, 212);   // 输入框内凹面
static const COLORREF C_SHAD  = RGB(146, 128, 98);    // 投影基色（GDI+ 低透明叠层）
static const COLORREF C_TEXT  = RGB(62, 57, 48);      // 主文字（暖深灰）
static const COLORREF C_MUT   = RGB(154, 146, 130);   // 次要文字
static const COLORREF C_ACC   = RGB(124, 154, 109);   // 主色（灰绿）
static const COLORREF C_HOVER = RGB(244, 239, 228);   // 卡片悬停面
static const COLORREF C_SPLIT = RGB(222, 213, 193);   // 控件描边
static const COLORREF C_EDGE  = RGB(209, 198, 174);   // 窗口描边

// ---------------- 前向声明 ----------------
static void Refresh();
static void LayoutNow();
static void DrawTextRect(HDC, const RECT&, const wchar_t*, HFONT, COLORREF, UINT);
static void DrawGlyph(HDC, const RECT&, wchar_t, COLORREF);
static void GdipSetup(gp::Graphics&);
static void GdipFillRound(gp::Graphics&, const RECT&, FLOAT, COLORREF, BYTE);
static void GdipStrokeRound(gp::Graphics&, const RECT&, FLOAT, COLORREF, BYTE, FLOAT);
static void GdipCard(gp::Graphics&, const RECT&, FLOAT, COLORREF);
static void DrawDlgFace(HWND, HDC);
static void EnableRoundedCorner(HWND, COLORREF);
static void LaunchLink(Node* n, bool admin);
static void OpenEditor(int type, int parent, const Node* src);
static void HideWin();
static void ShowMainMenu();
static void OpenHotkeyDlg();

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
    Node t; t.id = 1; t.type = NT_TAG; t.name = L"常用";
    Node l; l.id = 2; l.type = NT_LINK; l.parent = 1; l.name = L"腾讯文档"; l.path = L"https://docs.qq.com";
    g_nodes = { t, l };
    g_curTag = 1;
    g_nextId = 3;
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
        g_nodes.push_back(n);
    }
    if (g_nodes.empty()) Seed();
}

// ---------------- 视图 ----------------
static void RebuildView() {
    g_view.clear(); g_scroll = 0;
    for (auto& n : g_nodes) {
        if (n.type != NT_LINK) continue;
        if (g_curTag == 0 || n.parent == g_curTag) g_view.push_back(&n);
    }
}

static std::vector<Node*> TagList() {
    std::vector<Node*> v;
    for (auto& n : g_nodes) if (n.type == NT_TAG) v.push_back(&n);
    return v;
}

// 返回可挂链接的标签 id：优先当前标签，其次第一个标签；一个都没有就建"常用"
static int EnsureCurTag() {
    if (g_curTag) return g_curTag;
    auto tl = TagList();
    if (!tl.empty()) return tl[0]->id;
    Node t; t.id = g_nextId++; t.type = NT_TAG; t.name = L"常用";
    g_nodes.push_back(t); SaveNode(t); SaveNextId();
    g_curTag = t.id;
    return t.id;
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

// 手动 layered alpha 淡入淡出：AnimateWindow 临时增删 WS_EX_LAYERED 会触发 NC 重排，
// 偶尔闪出一帧系统老边框；直接操纵 alpha 无此副作用（短 Sleep 在显示/隐藏场景无感）
static void FadeIn(HWND h, int ms) {
    if (!IsWindowVisible(h)) {
        SetWindowLongPtrW(h, GWL_EXSTYLE, GetWindowLongPtrW(h, GWL_EXSTYLE) | WS_EX_LAYERED);
        SetLayeredWindowAttributes(h, 0, 0, LWA_ALPHA);
        ShowWindow(h, SW_SHOW);
    }
    for (int i = 1; i <= 8; i++) {
        SetLayeredWindowAttributes(h, 0, (BYTE)(255 * i / 8), LWA_ALPHA);
        Sleep(ms / 8);
    }
    SetWindowLongPtrW(h, GWL_EXSTYLE, GetWindowLongPtrW(h, GWL_EXSTYLE) & ~WS_EX_LAYERED);
}

static void FadeOut(HWND h, int ms) {
    if (!IsWindowVisible(h)) return;
    SetWindowLongPtrW(h, GWL_EXSTYLE, GetWindowLongPtrW(h, GWL_EXSTYLE) | WS_EX_LAYERED);
    for (int i = 7; i >= 0; i--) {
        SetLayeredWindowAttributes(h, 0, (BYTE)(255 * i / 8), LWA_ALPHA);
        Sleep(ms / 8);
    }
    ShowWindow(h, SW_HIDE);
    SetWindowLongPtrW(h, GWL_EXSTYLE, GetWindowLongPtrW(h, GWL_EXSTYLE) & ~WS_EX_LAYERED);
}

static void ShowWin() {
    FadeIn(g_hwnd, 140);
    ClampToWorkArea();
    SetForegroundWindow(g_hwnd);
}

static void HideWin() {
    SaveWinPos();
    FadeOut(g_hwnd, 120);
}
static void ToggleWin() { IsWindowVisible(g_hwnd) ? HideWin() : ShowWin(); }

// ---------------- GDI 资源 ----------------
static void FreeGdiRes() {
    for (HFONT f : { g_fUi, g_fUiB, g_fSmall, g_fGlyph, g_fTitle }) if (f) DeleteObject(f);
    g_fUi = g_fUiB = g_fSmall = g_fGlyph = g_fTitle = nullptr;
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
    g_fTitle = CreateFontW(-S(15), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
}

static void CreateGdiRes() {
    g_brBg    = CreateSolidBrush(C_BG);
    g_brCard  = CreateSolidBrush(C_CARD);
    g_brInset = CreateSolidBrush(C_INSET);
}

static void FreeMemDC() {
    if (g_memBmp) { SelectObject(g_memDC, g_memOld); DeleteObject(g_memBmp); g_memBmp = nullptr; }
    if (g_memDC) { DeleteDC(g_memDC); g_memDC = nullptr; }
}

// ---------------- 布局与命中测试 ----------------
static int TitleH() { return S(46); }
static int TagH()   { return S(40); }
static int CellW()  { return S(104); }
static int CellH()  { return S(98); }
static int Gap()    { return S(8); }

static void LayoutNow() {
    RECT rc; GetClientRect(g_hwnd, &rc);
    g_rcTitle = { 0, 0, rc.right, TitleH() };
    g_rcTag   = { 0, TitleH(), rc.right, TitleH() + TagH() };
    g_rcGrid  = { 0, TitleH() + TagH(), rc.right, rc.bottom };

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
    auto tl = TagList();
    int x = g_rcTag.left + Gap();
    auto addTab = [&](const wchar_t* s) {
        SIZE sz; GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz);
        RECT r{ x, g_rcTag.top, x + sz.cx + S(24), g_rcTag.bottom };
        g_rcTabs.push_back(r);
        x = r.right;
    };
    addTab(L"全部");
    for (auto* t : tl) addTab(t->name.c_str());
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

struct Hit { int reg = R_NONE; int idx = -1; };

static Hit HitTest(POINT p) {
    Hit h;
    if (g_rcBtns[0].right == 0) return h;
    if (PtInRect(&g_rcTitle, p)) {
        for (int i = 0; i < 3; i++)
            if (PtInRect(&g_rcBtns[i], p)) { h.reg = R_BTN_CLOSE + i; return h; }
        h.reg = R_TITLE;
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
        if (n.type == NT_TAG) g_curTag = n.id;
    } else if (Node* n = Find(g_ctx.id)) {
        n->name = g_ctx.name; n->path = g_ctx.path; n->params = g_ctx.params;
        SaveNode(*n);
    }
    Refresh();
    DestroyWindow(g_hDlg);
}

// 编辑/热键对话框共用：自绘按钮绘制与命中
static const DlgBtn* FindDlgBtn(POINT pt) {
    for (auto& b : g_dlgBtns) if (PtInRect(&b.rc, pt)) return &b;
    return nullptr;
}

static void DrawDlgBtns(HDC dc) {
    gp::Graphics g(dc); GdipSetup(g);
    for (auto& b : g_dlgBtns) {
        FLOAT rad = (FLOAT)(b.rc.bottom - b.rc.top) / 2;      // 胶囊
        if (b.primary) {
            GdipCard(g, b.rc, rad, C_ACC);
            DrawTextRect(dc, b.rc, b.txt, g_fUi, C_WHITE,
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            GdipFillRound(g, b.rc, rad, C_INSET, 255);
            GdipStrokeRound(g, b.rc, rad, C_SPLIT, 160, 1.0f);
            DrawTextRect(dc, b.rc, b.txt, g_fUi, C_TEXT,
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
}

// 编辑/热键两弹窗共用的整面绘制（标题 + 内凹输入框 + 按钮）
static void DrawDlgFace(HWND h, HDC dc) {
    RECT rc; GetClientRect(h, &rc);
    FillRect(dc, &rc, g_brCard);
    wchar_t title[128]; GetWindowTextW(h, title, 128);
    DrawTextRect(dc, { S(16), 0, S(280), S(36) }, title, g_fTitle, C_TEXT,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    gp::Graphics g(dc); GdipSetup(g);
    for (auto& r : g_dlgEdits) {                              // 内凹输入框底 + 描边
        GdipFillRound(g, r, S(8), C_INSET, 255);
        GdipStrokeRound(g, r, S(8), C_SPLIT, 200, 1.0f);
    }
    DrawDlgBtns(dc);
    DrawGlyph(dc, g_rcDlgX, 0xE711, C_MUT);
}

static LRESULT CALLBACK DlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        // 自绘对话框：仅 EDIT 是子窗口，标签/按钮/边框全部 WM_PAINT 绘制
        int type = (int)(INT_PTR)((CREATESTRUCTW*)l)->lpCreateParams;
        g_dlgBtns.clear(); g_dlgEdits.clear();
        g_hName = g_hPath = g_hParams = nullptr;
        HINSTANCE inst = ((CREATESTRUCTW*)l)->hInstance;
        auto label = [&](const wchar_t* t, int px, int py) {
            CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE,
                            px, py, S(48), S(18), h, nullptr, inst, nullptr);
        };
        auto edit = [&](int px, int py, int pw, const wchar_t* txt, int id) -> HWND {
            g_dlgEdits.push_back({ px, py, px + pw, py + S(28) });   // 外圈：父窗口画圆角底+描边
            HWND c = CreateWindowExW(0, L"EDIT", txt, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                     px + S(2), py + S(2), pw - S(4), S(28) - S(4),
                                     h, (HMENU)(INT_PTR)id, inst, nullptr);
            SendMessageW(c, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(S(8), S(8)));
            return c;
        };
        auto btn = [&](int px, int py, int pw, int id, const wchar_t* txt, bool pri) {
            g_dlgBtns.push_back({ { px, py, px + pw, py + S(28) }, id, txt, pri });
        };
        int x = S(16), lw = S(48), fw = S(380) - x * 2 - lw;
        label(L"名称", x, S(46) + S(5));
        g_hName = edit(x + lw, S(46), fw, g_ctx.name.c_str(), IDC_NAME);
        int by;
        if (type == NT_LINK) {
            label(L"目标", x, S(88) + S(5));
            g_hPath = edit(x + lw, S(88), fw - S(80), g_ctx.path.c_str(), IDC_PATH);
            btn(S(380) - x - S(72), S(88), S(72), IDC_BROWSE, L"浏览…", false);
            label(L"参数", x, S(130) + S(5));
            g_hParams = edit(x + lw, S(130), fw, g_ctx.params.c_str(), IDC_PARAMS);
            by = S(170);
        } else {
            by = S(96);
        }
        btn(S(380) - x - S(88) * 2 - S(8), by, S(88), IDOK, L"确定", true);
        btn(S(380) - x - S(88), by, S(88), IDCANCEL, L"取消", false);
        g_rcDlgX = { S(380) - S(40), 0, S(380) - S(8), S(36) };
        SendMessageW(g_hName, EM_SETSEL, 0, -1);
        SetFocus(g_hName);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        DrawDlgFace(h, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_NCACTIVATE:
        return DefWindowProcW(h, m, w, -1);
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w;
        SetBkColor(dc, C_INSET); SetTextColor(dc, C_TEXT);
        SelectObject(dc, g_fUi);
        return (LRESULT)g_brInset;     // 必须实心刷：HOLLOW 刷走 PatBlt 会整块变黑
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)w;
        SetBkColor(dc, C_CARD); SetTextColor(dc, C_TEXT);
        SelectObject(dc, g_fUi);
        return (LRESULT)g_brCard;
    }
    case WM_LBUTTONDOWN: {
        POINT pt{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        if (PtInRect(&g_rcDlgX, pt)) { DestroyWindow(h); return 0; }
        if (const DlgBtn* b = FindDlgBtn(pt)) {
            if (b->id == IDOK) EditorApply();
            else if (b->id == IDCANCEL) DestroyWindow(h);
            else if (b->id == IDC_BROWSE) {
                wchar_t file[1024] = L"";
                if (g_hPath) GetWindowTextW(g_hPath, file, 1024);
                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof ofn;
                ofn.hwndOwner = h;
                ofn.lpstrFilter = L"程序/链接 (*.exe;*.lnk;*.url;*.bat)\0*.exe;*.lnk;*.url;*.bat\0所有文件\0*.*\0";
                ofn.lpstrFile = file; ofn.nMaxFile = 1024;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
                if (GetOpenFileNameW(&ofn)) { SetWindowTextW(g_hPath, file); SetFocus(g_hPath); }
            }
            return 0;
        }
        if (pt.y < S(36)) {                                     // 标题栏拖动
            ReleaseCapture();
            SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
        return 0;
    }
    case WM_SETCURSOR: {
        if (LOWORD(l) == HTCLIENT) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(h, &pt);
            bool hand = PtInRect(&g_rcDlgX, pt) || FindDlgBtn(pt);
            SetCursor(LoadCursorW(nullptr, hand ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(w)) {
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
    if (g_hHK) DestroyWindow(g_hHK);   // 按钮数组全局共享，禁止两个对话框并发
    g_ctx = EditCtx();
    g_ctx.isNew = !src;
    if (src) {
        g_ctx.id = src->id; g_ctx.type = src->type; g_ctx.parent = src->parent;
        g_ctx.name = src->name; g_ctx.path = src->path; g_ctx.params = src->params;
    } else {
        g_ctx.type = type; g_ctx.parent = parent;
    }
    const wchar_t* title = g_ctx.isNew
        ? (type == NT_LINK ? L"添加链接" : L"添加标签")
        : L"编辑";
    int cw = S(380), ch = (g_ctx.type == NT_LINK) ? S(210) : S(140);
    RECT mr{};                                                  // 居中于主窗口
    if (g_hwnd) GetWindowRect(g_hwnd, &mr);
    int dx = mr.left + ((mr.right - mr.left) - cw) / 2;
    int dy = mr.top + ((mr.bottom - mr.top) - ch) / 2;
    g_hDlg = CreateWindowExW(0, DLG_CLASS, title, WS_POPUP,
                             dx, dy, cw, ch,
                             g_hwnd, nullptr, g_hInst, (LPVOID)(INT_PTR)g_ctx.type);
    if (g_hDlg) {
        EnableRoundedCorner(g_hDlg, C_EDGE);
        FadeIn(g_hDlg, 120);
    }
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
    AppendMenuW(m, MF_STRING, M_ADDTAG, L"添加标签");
    AppendMenuW(m, MF_STRING, M_ADDLINK, L"添加链接");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (g_pinned ? MF_CHECKED : 0), M_PIN,
                g_pinned ? L"取消钉住" : L"钉住");
    AppendMenuW(m, MF_STRING | (g_hideOnFocus ? MF_CHECKED : 0), M_HIDEFOCUS, L"失焦自动隐藏");
    AppendMenuW(m, MF_STRING, M_HOTKEY, L"快捷键设置…");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, M_EXIT, L"退出");
    RECT r = g_rcBtns[1];
    int cmd = PopupMenuClient(m, { r.left, r.bottom + S(4) });
    switch (cmd) {
    case M_ADDTAG: OpenEditor(NT_TAG, 0, nullptr); break;
    case M_ADDLINK: OpenEditor(NT_LINK, EnsureCurTag(), nullptr); break;
    case M_PIN: g_pinned = !g_pinned; ApplyPin(); SaveWinPos(); InvalidateRect(g_hwnd, nullptr, FALSE); break;
    case M_HIDEFOCUS: g_hideOnFocus = !g_hideOnFocus; SaveWinPos(); break;
    case M_HOTKEY: OpenHotkeyDlg(); break;
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

static void ShowGridBgMenu(POINT scr) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, BG_ADDLINK, L"添加链接");
    AppendMenuW(m, MF_STRING, BG_ADDTAG, L"添加标签");
    switch (PopupMenuScreen(m, scr)) {
    case BG_ADDLINK: OpenEditor(NT_LINK, EnsureCurTag(), nullptr); break;
    case BG_ADDTAG:  OpenEditor(NT_TAG, 0, nullptr); break;
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

// ---------------- GDI+ 抗锯齿绘制 ----------------
static gp::Color Col(COLORREF c, BYTE a) {
    return gp::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

static void GdipSetup(gp::Graphics& g) {
    g.SetSmoothingMode(gp::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(gp::PixelOffsetModeHighQuality);
}

static void GdipPathRound(gp::GraphicsPath& p, const gp::RectF& rf, FLOAT rad) {
    if (rad > rf.Width / 2)  rad = rf.Width / 2;              // 胶囊 = 高度一半
    if (rad > rf.Height / 2) rad = rf.Height / 2;
    FLOAT d = rad * 2;
    p.AddArc(rf.X, rf.Y, d, d, 180, 90);
    p.AddArc(rf.X + rf.Width - d, rf.Y, d, d, 270, 90);
    p.AddArc(rf.X + rf.Width - d, rf.Y + rf.Height - d, d, d, 0, 90);
    p.AddArc(rf.X, rf.Y + rf.Height - d, d, d, 90, 90);
    p.CloseFigure();
}

static void GdipFillRound(gp::Graphics& g, const RECT& r, FLOAT rad, COLORREF c, BYTE a) {
    gp::GraphicsPath p;
    GdipPathRound(p, gp::RectF((FLOAT)r.left, (FLOAT)r.top,
                               (FLOAT)(r.right - r.left), (FLOAT)(r.bottom - r.top)), rad);
    gp::SolidBrush b(Col(c, a));
    g.FillPath(&b, &p);
}

static void GdipStrokeRound(gp::Graphics& g, const RECT& r, FLOAT rad, COLORREF c, BYTE a, FLOAT w) {
    gp::GraphicsPath p;
    GdipPathRound(p, gp::RectF((FLOAT)r.left + 0.5f, (FLOAT)r.top + 0.5f,
                               (FLOAT)(r.right - r.left) - 1, (FLOAT)(r.bottom - r.top) - 1), rad);
    gp::Pen pen(Col(c, a), w);
    g.DrawPath(&pen, &p);
}

// 轻拟物浮起卡片：3 层暖影 + 本体 + 1px 高光描边
static void GdipCard(gp::Graphics& g, const RECT& r, FLOAT rad, COLORREF body) {
    for (int i = 1; i <= 3; i++) {
        RECT rs{ r.left, r.top + i, r.right, r.bottom + i };
        GdipFillRound(g, rs, rad, C_SHAD, (BYTE)(21 - i * 6));
    }
    GdipFillRound(g, r, rad, body, 255);
    GdipStrokeRound(g, r, rad, C_WHITE, 110, 1.0f);
}

// Win11 系统圆角窗口 + 描边色；Win10 无此属性，静默跳过
static void EnableRoundedCorner(HWND h, COLORREF border) {
    typedef LONG (WINAPI *PFN_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
    static PFN_DwmSetWindowAttribute fn = []() -> PFN_DwmSetWindowAttribute {
        HMODULE m = LoadLibraryW(L"dwmapi.dll");
        return m ? (PFN_DwmSetWindowAttribute)GetProcAddress(m, "DwmSetWindowAttribute") : nullptr;
    }();
    if (!fn) return;
    LONG pref = 2;                                        // DWMWCP_ROUND
    fn(h, 33, &pref, sizeof pref);                        // DWMWA_WINDOW_CORNER_PREFERENCE
    COLORREF cap = C_BG;
    fn(h, 35, &cap, sizeof cap);                          // DWMWA_CAPTION_COLOR 兜底
    fn(h, 34, &border, sizeof border);                    // DWMWA_BORDER_COLOR 描边
}

static void PaintAll(HDC dc) {
    auto tl = TagList();
    RECT rcAll{ 0, 0, g_rcTitle.right, g_rcGrid.bottom };
    FillRect(dc, &rcAll, g_brBg);
    gp::Graphics g(dc); GdipSetup(g);

    // 标题栏：左=应用名，右=日期 + 关闭/菜单/钉住
    {
        RECT nr{ S(14), 0, S(240), g_rcTitle.bottom };
        DrawTextRect(dc, nr, L"启动面板", g_fTitle, C_TEXT,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t db[32]; wsprintfW(db, L"%d月%d日", st.wMonth, st.wDay);
    RECT dr{ 0, 0, g_rcBtns[2].left - S(6), g_rcTitle.bottom };
    DrawTextRect(dc, dr, db, g_fUi, C_MUT, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    const wchar_t glyphs[3] = { 0xE711, 0xE700, 0xE718 };   // 关闭 菜单 钉住
    for (int i = 0; i < 3; i++) {
        bool hot = (g_hoverReg == R_BTN_CLOSE + i);
        if (hot && !(i == 2 && g_pinned)) {
            GdipFillRound(g, g_rcBtns[i], S(9), C_CARD, 255);
            GdipStrokeRound(g, g_rcBtns[i], S(9), C_SPLIT, 120, 1.0f);
        }
        COLORREF c = C_MUT;
        if (i == 2 && g_pinned) {                             // 已钉住：主色胶囊 + 白色图钉，一眼可辨
            GdipFillRound(g, g_rcBtns[2], S(9), C_ACC, 255);
            c = C_WHITE;
        } else if (hot) {
            c = C_TEXT;
        }
        DrawGlyph(dc, g_rcBtns[i], glyphs[i], c);
    }

    // 标签栏：胶囊标签，选中=主色浮起
    for (int i = 0; i < (int)g_rcTabs.size(); i++) {
        bool sel = (i == 0) ? (g_curTag == 0)
                            : (i - 1 < (int)tl.size() && tl[i - 1]->id == g_curTag);
        bool hot = (g_hoverReg == R_TAG && g_hoverIdx == i);
        const wchar_t* txt = (i == 0) ? L"全部" : tl[i - 1]->name.c_str();
        FLOAT rad = (FLOAT)(g_rcTabs[i].bottom - g_rcTabs[i].top) / 2;
        if (sel)      GdipCard(g, g_rcTabs[i], rad, C_ACC);
        else if (hot) GdipCard(g, g_rcTabs[i], rad, C_CARD);
        DrawTextRect(dc, g_rcTabs[i], txt, sel ? g_fUiB : g_fUi,
                     sel ? C_WHITE : (hot ? C_TEXT : C_MUT),
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    DrawGlyph(dc, g_rcAddTag, 0xE710, (g_hoverReg == R_ADDTAG) ? C_TEXT : C_MUT);

    // 网格：轻拟物浮起卡片
    if (g_view.empty()) {
        const wchar_t* hint = g_curTag ? L"右键空白处添加链接，或直接拖入文件" : L"右键添加标签";
        DrawTextRect(dc, g_rcGrid, hint, g_fUi, C_MUT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        for (int i = 0; i < (int)g_view.size(); i++) {
            RECT r = CellRect(i);
            if (r.bottom < g_rcGrid.top || r.top > g_rcGrid.bottom) continue;
            bool hot = (g_hoverReg == R_CELL && g_hoverIdx == i);
            GdipCard(g, r, S(12), hot ? C_HOVER : C_CARD);
            int isz = S(32);
            HICON ic = IconFor(*g_view[i]);
            if (ic) DrawIconEx(dc, r.left + (CellW() - isz) / 2, r.top + S(14), ic,
                               isz, isz, 0, nullptr, DI_NORMAL);
            RECT nr{ r.left + S(6), r.top + S(14) + isz + S(6), r.right - S(6),
                     r.top + S(14) + isz + S(6) + S(18) };
            DrawTextRect(dc, nr, g_view[i]->name.c_str(), g_fUi, C_TEXT,
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    }
}

// ---------------- 交互辅助 ----------------
static void SelectTag(int id) {
    g_curTag = id;
    Refresh();
}

// ---------------- 快捷键设置 ----------------
static const wchar_t HK_CLASS[] = L"LNP_HotkeyWnd";
struct HKState { int mod = 0, vk = 0; };
static HKState g_hk;

static std::wstring HotkeyText(int mod, int vk) {
    std::wstring s;
    if (mod & MOD_CONTROL) s += L"Ctrl+";
    if (mod & MOD_ALT)     s += L"Alt+";
    if (mod & MOD_SHIFT)   s += L"Shift+";
    if (mod & MOD_WIN)     s += L"Win+";
    if (vk) {
        wchar_t kb[64] = L"";
        if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
            wsprintfW(kb, L"%c", (wchar_t)vk);
        else if (!GetKeyNameTextW((LONG)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16, kb, 64))
            lstrcpyW(kb, L"?");
        s += kb;
    }
    return s;
}

// 注销旧热键 → 注册新热键；失败则按 conf.ini 旧值回滚
static bool ApplyHotkey(int mod, int vk) {
    UnregisterHotKey(g_hwnd, 1);
    if (RegisterHotKey(g_hwnd, 1, mod | MOD_NOREPEAT, (UINT)vk)) {
        wchar_t b[32];
        wsprintfW(b, L"%d", mod); WritePrivateProfileStringW(L"hot", L"mod", b, g_iniConf.c_str());
        wsprintfW(b, L"%d", vk);  WritePrivateProfileStringW(L"hot", L"vk",  b, g_iniConf.c_str());
        return true;
    }
    int om  = GetPrivateProfileIntW(L"hot", L"mod", MOD_CONTROL | MOD_ALT, g_iniConf.c_str());
    int ovk = GetPrivateProfileIntW(L"hot", L"vk", 'L', g_iniConf.c_str());
    RegisterHotKey(g_hwnd, 1, om | MOD_NOREPEAT, (UINT)ovk);
    return false;
}

static LRESULT CALLBACK HKProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        g_hk.mod = GetPrivateProfileIntW(L"hot", L"mod", MOD_CONTROL | MOD_ALT, g_iniConf.c_str());
        g_hk.vk  = GetPrivateProfileIntW(L"hot", L"vk", 'L', g_iniConf.c_str());
        CreateWindowExW(0, L"STATIC", L"直接按下新的快捷键组合（单独一个键也可以）",
                        WS_CHILD | WS_VISIBLE, S(16), S(44), S(300) - S(32), S(20),
                        h, nullptr, g_hInst, nullptr);
        CreateWindowExW(0, L"STATIC", HotkeyText(g_hk.mod, g_hk.vk).c_str(),
                        WS_CHILD | WS_VISIBLE | SS_CENTER, S(16), S(72), S(300) - S(32), S(30),
                        h, (HMENU)(INT_PTR)1, g_hInst, nullptr);
        g_dlgBtns.clear(); g_dlgEdits.clear();
        g_dlgBtns.push_back({ { S(300) - S(16) - S(88) * 2 - S(8), S(112),
                                S(300) - S(16) - S(88) - S(8), S(140) }, IDOK, L"确定", true });
        g_dlgBtns.push_back({ { S(300) - S(16) - S(88), S(112),
                                S(300) - S(16), S(140) }, IDCANCEL, L"取消", false });
        g_rcDlgX = { S(300) - S(40), 0, S(300) - S(8), S(36) };
        SetFocus(h);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        DrawDlgFace(h, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_NCACTIVATE:
        return DefWindowProcW(h, m, w, -1);
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)w;
        bool big = (HWND)l && GetDlgCtrlID((HWND)l) == 1;      // 键值预览：加粗主色
        SetBkColor(dc, C_CARD);
        SetTextColor(dc, big ? C_ACC : C_TEXT);
        SelectObject(dc, big ? g_fUiB : g_fUi);
        return (LRESULT)g_brCard;
    }
    case WM_LBUTTONDOWN: {
        POINT pt{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        if (PtInRect(&g_rcDlgX, pt)) { DestroyWindow(h); return 0; }
        if (const DlgBtn* b = FindDlgBtn(pt)) {
            SendMessageW(h, WM_COMMAND, b->id, 0);
            return 0;
        }
        if (pt.y < S(36)) {
            ReleaseCapture();
            SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
        return 0;
    }
    case WM_SETCURSOR: {
        if (LOWORD(l) == HTCLIENT) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(h, &pt);
            bool hand = PtInRect(&g_rcDlgX, pt) || FindDlgBtn(pt);
            SetCursor(LoadCursorW(nullptr, hand ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    }
    case WM_ACTIVATE:
        if (LOWORD(w)) SetFocus(h);
        return 0;
    case WM_KEYDOWN: {
        if (w == VK_ESCAPE) { DestroyWindow(h); return 0; }
        if (w == VK_RETURN) { SendMessageW(h, WM_COMMAND, IDOK, 0); return 0; }
        int mod = 0;
        if (GetKeyState(VK_CONTROL) < 0) mod |= MOD_CONTROL;
        if (GetKeyState(VK_MENU) < 0)    mod |= MOD_ALT;
        if (GetKeyState(VK_SHIFT) < 0)   mod |= MOD_SHIFT;
        if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) mod |= MOD_WIN;
        if (w == VK_CONTROL || w == VK_MENU || w == VK_SHIFT || w == VK_LWIN || w == VK_RWIN) {
            g_hk.mod = mod;                                   // 只按修饰键：先记下
        } else {
            g_hk.mod = mod; g_hk.vk = (int)w;                 // 任意键均可，含无修饰键的裸键
            SetWindowTextW(GetDlgItem(h, 1), HotkeyText(g_hk.mod, g_hk.vk).c_str());
        }
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDOK: {
            if (!g_hk.vk) {
                MessageBoxW(h, L"请先按下要设置的快捷键。", L"启动器", MB_OK | MB_ICONWARNING);
                return 0;
            }
            if (!ApplyHotkey(g_hk.mod, g_hk.vk)) {
                MessageBoxW(h, L"该快捷键可能已被其他程序占用，请换一个。", L"启动器", MB_OK | MB_ICONWARNING);
                return 0;
            }
            DestroyWindow(h);
            return 0;
        }
        case IDCANCEL: DestroyWindow(h); return 0;
        }
        break;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_NCDESTROY: g_hHK = nullptr; return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void OpenHotkeyDlg() {
    if (g_hHK) { SetForegroundWindow(g_hHK); return; }
    if (g_hDlg) DestroyWindow(g_hDlg);   // 按钮数组全局共享，禁止两个对话框并发
    int cw = S(300), ch = S(156);
    RECT mr{};
    if (g_hwnd) GetWindowRect(g_hwnd, &mr);
    g_hHK = CreateWindowExW(0, HK_CLASS, L"快捷键设置", WS_POPUP,
                            mr.left + ((mr.right - mr.left) - cw) / 2,
                            mr.top + ((mr.bottom - mr.top) - ch) / 2,
                            cw, ch, g_hwnd, nullptr, g_hInst, nullptr);
    if (g_hHK) {
        EnableRoundedCorner(g_hHK, C_EDGE);
        FadeIn(g_hHK, 120);
    }
}

// ---------------- 主窗口过程 ----------------
static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        SetTimer(h, 1, 300, nullptr);
        DragAcceptFiles(h, TRUE);
        EnableRoundedCorner(h, C_EDGE);
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
    case WM_NCCALCSIZE: {
        if (!w) break;
        if (IsZoomed(h)) {                // 最大化：窗口比屏幕大一圈边框，客户区内缩，避免内容溢出
            RECT br{ 0, 0, 0, 0 };
            AdjustWindowRect(&br, WS_THICKFRAME | WS_CAPTION, FALSE);
            RECT* rcp = (RECT*)l;
            rcp->left -= br.left; rcp->top -= br.top;
            rcp->right -= br.right; rcp->bottom -= br.bottom;
        }
        return 0;                         // 无边框：整个窗口为客户区
    }
    case WM_NCACTIVATE:
        return DefWindowProcW(h, m, w, -1);   // -1：激活切换时禁止系统重绘非客户区（防闪老边框）
    case WM_NCHITTEST: {
        POINT pt{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        ScreenToClient(h, &pt);
        RECT rc; GetClientRect(h, &rc);
        if (!IsZoomed(h)) {               // 最大化时不提供边缘缩放
            int e = S(6);
            bool Lt = pt.x < e, Rt = pt.x >= rc.right - e, Tp = pt.y < e, Bt = pt.y >= rc.bottom - e;
            if (Tp && Lt) return HTTOPLEFT;
            if (Tp && Rt) return HTTOPRIGHT;
            if (Bt && Lt) return HTBOTTOMLEFT;
            if (Bt && Rt) return HTBOTTOMRIGHT;
            if (Lt) return HTLEFT; if (Rt) return HTRIGHT;
            if (Tp) return HTTOP;  if (Bt) return HTBOTTOM;
        }
        Hit hh = HitTest(pt);
        if (PtInRect(&g_rcTitle, pt) &&
            !(hh.reg >= R_BTN_CLOSE && hh.reg <= R_BTN_PIN))
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
                        (hh.reg >= R_BTN_CLOSE && hh.reg <= R_BTN_PIN);
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
        case R_ADDTAG:
            OpenEditor(NT_TAG, 0, nullptr);
            break;
        case R_TAG: {
            auto tl = TagList();
            if (hh.idx == 0) g_curTag = 0;
            else if (hh.idx - 1 < (int)tl.size()) g_curTag = tl[hh.idx - 1]->id;
            Refresh();
            break;
        }
        }
        return 0;
    }
    case WM_CONTEXTMENU: {
        if ((HWND)w != h) break;
        POINT scr{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        if (scr.x == -1 && scr.y == -1) break;
        POINT pt = scr; ScreenToClient(h, &pt);
        Hit hh = HitTest(pt);
        switch (hh.reg) {
        case R_CELL: ShowLinkMenu(const_cast<Node*>(g_view[hh.idx]), scr); break;
        case R_TAG: {
            auto tl = TagList();
            if (hh.idx >= 1 && hh.idx - 1 < (int)tl.size()) ShowTagMenu(tl[hh.idx - 1], scr);
            break;
        }
        case R_ADDTAG: ShowGridBgMenu(scr); break;
        case R_TITLE: ShowMainMenu(); break;
        default: ShowGridBgMenu(scr); break;
        }
        return 0;
    }
    case WM_DROPFILES: {                                  // 拖入文件/文件夹 → 追加为链接
        HDROP d = (HDROP)w;
        int parent = EnsureCurTag();
        UINT n = parent ? DragQueryFileW(d, 0xFFFFFFFF, nullptr, 0) : 0;
        for (UINT i = 0; i < n; i++) {
            wchar_t p[1024];
            if (!DragQueryFileW(d, i, p, 1024)) continue;
            Node nd;
            nd.id = g_nextId++; nd.type = NT_LINK; nd.parent = parent;
            PCWSTR base = wcsrchr(p, L'\\'); base = base ? base + 1 : p;
            nd.name = base;
            size_t dot = nd.name.find_last_of(L'.');
            if (dot != std::wstring::npos && dot > 0) nd.name.resize(dot);   // 去扩展名，可再编辑
            nd.path = p;
            g_nodes.push_back(nd); SaveNode(nd);
        }
        if (n) SaveNextId();
        DragFinish(d);
        if (n) Refresh();
        return 0;
    }
    case WM_TIMER:
        if (w == 1 && g_hideOnFocus && IsWindowVisible(h) && !g_pinned && !g_hDlg && !g_hHK) {
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
        DeleteObject(g_brBg); DeleteObject(g_brCard); DeleteObject(g_brInset);
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
    gp::GdiplusStartupInput gsi;
    gp::GdiplusStartup(&g_gdipToken, &gsi, nullptr);
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
    g_pinned           = GetPrivateProfileIntW(L"ui", L"pinned", 0, g_iniConf.c_str()) != 0;
    g_startHidden      = GetPrivateProfileIntW(L"behavior", L"startHidden", 0, g_iniConf.c_str()) != 0;
    int wx = GetPrivateProfileIntW(L"ui", L"x", -32000, g_iniConf.c_str());
    int wy = GetPrivateProfileIntW(L"ui", L"y", -32000, g_iniConf.c_str());
    int ww = std::max(S(430), (int)GetPrivateProfileIntW(L"ui", L"w", S(660), g_iniConf.c_str()));
    int wh = std::max(S(340), (int)GetPrivateProfileIntW(L"ui", L"h", S(460), g_iniConf.c_str()));

    LoadNodes();

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

    WNDCLASSW hk = {};
    hk.lpfnWndProc = HKProc;
    hk.hInstance = hInst;
    hk.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    hk.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    hk.lpszClassName = HK_CLASS;
    RegisterClassW(&hk);

    MakeFonts();
    CreateGdiRes();

    RECT wr{ 0, 0, ww, wh };
    // 无 WS_CAPTION：彻底杜绝系统标题栏；缩放/动画由 WS_THICKFRAME + WM_NCHITTEST 提供
    const DWORD winStyle = WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    AdjustWindowRect(&wr, winStyle, FALSE);
    g_hwnd = CreateWindowExW(0, MAIN_CLASS, L"启动面板",
                             winStyle,
                             wx == -32000 ? CW_USEDEFAULT : wx,
                             wy == -32000 ? CW_USEDEFAULT : wy,
                             wr.right - wr.left, wr.bottom - wr.top,
                             nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return 1;
    g_dpi = (int)WindowDpi(g_hwnd);
    MakeFonts();                                            // 按实际 DPI 重做字体
    Refresh();
    if (g_pinned) ApplyPin();
    // 强制 DWM 立即按我们的 NCCALCSIZE 重排非客户区（否则首帧残留系统标题栏，resize 后才消失）
    SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    if (!g_startHidden) ShowWin();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (g_hDlg && IsDialogMessageW(g_hDlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CloseHandle(mutex);
    gp::GdiplusShutdown(g_gdipToken);
    return 0;
}
