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
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include "version.h"

#pragma comment(lib, "ws2_32.lib")

namespace gp = Gdiplus;

// ---------------- 数据模型 ----------------
enum { NT_GROUP = 0, NT_TAG = 1, NT_LINK = 2 };
enum { R_NONE = 0, R_TITLE, R_TAG, R_CELL, R_BTN_CLOSE, R_BTN_MENU, R_BTN_PIN, R_BTN_PLAY };

struct Node {
    int id = 0, type = 0, parent = 0;
    int order = 0;                        // 拖拽排序键（旧文件缺省 = 读入序）
    std::wstring name, path, params;
};

// 菜单命令 ID
enum {
    M_ADDTAG = 101, M_ADDLINK, M_PIN, M_HIDEFOCUS, M_AUTOSTART, M_HOTKEY, M_EXIT, M_LISTEN,
    M_ICON0 = 111, M_ICON1, M_ICON2,              // 图标 小/中/大
    M_SORT0 = 121, M_SORT1, M_SORT2,               // 排序 添加顺序/名称升/名称降
    M_THEME0 = 131, M_THEME1,                      // 主题 深色/浅色
    L_OPEN = 201, L_ADMIN, L_LOC, L_EDIT, L_DEL,
    T_ADDLINK = 301, T_REN, T_DEL,
    BG_ADDTAG = 501, BG_ADDLINK,
    TR_SHOW = 601, TR_EXIT
};

// 编辑框控件 ID
enum { IDC_NAME = 1001, IDC_PATH, IDC_PARAMS, IDC_BROWSE, IDC_PORT, IDC_ALLOW, IDC_TSEC };

// ---------------- 全局状态 ----------------
static const wchar_t MAIN_CLASS[] = L"LNP_MainWnd";
static const wchar_t DLG_CLASS[]  = L"LNP_EditWnd";
static const UINT    WM_TRAY = WM_APP + 1;
static const UINT    WM_MSG  = WM_APP + 2;   // 监听线程 → 主线程：弹出消息框

static HINSTANCE g_hInst;
static HWND      g_hwnd, g_hDlg, g_hName, g_hPath, g_hParams;
static int       g_dpi = 96;

static std::vector<Node> g_nodes;
static int  g_nextId = 1;
static int  g_curTag = 0;               // 节点 id；0 = "全部"
static std::vector<const Node*> g_view;
static std::map<int, HICON> g_icons;
static int  g_scroll = 0, g_cols = 4, g_maxScroll = 0;
static int  g_iconIdx = 1;              // 图标大小：0 小 / 1 中 / 2 大
static int  g_sortMode = 0;             // 排序：0 添加顺序 / 1 名称升序 / 2 名称降序

static bool g_pinned = false, g_hideOnFocus = true, g_hideAfterLaunch = true;
static bool g_autoStart = true;         // 开机自启（HKCU Run），默认开
static bool g_tracking = false, g_startHidden = false;
static int  g_hoverReg = R_NONE, g_hoverIdx = -1;
static int  g_pressIdx = -1;            // 左键按下的网格单元：拖拽/单击共用起点
static bool g_dragging = false;
static POINT g_pressPt{};

static std::wstring g_iniConf, g_iniLinks;
static NOTIFYICONDATAW g_nid = {};

// 布局区域
static RECT g_rcTitle, g_rcTag, g_rcGrid;
static std::vector<RECT> g_rcTabs;   // [0]="全部"，之后为各标签
static RECT g_rcBtns[4];             // 0=关闭 1=菜单 2=钉住 3=播放
static RECT g_rcDlgX;                // 编辑对话框右上角关闭区
struct DlgBtn { RECT rc; int id; const wchar_t* txt; bool primary; };
static std::vector<DlgBtn> g_dlgBtns;
static std::vector<RECT>   g_dlgEdits;
static HWND g_hHK = nullptr;         // 热键对话框（编辑对话框句柄见 g_hDlg）

// 消息监听（HTTP）与屏幕弹框
static const wchar_t LSTN_CLASS[]  = L"LNP_ListenWnd";
static const wchar_t TOAST_CLASS[] = L"LNP_MsgWnd";
static HWND g_hLstn = nullptr, g_hPort = nullptr, g_hAllow = nullptr, g_hTSec = nullptr, g_hToast = nullptr;
static int  g_listenPort = 22222;        // 配置端口，0 = 随机
static int  g_listenActual = 0;          // 实际绑定端口
static int  g_toastSec = 5;              // 弹框停留秒数，0 = 不自动消失
static std::wstring g_listenAllow;       // 允许来源 IP 前缀（逗号分隔，空 = 不限）
static std::atomic<bool> g_listenOn{ false };
static std::wstring g_listenStatus;      // 监听对话框状态行
static SOCKET g_srvSock = INVALID_SOCKET;
static HANDLE g_listenThread = nullptr;
static bool   g_wsaOk = false;
static std::wstring g_msgText;           // 弹框当前文本
// GDI 资源
static HFONT g_fUi, g_fUiB, g_fSmall, g_fGlyph, g_fTitle;
static HBRUSH g_brBg, g_brCard, g_brInset;
static HDC g_memDC = nullptr; static HBITMAP g_memBmp = nullptr, g_memOld = nullptr;
static int g_memW = 0, g_memH = 0;
static ULONG_PTR g_gdipToken;

// 配色：同一组语义槽位，两套值。0=深色（默认，取自用户提供的 Global 配色表）1=浅色
static COLORREF C_BG, C_WHITE, C_BAR, C_CARD, C_HOVER, C_INSET, C_SHAD,
                C_TEXT, C_MUT, C_ACC, C_ONTXT, C_SPLIT, C_EDGE, C_MENU, C_MENUH;
static int g_theme = 0;

static void ApplyTheme(int idx) {
    g_theme = (idx == 1) ? 1 : 0;
    if (g_theme == 0) {
        C_BG    = RGB(32, 32, 32); C_BAR  = RGB(51, 51, 51); C_CARD = RGB(59, 59, 59);
        C_HOVER = RGB(69, 69, 69); C_INSET = RGB(43, 43, 43); C_SHAD = RGB(0, 0, 0);
        C_TEXT  = RGB(255, 255, 255); C_MUT = RGB(146, 146, 146);
        C_SPLIT = RGB(107, 107, 107); C_EDGE = RGB(36, 36, 36);
        C_MENU  = RGB(74, 74, 74);     C_MENUH = RGB(97, 97, 97);
    } else {
        C_BG    = RGB(237, 237, 237); C_BAR = RGB(248, 248, 248); C_CARD = RGB(255, 255, 255);
        C_HOVER = RGB(238, 244, 250); C_INSET = RGB(243, 243, 243); C_SHAD = RGB(70, 70, 70);
        C_TEXT  = RGB(34, 34, 34); C_MUT = RGB(124, 124, 124);
        C_SPLIT = RGB(212, 212, 212); C_EDGE = RGB(198, 198, 198);
        C_MENU  = RGB(255, 255, 255);  C_MENUH = RGB(238, 244, 250);
    }
    C_WHITE = RGB(255, 255, 255);
    C_ACC   = RGB(120, 150, 180);                       // 主色两套主题共用
    C_ONTXT = RGB(28, 32, 38);                          // 主色上的深色文字（白字对比度仅 2.6:1）
}

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
static bool EnableRoundedCorner(HWND, COLORREF);
static void ApplyRoundRegion(HWND, bool);
static void LaunchLink(Node* n, bool admin);
static void OpenEditor(int type, int parent, const Node* src);
static void HideWin();
static void ShowMainMenu();
static void OpenHotkeyDlg();
static void OpenListenDlg();
static void StopListener();

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
    WritePrivateProfileStringW(L"ui", L"theme", std::to_wstring(g_theme).c_str(), g_iniConf.c_str());
    WritePrivateProfileStringW(L"ui", L"curTag", std::to_wstring(g_curTag).c_str(), g_iniConf.c_str());
    WritePrivateProfileStringW(L"behavior", L"hideOnFocus", std::to_wstring(g_hideOnFocus ? 1 : 0).c_str(), g_iniConf.c_str());
    WritePrivateProfileStringW(L"behavior", L"hideAfterLaunch", std::to_wstring(g_hideAfterLaunch ? 1 : 0).c_str(), g_iniConf.c_str());
    WritePrivateProfileStringW(L"behavior", L"autostart", std::to_wstring(g_autoStart ? 1 : 0).c_str(), g_iniConf.c_str());
}

static void Quit() {
    StopListener();
    SaveWinPos();
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    DestroyWindow(g_hwnd);
}

// ---------------- 开机自启（HKCU\...\Run；随开关写/删，路径变化时自动刷新） ----------------
static void ApplyAutoStart(bool on) {
    HKEY k;
    if (on) {
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                            0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";   // 加引号防空格路径被截断
        RegSetValueExW(k, L"windowTAG", 0, REG_SZ, (const BYTE*)cmd.c_str(),
                       (DWORD)(cmd.size() + 1) * sizeof(wchar_t));
        RegCloseKey(k);
    } else {
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                          0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
        RegDeleteValueW(k, L"windowTAG");
        RegCloseKey(k);
    }
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
    WritePrivateProfileStringW(sec, L"order",  std::to_wstring(n.order).c_str(),  g_iniLinks.c_str());
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
    for (int i = 0; i < (int)g_nodes.size(); i++) g_nodes[i].order = i;
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
        n.order  = GetPrivateProfileIntW(sec, L"order", i, g_iniLinks.c_str());
        GetPrivateProfileStringW(sec, L"name", L"", buf, 1024, g_iniLinks.c_str());   n.name = buf;
        GetPrivateProfileStringW(sec, L"path", L"", buf, 1024, g_iniLinks.c_str());   n.path = buf;
        GetPrivateProfileStringW(sec, L"params", L"", buf, 1024, g_iniLinks.c_str()); n.params = buf;
        g_nodes.push_back(n);
    }
    std::stable_sort(g_nodes.begin(), g_nodes.end(),
                     [](const Node& a, const Node& b) { return a.order < b.order; });
    if (g_nodes.empty()) Seed();
}

// 拖拽结束后按 g_nodes 现顺序重排 order 键并落盘
static void SaveOrders() {
    for (int i = 0; i < (int)g_nodes.size(); i++) {
        if (g_nodes[i].order != i) { g_nodes[i].order = i; SaveNode(g_nodes[i]); }
    }
}

// 把视图里 from 位置的链接实时移到 to 位置；g_nodes 与 g_view 同步换位（元素地址不变，指针不失效）
static void DragMove(int from, int to) {
    if (from < 0 || to < 0 || from >= (int)g_view.size() || to >= (int)g_view.size() || from == to) return;
    Node* src = const_cast<Node*>(g_view[from]);
    Node* dst = const_cast<Node*>(g_view[to]);
    if (from < to) std::rotate(g_view.begin() + from, g_view.begin() + from + 1, g_view.begin() + to + 1);
    else           std::rotate(g_view.begin() + to, g_view.begin() + from, g_view.begin() + from + 1);
    auto si = std::find_if(g_nodes.begin(), g_nodes.end(), [&](const Node& n) { return &n == src; });
    auto di = std::find_if(g_nodes.begin(), g_nodes.end(), [&](const Node& n) { return &n == dst; });
    if (si == g_nodes.end() || di == g_nodes.end()) return;
    if (si < di) std::rotate(si, si + 1, di + 1);
    else         std::rotate(di, si, si + 1);
}

// ---------------- 视图 ----------------
static void RebuildView() {
    g_view.clear(); g_scroll = 0;
    for (auto& n : g_nodes) {
        if (n.type != NT_LINK) continue;
        if (g_curTag == 0 || n.parent == g_curTag) g_view.push_back(&n);
    }
    if (g_sortMode) {                                   // 0=添加顺序（g_nodes 原始次序）
        bool desc = (g_sortMode == 2);
        std::stable_sort(g_view.begin(), g_view.end(),
                         [desc](const Node* a, const Node* b) {
                             int c = _wcsicmp(a->name.c_str(), b->name.c_str());
                             return desc ? c > 0 : c < 0;
                         });
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
    Node t; t.id = g_nextId++; t.type = NT_TAG; t.name = L"常用"; t.order = (int)g_nodes.size();
    g_nodes.push_back(t); SaveNode(t); SaveNextId();
    Refresh();                                           // push_back 可能扩容挪动 g_nodes，g_view 指针须重建
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
static void FadeIn(HWND h, int ms, UINT show = SW_SHOW) {
    if (!IsWindowVisible(h)) {
        SetWindowLongPtrW(h, GWL_EXSTYLE, GetWindowLongPtrW(h, GWL_EXSTYLE) | WS_EX_LAYERED);
        SetLayeredWindowAttributes(h, 0, 0, LWA_ALPHA);
        ShowWindow(h, show);
        UpdateWindow(h);                 // 首帧先画完再提 alpha：否则会闪一帧未绘制/系统底色
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
    if (g_brBg) DeleteObject(g_brBg);
    if (g_brCard) DeleteObject(g_brCard);
    if (g_brInset) DeleteObject(g_brInset);
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
static int Gap()    { return S(8); }
static int IconPx() { static const int k[3] = { 24, 32, 44 }; return S(k[g_iconIdx]); }
static int CellW()  { static const int k[3] = { 92, 108, 128 }; return S(k[g_iconIdx]); }
static int TextLineH() { return S(17); }
static int CellH()  { return S(12) + IconPx() + S(6) + TextLineH() * 2 + S(12); }

// 内容面板：卡片布局的唯一坐标基准（画板、卡片、滚动范围都从这里算，避免互相越界）
static RECT PanelRect() {
    return { g_rcGrid.left + S(10), g_rcGrid.top + S(4),
             g_rcGrid.right - S(10), g_rcGrid.bottom - S(10) };
}

static void LayoutNow() {
    RECT rc; GetClientRect(g_hwnd, &rc);
    g_rcTitle = { 0, 0, rc.right, TitleH() };
    g_rcTag   = { 0, TitleH(), rc.right, TitleH() + TagH() };
    g_rcGrid  = { 0, TitleH() + TagH(), rc.right, rc.bottom };

    // 标题栏按钮：0=关闭 1=菜单 2=钉住 3=播放
    int b = S(34);
    for (int i = 0; i < 4; i++) {
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

    // 网格列数与滚动范围
    RECT p = PanelRect();
    int avail = (p.right - p.left) - Gap() * 2;
    g_cols = std::max(1, (avail + Gap()) / (CellW() + Gap()));
    int rows = (int)((g_view.size() + g_cols - 1) / g_cols);
    int content = rows * (CellH() + Gap()) + Gap();
    g_maxScroll = std::max(0, content - (int)((p.bottom - p.top) - Gap()));
    g_scroll = std::min(g_scroll, g_maxScroll);
}

static RECT CellRect(int i) {
    int col = i % g_cols, row = i / g_cols;
    RECT p = PanelRect();
    int x = p.left + Gap() + col * (CellW() + Gap());
    int y = p.top + Gap() + row * (CellH() + Gap()) - g_scroll;
    return { x, y, x + CellW(), y + CellH() };
}

struct Hit { int reg = R_NONE; int idx = -1; };

static Hit HitTest(POINT p) {
    Hit h;
    if (g_rcBtns[0].right == 0) return h;
    if (PtInRect(&g_rcTitle, p)) {
        for (int i = 0; i < 4; i++)
            if (PtInRect(&g_rcBtns[i], p)) { h.reg = R_BTN_CLOSE + i; return h; }
        h.reg = R_TITLE;
        return h;
    }
    if (PtInRect(&g_rcTag, p)) {
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
        n.order = (int)g_nodes.size();
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
            DrawTextRect(dc, b.rc, b.txt, g_fUi, C_ONTXT,
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
    case WM_ERASEBKGND:                                   // 自己整面重绘，禁止系统用类底色擦除（白色闪一下）
        return 1;
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
    if (g_hHK) DestroyWindow(g_hHK);       // 按钮数组全局共享，禁止对话框并发
    if (g_hLstn) DestroyWindow(g_hLstn);
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
        ApplyRoundRegion(g_hDlg, true);          // 无边框弹窗 DWM 不圆角，直接用区域兜底
        FadeIn(g_hDlg, 120);
    }
}

// ---------------- 自绘弹出菜单 ----------------
// 系统菜单在深色主题下是白底灰框，和主体完全不搭。这里自绘：圆角、软阴影、同款字体与配色。
// 用 WS_EX_LAYERED + UpdateLayeredWindow 做逐像素 alpha，圆角才是抗锯齿的（SetWindowRgn 会留锯齿边）。
struct MItem {
    int id = 0;
    std::wstring text;
    bool sep = false;
    bool check = false;
    std::vector<MItem> sub;
};
static void MAdd(std::vector<MItem>& v, int id, const wchar_t* t, bool ck = false) {
    MItem it; it.id = id; it.text = t; it.check = ck; v.push_back(it);
}
static void MSep(std::vector<MItem>& v) { MItem it; it.sep = true; v.push_back(it); }
static void MSub(std::vector<MItem>& v, const wchar_t* t, std::vector<MItem> s) {
    MItem it; it.text = t; it.sub = std::move(s); v.push_back(it);
}

static int MenuM()     { return S(10); }      // 四周阴影留白
static int MenuRad()   { return S(10); }
static int MenuItemH() { return S(30); }
static int MenuSepH()  { return S(9); }
static int MenuPad()   { return S(5); }

struct MPanel {
    std::vector<MItem> items;
    std::vector<RECT>  rc;                    // 条目矩形（客户区坐标，已含阴影留白）
    int pw = 0, ph = 0, sw = 0, sh = 0;
    int hover = -1;
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    MPanel* child = nullptr;
    int src = -1;                             // 子菜单来自哪一条目（用于切换子菜单）
    POINT pos{};
    HDC dc = nullptr; HBITMAP bmp = nullptr, oldBmp = nullptr; void* bits = nullptr;
};
static std::vector<MPanel*> g_panels;
static bool g_menuDone = true, g_menuClosing = false, g_lbtnDown = true;
static int  g_menuPick = 0;
static const wchar_t MENU_CLASS[] = L"LNP_MenuWnd";
static void MenuCloseAll();
static void MenuCloseSub(MPanel*);
static void MenuOpenSub(MPanel*, int);

static void MenuLayout(MPanel* p) {
    int m = MenuM();
    HDC dc = GetDC(g_hwnd);
    HFONT of = (HFONT)SelectObject(dc, g_fUi);
    int maxw = S(60);
    for (auto& it : p->items) {
        if (it.sep) continue;
        SIZE sz{}; GetTextExtentPoint32W(dc, it.text.c_str(), (int)it.text.size(), &sz);
        if (sz.cx > maxw) maxw = sz.cx;
    }
    SelectObject(dc, of); ReleaseDC(g_hwnd, dc);
    p->pw = MenuPad() * 2 + S(20) + maxw + S(18);       // 勾选列 + 文字 + 箭头列
    int y = MenuPad();
    p->rc.clear();
    for (auto& it : p->items) {
        int ih = it.sep ? MenuSepH() : MenuItemH();
        p->rc.push_back({ m + MenuPad(), m + y, m + p->pw - MenuPad(), m + y + ih });
        y += ih;
    }
    p->ph = y + MenuPad();
    p->sw = p->pw + m * 2;
    p->sh = p->ph + m * 2;
}

// 圆角矩形有符号距离场：内部为负，外部为正；用于抗锯齿边缘与软阴影衰减
static float MenuSdf(float x, float y, float cx, float cy, float hw, float hh, float r) {
    float qx = fabsf(x - cx) - hw + r, qy = fabsf(y - cy) - hh + r;
    float ax = qx > 0 ? qx : 0, ay = qy > 0 ? qy : 0;
    float mx = qx > qy ? qx : qy;
    return sqrtf(ax * ax + ay * ay) + (mx < 0 ? mx : 0) - r;
}

static void MenuAlpha(MPanel* p) {
    int m = MenuM();
    float cx = (float)m + p->pw * 0.5f, cy = (float)m + p->ph * 0.5f;
    float hw = p->pw * 0.5f, hh = p->ph * 0.5f, rad = (float)MenuRad(), mm = (float)m;
    BYTE* px = (BYTE*)p->bits;
    for (int y = 0; y < p->sh; y++) {
        for (int x = 0; x < p->sw; x++) {
            BYTE* q = px + ((size_t)y * p->sw + x) * 4;
            float d = MenuSdf((float)x + 0.5f, (float)y + 0.5f, cx, cy, hw, hh, rad);
            float a;
            if (d <= -0.5f) a = 255.0f;
            else if (d < 0.5f) a = (0.5f - d) * 255.0f;          // 边缘抗锯齿
            else {
                float t = (d - 0.5f) / mm;
                a = t >= 1.0f ? 0.0f : (1.0f - t) * (1.0f - t) * 100.0f;   // 软阴影
            }
            float f = a / 255.0f;                                // AC_SRC_ALPHA 要求预乘
            q[0] = (BYTE)(q[0] * f); q[1] = (BYTE)(q[1] * f); q[2] = (BYTE)(q[2] * f);
            q[3] = (BYTE)a;
        }
    }
}

static void MenuRender(MPanel* p) {
    if (!p->dc) {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = p->sw;
        bi.bmiHeader.biHeight = -p->sh;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        p->dc = CreateCompatibleDC(nullptr);
        p->bmp = CreateDIBSection(p->dc, &bi, DIB_RGB_COLORS, &p->bits, nullptr, 0);
        p->oldBmp = (HBITMAP)SelectObject(p->dc, p->bmp);
    }
    memset(p->bits, 0, (size_t)p->sw * p->sh * 4);           // 透明黑：面板外即纯黑阴影
    int m = MenuM();
    RECT pr{ m, m, m + p->pw, m + p->ph };
    gp::Graphics g(p->dc); GdipSetup(g);
    GdipFillRound(g, pr, (FLOAT)MenuRad(), C_MENU, 255);
    for (size_t i = 0; i < p->items.size(); i++) {
        const MItem& it = p->items[i];
        RECT r = p->rc[i];
        if (it.sep) {
            int yy = (r.top + r.bottom) / 2;
            RECT ln{ r.left + S(6), yy, r.right - S(6), yy + 1 };
            GdipFillRound(g, ln, 0.5f, C_SPLIT, 255);
            continue;
        }
        if ((int)i == p->hover) GdipFillRound(g, r, (FLOAT)S(6), C_MENUH, 255);
        if (it.check) DrawGlyph(p->dc, { r.left + S(2), r.top, r.left + S(20), r.bottom }, 0xE73E, C_ACC);
        RECT tr{ r.left + S(20), r.top, r.right - S(18), r.bottom };
        DrawTextRect(p->dc, tr, it.text.c_str(), g_fUi, C_TEXT,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (!it.sub.empty())
            DrawGlyph(p->dc, { r.right - S(18), r.top, r.right, r.bottom }, 0xE76C, C_MUT);
    }
    MenuAlpha(p);
}

static void MenuCommit(MPanel* p) {
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    SIZE sz{ p->sw, p->sh };
    POINT src{ 0, 0 }, dst{ p->pos.x, p->pos.y };
    UpdateLayeredWindow(p->hwnd, nullptr, &dst, &sz, p->dc, &src, 0, &bf, ULW_ALPHA);
}

// activate=false 用于子菜单：不抢激活，父面板就不会收到 WA_INACTIVE 而误收起整套菜单
static void MenuShow(MPanel* p, POINT at, bool activate) {
    p->pos = at;
    g_panels.push_back(p);
    p->hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, MENU_CLASS, L"",
                              WS_POPUP, at.x, at.y, p->sw, p->sh, p->owner, nullptr, g_hInst, p);
    if (!p->hwnd) return;
    MenuRender(p);
    MenuCommit(p);
    ShowWindow(p->hwnd, activate ? SW_SHOW : SW_SHOWNOACTIVATE);
    if (activate) SetForegroundWindow(p->hwnd);
}

static void MenuFree(MPanel* p) {
    if (p->hwnd) DestroyWindow(p->hwnd);
    if (p->bmp) { SelectObject(p->dc, p->oldBmp); DeleteObject(p->bmp); }
    if (p->dc) DeleteDC(p->dc);
    delete p;
}

static void MenuCloseSub(MPanel* p) {
    if (!p->child) return;
    for (size_t i = 0; i < g_panels.size(); i++)
        if (g_panels[i] == p->child) { g_panels.erase(g_panels.begin() + i); break; }
    MenuFree(p->child);
    p->child = nullptr;
}

static void MenuCloseAll() {
    if (g_menuClosing) return;
    g_menuClosing = true;
    auto all = g_panels;
    g_panels.clear();
    for (auto* p : all) MenuFree(p);
    g_menuDone = true;
    g_menuClosing = false;
}

static void MenuOpenSub(MPanel* p, int idx) {
    if (p->child) {                          // 已开着子菜单：同一条目保持，换条目就切换
        if (p->child->src == idx) return;
        MenuCloseSub(p);
    }
    MPanel* c = new MPanel;
    c->items = p->items[idx].sub;
    c->owner = p->owner;
    MenuLayout(c);
    RECT pr; GetWindowRect(p->hwnd, &pr);
    int x = pr.left + MenuM() + p->pw - S(6);
    int y = pr.top + p->rc[idx].top - S(6);
    MONITORINFO mi{}; mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromPoint({ x, y }, MONITOR_DEFAULTTONEAREST), &mi);
    if (x + c->sw > mi.rcWork.right) x = pr.left - c->sw + MenuM() + S(6);   // 右边放不下就翻到左侧
    if (y + c->sh > mi.rcWork.bottom) y = mi.rcWork.bottom - c->sh;
    c->src = idx;
    p->child = c;
    MenuShow(c, { x, y }, false);
}

static LRESULT CALLBACK MenuProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    MPanel* p = (MPanel*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_CREATE:
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)l)->lpCreateParams);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; BeginPaint(h, &ps); EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!p) break;
        POINT pt{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        int hit = -1;
        for (size_t i = 0; i < p->rc.size(); i++)
            if (!p->items[i].sep && PtInRect(&p->rc[i], pt)) { hit = (int)i; break; }
        if (hit != p->hover) { p->hover = hit; MenuRender(p); MenuCommit(p); }
        if (hit >= 0 && !p->items[hit].sub.empty()) MenuOpenSub(p, hit);
        else if (hit >= 0 && p->child) MenuCloseSub(p);
        return 0;
    }
    case WM_LBUTTONUP: {
        if (!p) break;
        POINT pt{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        bool hitItem = false;
        for (size_t i = 0; i < p->rc.size(); i++) {
            if (p->items[i].sep || !PtInRect(&p->rc[i], pt)) continue;
            hitItem = true;
            if (p->items[i].sub.empty()) { g_menuPick = p->items[i].id; MenuCloseAll(); }
            break;
        }
        if (!hitItem) MenuCloseAll();       // 点在留白/阴影上：和系统菜单一样直接收起
        return 0;
    }
    case WM_RBUTTONUP:
        MenuCloseAll();
        return 0;
    case WM_TIMER: {
        // 兜底：菜单未抢到激活时也能靠全局左键状态感知"外部点击"而收起
        bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (down && !g_lbtnDown) {
            POINT c; GetCursorPos(&c);
            bool inside = false;
            for (auto* q : g_panels) {
                RECT wr; GetWindowRect(q->hwnd, &wr);
                if (PtInRect(&wr, c)) inside = true;
            }
            if (!inside) { g_lbtnDown = down; MenuCloseAll(); return 0; }
        }
        g_lbtnDown = down;
        return 0;
    }
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) { MenuCloseAll(); return 0; }
        break;
    case WM_ACTIVATE: {
        if (LOWORD(w) != WA_INACTIVE || g_menuClosing) break;
        HWND a = GetForegroundWindow();               // 激活切到本进程内的窗口（主窗口/别的菜单面板）不算离开
        if (!a) break;
        DWORD pid = 0; GetWindowThreadProcessId(a, &pid);
        if (pid != GetCurrentProcessId()) MenuCloseAll();   // 切到别的程序：收起整套菜单
        break;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

static int MPopup(std::vector<MItem> items, POINT scr, HWND owner = nullptr) {
    g_menuPick = 0;
    g_menuDone = false;
    g_lbtnDown = true;                       // 打开菜单的左键可能正按着（按钮触发），不能算外部点击
    MPanel* p = new MPanel;
    p->items = std::move(items);
    p->owner = owner ? owner : g_hwnd;       // 托盘场景主窗口是隐藏的，不能在隐藏 owner 下挂弹出菜单
    MenuLayout(p);
    MONITORINFO mi{}; mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromPoint(scr, MONITOR_DEFAULTTONEAREST), &mi);
    int x = scr.x, y = scr.y;
    if (x + p->sw > mi.rcWork.right)  x = mi.rcWork.right - p->sw;
    if (y + p->sh > mi.rcWork.bottom) y = mi.rcWork.bottom - p->sh;
    MenuShow(p, { x, y }, true);
    if (p->hwnd) SetTimer(p->hwnd, 1, 20, nullptr);
    MSG msg;
    while (!g_menuDone && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    if (!g_menuDone) PostQuitMessage((int)msg.wParam);
    MenuCloseAll();
    return g_menuPick;
}

static void ShowMainMenu() {
    std::vector<MItem> mi, ms, mt, m;                    // 图标大小 / 排序方式 / 主题 / 主菜单
    static const wchar_t* kIcon[3] = { L"小", L"中", L"大" };
    static const wchar_t* kSort[3] = { L"添加顺序", L"名称升序", L"名称降序" };
    static const wchar_t* kTheme[2] = { L"深色", L"浅色" };
    for (int i = 0; i < 3; i++) MAdd(mi, M_ICON0 + i, kIcon[i], g_iconIdx == i);
    for (int i = 0; i < 3; i++) MAdd(ms, M_SORT0 + i, kSort[i], g_sortMode == i);
    for (int i = 0; i < 2; i++) MAdd(mt, M_THEME0 + i, kTheme[i], g_theme == i);
    MAdd(m, M_ADDTAG, L"添加标签");
    MAdd(m, M_ADDLINK, L"添加链接");
    MSub(m, L"图标大小", mi);
    MSub(m, L"排序方式", ms);
    MSub(m, L"主题", mt);
    MSep(m);
    MAdd(m, M_PIN, g_pinned ? L"取消钉住" : L"钉住", g_pinned);
    MAdd(m, M_HIDEFOCUS, L"失焦自动隐藏", g_hideOnFocus);
    MAdd(m, M_AUTOSTART, L"开机自启", g_autoStart);
    MAdd(m, M_HOTKEY, L"快捷键设置…");
    MAdd(m, M_LISTEN, L"监听设置…");
    MSep(m);
    MAdd(m, M_EXIT, L"退出");
    RECT r = g_rcBtns[1];
    POINT s{ r.left, r.bottom + S(4) };
    ClientToScreen(g_hwnd, &s);
    int cmd = MPopup(std::move(m), s);
    switch (cmd) {
    case M_ADDTAG: OpenEditor(NT_TAG, 0, nullptr); break;
    case M_ADDLINK: OpenEditor(NT_LINK, EnsureCurTag(), nullptr); break;
    case M_PIN: g_pinned = !g_pinned; ApplyPin(); SaveWinPos(); InvalidateRect(g_hwnd, nullptr, FALSE); break;
    case M_HIDEFOCUS: g_hideOnFocus = !g_hideOnFocus; SaveWinPos(); break;
    case M_AUTOSTART:
        g_autoStart = !g_autoStart;
        ApplyAutoStart(g_autoStart);
        SaveWinPos();
        break;
    case M_HOTKEY: OpenHotkeyDlg(); break;
    case M_LISTEN: OpenListenDlg(); break;
    case M_EXIT: Quit(); break;
    default:
        if (cmd >= M_ICON0 && cmd <= M_ICON2) {
            g_iconIdx = cmd - M_ICON0;
            WritePrivateProfileStringW(L"ui", L"iconSize", std::to_wstring(g_iconIdx).c_str(),
                                       g_iniConf.c_str());
            Refresh();
        } else if (cmd >= M_SORT0 && cmd <= M_SORT2) {
            g_sortMode = cmd - M_SORT0;
            WritePrivateProfileStringW(L"ui", L"sort", std::to_wstring(g_sortMode).c_str(),
                                       g_iniConf.c_str());
            Refresh();
        } else if (cmd >= M_THEME0 && cmd <= M_THEME1) {
            ApplyTheme(cmd - M_THEME0);
            CreateGdiRes();                              // 画刷跟着主题重建
            EnableRoundedCorner(g_hwnd, C_EDGE);         // 标题栏/描边色同步
            ApplyRoundRegion(g_hwnd, false);
            WritePrivateProfileStringW(L"ui", L"theme", std::to_wstring(g_theme).c_str(),
                                       g_iniConf.c_str());
            InvalidateRect(g_hwnd, nullptr, TRUE);
        }
        break;
    }
}

static void ShowLinkMenu(Node* n, POINT scr) {
    bool hasDir = n->path.find(L'\\') != std::wstring::npos;
    std::vector<MItem> m;
    MAdd(m, L_OPEN, L"打开");
    MAdd(m, L_ADMIN, L"以管理员身份运行");
    if (hasDir) MAdd(m, L_LOC, L"打开所在位置");
    MSep(m);
    MAdd(m, L_EDIT, L"编辑");
    MAdd(m, L_DEL, L"删除");
    switch (MPopup(std::move(m), scr)) {
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
    std::vector<MItem> m;
    MAdd(m, T_ADDLINK, L"添加链接");
    MSep(m);
    MAdd(m, T_REN, L"重命名");
    MAdd(m, T_DEL, L"删除");
    switch (MPopup(std::move(m), scr)) {
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
    std::vector<MItem> m;
    MAdd(m, BG_ADDLINK, L"添加链接");
    MAdd(m, BG_ADDTAG, L"添加标签");
    switch (MPopup(std::move(m), scr)) {
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

// 按宽度折行：优先在空格断，否则逐字断；行数超限时末行截断并补省略号
static std::vector<std::wstring> WrapText(HDC dc, const std::wstring& s, int maxW, int maxLines) {
    std::vector<std::wstring> lines;
    if (s.empty() || maxW <= 0) return lines;
    HFONT of = (HFONT)SelectObject(dc, g_fUi);
    auto W = [&](const std::wstring& t) {
        SIZE sz{}; GetTextExtentPoint32W(dc, t.c_str(), (int)t.size(), &sz); return (int)sz.cx;
    };
    std::wstring cur;
    for (wchar_t ch : s) {
        std::wstring nx = cur + ch;
        if (!cur.empty() && W(nx) > maxW) {
            size_t sp = cur.find_last_of(L' ');
            if (sp != std::wstring::npos && sp > 0) {
                lines.push_back(cur.substr(0, sp));
                cur = cur.substr(sp + 1) + ch;
            } else {
                lines.push_back(cur);
                cur = std::wstring(1, ch);
            }
        } else {
            cur = nx;
        }
    }
    if (!cur.empty()) lines.push_back(cur);
    if ((int)lines.size() > maxLines) {
        lines.resize(maxLines);
        std::wstring last = lines.back();
        while (!last.empty() && W(last + L"…") > maxW) last.pop_back();
        lines.back() = last + L"…";
    }
    SelectObject(dc, of);
    return lines;
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

// 轻拟物浮起卡片：3 层淡影 + 本体 + 1px 高光描边（浅色主题需要更重的投影才看得出浮起）
static void GdipCard(gp::Graphics& g, const RECT& r, FLOAT rad, COLORREF body) {
    int base = (g_theme == 0) ? 21 : 30;
    for (int i = 1; i <= 3; i++) {
        RECT rs{ r.left, r.top + i, r.right, r.bottom + i };
        GdipFillRound(g, rs, rad, C_SHAD, (BYTE)(base - i * 6));
    }
    GdipFillRound(g, r, rad, body, 255);
    GdipStrokeRound(g, r, rad, C_WHITE, 22, 1.0f);
}

// Win11 系统圆角窗口 + 描边色；返回是否成功（Win10 无此属性，syscall 返回错误码）
static bool EnableRoundedCorner(HWND h, COLORREF border) {
    typedef LONG (WINAPI *PFN_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
    static PFN_DwmSetWindowAttribute fn = []() -> PFN_DwmSetWindowAttribute {
        HMODULE m = LoadLibraryW(L"dwmapi.dll");
        return m ? (PFN_DwmSetWindowAttribute)GetProcAddress(m, "DwmSetWindowAttribute") : nullptr;
    }();
    if (!fn) return false;
    LONG pref = 2;                                        // DWMWCP_ROUND
    HRESULT hr = fn(h, 33, &pref, sizeof pref);           // DWMWA_WINDOW_CORNER_PREFERENCE
    COLORREF cap = C_BG;
    fn(h, 35, &cap, sizeof cap);                          // DWMWA_CAPTION_COLOR 兜底
    fn(h, 34, &border, sizeof border);                    // DWMWA_BORDER_COLOR 描边
    return SUCCEEDED(hr);
}

// 无 DWM 圆角的场景（Win10 / 无边框弹窗）用窗口区域兜底，否则四角是直角
static bool g_dwmRound = false;                          // 主窗口是否已由 DWM 提供抗锯齿圆角
static void ApplyRoundRegion(HWND h, bool force) {
    if (!force && g_dwmRound) { SetWindowRgn(h, nullptr, TRUE); return; }
    RECT rc; GetClientRect(h, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return;
    if (IsZoomed(h)) { SetWindowRgn(h, nullptr, TRUE); return; }   // 最大化不切角
    int rad = S(16);
    HRGN r = CreateRoundRectRgn(0, 0, rc.right + 1, rc.bottom + 1, rad, rad);
    if (r) SetWindowRgn(h, r, TRUE);                     // 成功后 region 归系统所有，不可删除
}

static void PaintAll(HDC dc) {
    auto tl = TagList();
    RECT rcAll{ 0, 0, g_rcTitle.right, g_rcGrid.bottom };
    FillRect(dc, &rcAll, g_brBg);
    gp::Graphics g(dc); GdipSetup(g);

    // 三区层级递进：标题/标签栏=BG → 内容面板=BAR → 卡片=CARD，亮度逐级浮起
    RECT panel = PanelRect();
    GdipFillRound(g, panel, S(14), C_BAR, 255);
    GdipStrokeRound(g, panel, S(14), C_SPLIT, 140, 1.0f);

    // 标题栏：左=应用名，右=日期 + 关闭/菜单/钉住
    {
        RECT nr{ S(14), 0, S(240), g_rcTitle.bottom };
        DrawTextRect(dc, nr, L"启动面板", g_fTitle, C_TEXT,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t db[32]; wsprintfW(db, L"%d月%d日", st.wMonth, st.wDay);
    RECT dr{ 0, 0, g_rcBtns[3].left - S(6), g_rcTitle.bottom };
    DrawTextRect(dc, dr, db, g_fUi, C_MUT, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    const wchar_t glyphs[4] = { 0xE711, 0xE700, 0xE718, 0xE768 };   // 关闭 菜单 钉住 播放
    for (int i = 0; i < 4; i++) {
        bool sticky = (i == 2 && g_pinned) || (i == 3 && g_listenOn);
        bool hot = (g_hoverReg == R_BTN_CLOSE + i);
        if (hot && !sticky) {
            GdipFillRound(g, g_rcBtns[i], S(9), C_CARD, 255);
            GdipStrokeRound(g, g_rcBtns[i], S(9), C_SPLIT, 120, 1.0f);
        }
        COLORREF c = C_MUT;
        if (sticky) {                                         // 钉住/监听中：主色胶囊，一眼可辨
            GdipFillRound(g, g_rcBtns[i], S(9), C_ACC, 255);
            c = C_ONTXT;
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
                     sel ? C_ONTXT : (hot ? C_TEXT : C_MUT),
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    // 网格：轻拟物浮起卡片（裁剪到面板内，滚动时图标/文字不会溢出画板）
    RECT inner = panel;
    InflateRect(&inner, -S(2), -S(2));
    if (g_view.empty()) {
        const wchar_t* hint = g_curTag ? L"右键空白处添加链接，或直接拖入文件" : L"右键添加标签";
        DrawTextRect(dc, inner, hint, g_fUi, C_MUT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        int isz = IconPx();
        int saved = SaveDC(dc);
        IntersectClipRect(dc, inner.left, inner.top, inner.right, inner.bottom);
        gp::Graphics gc(dc); GdipSetup(gc);
        for (int i = 0; i < (int)g_view.size(); i++) {
            RECT r = CellRect(i);
            if (r.bottom < inner.top || r.top > inner.bottom) continue;
            bool hot = (g_hoverReg == R_CELL && g_hoverIdx == i && !(g_dragging && g_pressIdx == i));
            bool drag = (g_dragging && g_pressIdx == i);
            GdipCard(gc, r, S(12), (hot || drag) ? C_HOVER : C_CARD);
            if (drag) GdipStrokeRound(gc, r, S(12), C_ACC, 255, 2.0f);   // 拖拽中的卡片描主色边
            HICON ic = IconFor(*g_view[i]);
            int iy = r.top + S(12);
            if (ic) DrawIconEx(dc, r.left + (CellW() - isz) / 2, iy, ic,
                               isz, isz, 0, nullptr, DI_NORMAL);
            int ty = iy + isz + S(6);
            auto lines = WrapText(dc, g_view[i]->name, CellW() - S(12), 2);
            if (lines.size() == 1) ty += TextLineH() / 2;      // 单行时垂直居中，不贴顶
            for (size_t k = 0; k < lines.size(); k++) {
                RECT lr{ r.left + S(6), ty + (int)k * TextLineH(),
                         r.right - S(6), ty + (int)(k + 1) * TextLineH() };
                DrawTextRect(dc, lr, lines[k].c_str(), g_fUi, C_TEXT,
                             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
        RestoreDC(dc, saved);
    }
}

// ---------------- 交互辅助 ----------------
static void SelectTag(int id) {
    g_curTag = id;
    WritePrivateProfileStringW(L"ui", L"curTag", std::to_wstring(g_curTag).c_str(),
                               g_iniConf.c_str());
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
                        WS_CHILD | WS_VISIBLE | SS_CENTER, S(16), S(66), S(300) - S(32), S(28),
                        h, (HMENU)(INT_PTR)1, g_hInst, nullptr);
        g_dlgBtns.clear(); g_dlgEdits.clear();
        g_dlgBtns.push_back({ { S(300) - S(16) - S(88) * 2 - S(8), S(128),
                                S(300) - S(16) - S(88) - S(8), S(156) }, IDOK, L"确定", true });
        g_dlgBtns.push_back({ { S(300) - S(16) - S(88), S(128),
                                S(300) - S(16), S(156) }, IDCANCEL, L"取消", false });
        g_rcDlgX = { S(300) - S(40), 0, S(300) - S(8), S(36) };
        SetFocus(h);
        return 0;
    }
    case WM_PAINT: {                                      // 画对话框脸面（标题/X/按钮），缺了只剩 STATIC 文字悬空
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        DrawDlgFace(h, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_NCACTIVATE:
        return DefWindowProcW(h, m, w, -1);
    case WM_ERASEBKGND:                                   // 同编辑弹窗：避免系统底色闪一帧
        return 1;
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
    if (g_hDlg) DestroyWindow(g_hDlg);     // 按钮数组全局共享，禁止对话框并发
    if (g_hLstn) DestroyWindow(g_hLstn);
    int cw = S(300), ch = S(188);
    RECT mr{};
    if (g_hwnd) GetWindowRect(g_hwnd, &mr);
    g_hHK = CreateWindowExW(0, HK_CLASS, L"快捷键设置", WS_POPUP,
                            mr.left + ((mr.right - mr.left) - cw) / 2,
                            mr.top + ((mr.bottom - mr.top) - ch) / 2,
                            cw, ch, g_hwnd, nullptr, g_hInst, nullptr);
    if (g_hHK) {
        EnableRoundedCorner(g_hHK, C_EDGE);
        ApplyRoundRegion(g_hHK, true);
        FadeIn(g_hHK, 120);
    }
}

// ---------------- 消息监听（HTTP → 屏幕弹框） ----------------
static std::string WideToUtf8(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    if (n) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// 宽松解析 {"msg":"XXXX"}：取到 msg 字符串值即成功（支持常规转义，容忍 UTF-8 BOM）
static bool ParseMsgJson(const std::wstring& body, std::wstring& out) {
    size_t i = body.find_first_not_of(L" \t\r\n\xFEFF");
    if (i == std::wstring::npos || body[i] != L'{') return false;
    size_t k = body.find(L"\"msg\"", i);
    if (k == std::wstring::npos) return false;
    k = body.find(L':', k + 5);
    if (k == std::wstring::npos) return false;
    k = body.find_first_not_of(L" \t\r\n", k + 1);
    if (k == std::wstring::npos || body[k] != L'"') return false;
    k++;
    while (k < body.size()) {
        wchar_t ch = body[k++];
        if (ch == L'"') return true;
        if (ch != L'\\') { out += ch; continue; }
        if (k >= body.size()) return false;
        wchar_t e = body[k++];
        switch (e) {
        case L'"':  out += L'"';  break;
        case L'\\': out += L'\\'; break;
        case L'/':  out += L'/';  break;
        case L'n':  out += L'\n'; break;
        case L'r':  out += L'\r'; break;
        case L't':  out += L'\t'; break;
        case L'b':  out += L'\b'; break;
        case L'f':  out += L'\f'; break;
        case L'u': {
            if (k + 4 > body.size()) return false;
            int v = 0;
            for (int j = 0; j < 4; j++) {
                wchar_t hc = body[k++];
                v <<= 4;
                if (hc >= L'0' && hc <= L'9')      v += hc - L'0';
                else if (hc >= L'a' && hc <= L'f') v += hc - L'a' + 10;
                else if (hc >= L'A' && hc <= L'F') v += hc - L'A' + 10;
                else return false;
            }
            out += (wchar_t)v;
            break;
        }
        default: return false;
        }
    }
    return false;
}

// 来源 IP 前缀白名单：allow 空 = 全放行；前缀按 , ， ; ； 空格 分隔，对点分十进制 IP 字面前缀匹配
static bool IpAllowed(const wchar_t* ip, const std::wstring& allow) {
    if (allow.empty()) return true;
    size_t i = 0;
    while (i < allow.size()) {
        size_t j = i;
        while (j < allow.size() && allow[j] != L',' && allow[j] != L'，' &&
               allow[j] != L';' && allow[j] != L'；' && allow[j] != L' ') j++;
        if (j > i && wcsncmp(ip, allow.c_str() + i, j - i) == 0) return true;
        i = j + 1;
    }
    return false;
}

static void SendHttp(SOCKET c, int code, const char* status, const std::string& body) {
    char hdr[160];
    int n = snprintf(hdr, sizeof hdr,
                     "HTTP/1.1 %d %s\r\nContent-Type: text/plain; charset=utf-8\r\n"
                     "Content-Length: %d\r\nConnection: close\r\n\r\n",
                     code, status, (int)body.size());
    if (n > 0) send(c, hdr, n, 0);
    if (!body.empty()) send(c, body.data(), (int)body.size(), 0);
}

// 单线程顺序处理：收完请求头 → 按 Content-Length 收 body → 解析 → 统一应答。
// 刻意简化：同一时刻只服务一个连接，收发各挂 3s 超时防半开连接卡死
static void HandleClient(SOCKET c, DWORD peer, const std::wstring& allow) {
    DWORD tmo = 3000;
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (char*)&tmo, sizeof tmo);
    setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, (char*)&tmo, sizeof tmo);

    char ip[64] = "";
    inet_ntop(AF_INET, &peer, ip, sizeof ip);
    wchar_t wip[64] = L"";
    MultiByteToWideChar(CP_UTF8, 0, ip, -1, wip, 64);

    std::string req, body;
    if (IpAllowed(wip, allow)) {
        size_t hdrEnd = std::string::npos;
        char buf[4096];
        while (req.size() < 8192) {
            int r = recv(c, buf, sizeof buf, 0);
            if (r <= 0) break;
            req.append(buf, r);
            hdrEnd = req.find("\r\n\r\n");
            if (hdrEnd != std::string::npos) break;
        }
        if (hdrEnd != std::string::npos) {
            std::string low = req.substr(0, hdrEnd);
            for (char& ch : low) ch = (char)tolower((unsigned char)ch);
            if (low.find("expect: 100-continue") != std::string::npos)
                send(c, "HTTP/1.1 100 Continue\r\n\r\n", 25, 0);   // 大消息 curl 会先等这个
            int cl = 0;
            size_t p = low.find("content-length:");
            if (p != std::string::npos) cl = atoi(low.c_str() + p + 15);
            if (cl < 0 || cl > 65536) cl = 65536;
            size_t have = req.size() - hdrEnd - 4;
            while (have < (size_t)cl) {
                int r = recv(c, buf, sizeof buf, 0);
                if (r <= 0) break;
                req.append(buf, r);
                have += r;
            }
            body = req.substr(hdrEnd + 4, (size_t)cl);
        }
    }

    std::wstring msg;
    bool good = false;
    if (!body.empty()) {
        int wl = MultiByteToWideChar(CP_UTF8, 0, body.data(), (int)body.size(), nullptr, 0);
        if (wl > 0 && wl < 65536) {
            std::wstring w(wl, 0);
            MultiByteToWideChar(CP_UTF8, 0, body.data(), (int)body.size(), &w[0], wl);
            good = ParseMsgJson(w, msg);
        }
    }
    if (good) {
        SendHttp(c, 200, "OK", "OK");
        wchar_t* heap = new wchar_t[msg.size() + 1];
        wmemcpy(heap, msg.c_str(), msg.size() + 1);
        if (!PostMessageW(g_hwnd, WM_MSG, 0, (LPARAM)heap)) delete[] heap;   // 窗口已销毁等场景不丢不漏
    } else if (!IpAllowed(wip, allow)) {
        char tip[128];                                        // 与格式错误分开：被白名单拦下要能一眼看出
        snprintf(tip, sizeof tip, "来源 IP %s 不在允许的前缀内", ip);
        SendHttp(c, 403, "Forbidden", tip);
    } else if (body.empty()) {
        SendHttp(c, 400, "Bad Request",
                 WideToUtf8(L"未读到请求体。请按照 {\"msg\":\"XXXXX\"}这个格式发送"));
    } else {
        std::string r = "收到: " + body.substr(0, 120) +
                        " 。请按照 {\"msg\":\"XXXXX\"}这个格式发送";   // 回显便于发现客户端转义/引号问题
        SendHttp(c, 400, "Bad Request", r);
    }
    closesocket(c);
}

static DWORD WINAPI ListenThread(LPVOID p) {
    SOCKET srv = (SOCKET)(INT_PTR)p;                       // 本线程专属：Stop 超时后重新 Start 也不会摸到新 socket
    std::wstring allow = g_listenAllow;                    // 启动时快照
    while (g_listenOn) {
        sockaddr_in ca{};
        int cl = sizeof ca;
        SOCKET c = accept(srv, (sockaddr*)&ca, &cl);
        if (c == INVALID_SOCKET) break;                    // 停止 = closesocket 唤醒
        HandleClient(c, ca.sin_addr.S_un.S_addr, allow);
    }
    return 0;
}

static void SaveListenCfg() {
    WritePrivateProfileStringW(L"listen", L"port", std::to_wstring(g_listenPort).c_str(),
                               g_iniConf.c_str());
    WritePrivateProfileStringW(L"listen", L"allow", g_listenAllow.c_str(), g_iniConf.c_str());
    WritePrivateProfileStringW(L"listen", L"toast", std::to_wstring(g_toastSec).c_str(),
                               g_iniConf.c_str());
}

// 托盘气泡提示（随机端口、启动失败等关键反馈；副本方式不污染 g_nid）
static void TrayTip(const wchar_t* msg) {
    NOTIFYICONDATAW n = g_nid;
    n.uFlags = NIF_INFO;
    lstrcpynW(n.szInfo, msg, 256);
    lstrcpynW(n.szInfoTitle, L"消息监听", 64);
    n.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

static bool StartListener() {
    if (g_listenOn) return true;
    if (!g_wsaOk) {
        WSADATA d;
        if (WSAStartup(MAKEWORD(2, 2), &d) != 0) {
            g_listenStatus = L"网络初始化失败";
            TrayTip(L"监听启动失败：网络初始化失败");
            return false;
        }
        g_wsaOk = true;
    }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        g_listenStatus = L"创建套接字失败";
        TrayTip(L"监听启动失败：创建套接字失败");
        return false;
    }
    BOOL reuse = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof reuse);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)g_listenPort);
    a.sin_addr.s_addr = INADDR_ANY;
    if (bind(s, (sockaddr*)&a, sizeof a) != 0) {
        closesocket(s);
        g_listenStatus = g_listenPort ? L"启动失败：端口被占用" : L"启动失败";
        TrayTip(g_listenPort ? L"监听启动失败：端口被占用" : L"监听启动失败");
        return false;
    }
    listen(s, 8);
    sockaddr_in got{}; int gl = sizeof got;
    g_listenActual = (getsockname(s, (sockaddr*)&got, &gl) == 0) ? ntohs(got.sin_port) : 0;
    g_srvSock = s;
    g_listenOn = true;
    g_listenThread = CreateThread(nullptr, 0, ListenThread, (LPVOID)(INT_PTR)s, 0, nullptr);
    wchar_t b[64];
    wsprintfW(b, L"监听已启动，端口 %d", g_listenActual);
    g_listenStatus = b;
    TrayTip(b);                                            // 随机端口必须告知
    InvalidateRect(g_hwnd, nullptr, FALSE);                // 播放按钮点亮
    return true;
}

static void StopListener() {
    if (!g_listenOn) return;
    g_listenOn = false;
    closesocket(g_srvSock);                                // 唤醒阻塞在 accept 的线程
    g_srvSock = INVALID_SOCKET;
    if (g_listenThread) {
        WaitForSingleObject(g_listenThread, 500);          // 正在收发的场景有 3s 超时，届时自行退出
        CloseHandle(g_listenThread);
        g_listenThread = nullptr;
    }
    g_listenStatus = L"已停止";
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

// ---------------- 消息弹框（主显示器正中 · 最上层 · 不抢焦点） ----------------
static LRESULT CALLBACK ToastProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_LBUTTONDOWN:                                   // 主动点击：立即消失
        KillTimer(h, 1);
        FadeOut(h, 120);
        return 0;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        gp::Graphics g(dc); GdipSetup(g);
        // 饱和渐变底 + 白描边：纯白（浏览器）/纯黑桌面上都一眼可见
        gp::GraphicsPath p;
        GdipPathRound(p, gp::RectF(0, 0, (FLOAT)rc.right, (FLOAT)rc.bottom), (FLOAT)S(14));
        gp::LinearGradientBrush br(gp::PointF(0, 0),
                                   gp::PointF((FLOAT)rc.right, (FLOAT)rc.bottom),
                                   gp::Color(255, 70, 110, 225),
                                   gp::Color(255, 168, 68, 205));
        g.FillPath(&br, &p);
        GdipStrokeRound(g, rc, S(14), C_WHITE, 210, 1.5f);
        // 顶部小标签 + 底部操作提示
        DrawGlyph(dc, { S(12), S(8), S(30), S(26) }, 0xE8BD, RGB(225, 232, 255));
        DrawTextRect(dc, { S(32), S(8), rc.right - S(12), S(26) }, L"消息",
                     g_fSmall, RGB(225, 232, 255), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DrawTextRect(dc, { 0, rc.bottom - S(22), rc.right, rc.bottom - S(6) }, L"点击关闭",
                     g_fSmall, RGB(220, 228, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        std::wstring txt = g_msgText.empty() ? L"(空消息)" : g_msgText;
        auto lines = WrapText(dc, txt, rc.right - S(36), 3);
        int top = S(28);
        int tot = (int)lines.size() * TextLineH();
        int y = top + (rc.bottom - S(24) - top - tot) / 2;
        for (size_t i = 0; i < lines.size(); i++) {
            RECT lr{ S(18), y + (int)i * TextLineH(), rc.right - S(18), y + (int)(i + 1) * TextLineH() };
            DrawTextRect(dc, lr, lines[i].c_str(), g_fUiB, C_WHITE,
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_TIMER:
        if (w == 1) { KillTimer(h, 1); FadeOut(h, 150); }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void ShowToast(const std::wstring& text) {
    int w = S(240), h = S(120);
    if (!g_hToast) {
        WNDCLASSW tc = {};
        tc.lpfnWndProc = ToastProc;
        tc.hInstance = g_hInst;
        tc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        tc.hbrBackground = nullptr;                        // 同其他弹窗：防系统底色闪一帧
        tc.lpszClassName = TOAST_CLASS;
        RegisterClassW(&tc);
        g_hToast = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, TOAST_CLASS, L"",
                                   WS_POPUP, 0, 0, w, h, nullptr, nullptr, g_hInst, nullptr);
        if (!g_hToast) return;
        EnableRoundedCorner(g_hToast, C_EDGE);
        ApplyRoundRegion(g_hToast, true);
    }
    g_msgText = text;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;       // 主显示器正中
    int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    SetWindowPos(g_hToast, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    FadeIn(g_hToast, 120, SW_SHOWNOACTIVATE);
    InvalidateRect(g_hToast, nullptr, FALSE);
    if (g_toastSec > 0) SetTimer(g_hToast, 1, g_toastSec * 1000, nullptr);   // 新消息重置计时
    else KillTimer(g_hToast, 1);                           // 0 = 不自动消失，点击才关
}

// ---------------- 监听设置对话框 ----------------
static LRESULT CALLBACK LstnProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        g_dlgBtns.clear(); g_dlgEdits.clear();
        HINSTANCE inst = ((CREATESTRUCTW*)l)->hInstance;
        int x = S(16), lw = S(84), fw = S(380) - x * 2 - lw;
        auto label = [&](const wchar_t* t, int px, int py, int pw) {
            CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE,
                            px, py, pw, S(18), h, nullptr, inst, nullptr);
        };
        auto edit = [&](int px, int py, int pw, const wchar_t* txt, int id) -> HWND {
            g_dlgEdits.push_back({ px, py, px + pw, py + S(28) });
            HWND c = CreateWindowExW(0, L"EDIT", txt, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                     px + S(2), py + S(2), pw - S(4), S(28) - S(4),
                                     h, (HMENU)(INT_PTR)id, inst, nullptr);
            SendMessageW(c, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(S(8), S(8)));
            return c;
        };
        wchar_t pb[16] = L"";
        if (g_listenPort) wsprintfW(pb, L"%d", g_listenPort);
        label(L"端口", x, S(46) + S(5), lw);
        g_hPort = edit(x + lw, S(46), S(140), pb, IDC_PORT);
        label(L"允许IP前缀", x, S(88) + S(5), lw);
        g_hAllow = edit(x + lw, S(88), fw, g_listenAllow.c_str(), IDC_ALLOW);
        wchar_t tb[16] = L"";
        wsprintfW(tb, L"%d", g_toastSec);
        label(L"弹窗停留(秒)", x, S(130) + S(5), S(96));
        g_hTSec = edit(x + S(96), S(130), S(140), tb, IDC_TSEC);
        g_dlgBtns.push_back({ { S(380) - x - S(88) * 2 - S(8), S(204), S(380) - x - S(88) - S(8), S(204) + S(28) },
                              IDOK, L"确定", true });
        g_dlgBtns.push_back({ { S(380) - x - S(88), S(204), S(380) - x, S(204) + S(28) },
                              IDCANCEL, L"取消", false });
        g_rcDlgX = { S(380) - S(40), 0, S(380) - S(8), S(36) };
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        DrawDlgFace(h, dc);
        DrawTextRect(dc, { S(16), S(172), S(380) - S(16), S(194) },
                     g_listenStatus.c_str(), g_fSmall, C_MUT,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_NCACTIVATE:
        return DefWindowProcW(h, m, w, -1);
    case WM_ERASEBKGND:
        return 1;
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
        if (const DlgBtn* b = FindDlgBtn(pt)) { SendMessageW(h, WM_COMMAND, b->id, 0); return 0; }
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
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) { DestroyWindow(h); return 0; }
        break;
    case WM_COMMAND:
        if (LOWORD(w) == IDOK) {
            wchar_t b[512];
            GetWindowTextW(g_hPort, b, 64);
            int p = _wtoi(b);
            if (p < 0 || p > 65535) {
                g_listenStatus = L"端口无效：留空 = 随机，或填 1-65535";
                InvalidateRect(h, nullptr, TRUE);
                return 0;
            }
            GetWindowTextW(g_hAllow, b, 512);
            g_listenAllow = b;
            g_listenPort = p;
            GetWindowTextW(g_hTSec, b, 16);
            int t = _wtoi(b);
            if (t < 0) t = 0;
            if (t > 86400) t = 86400;                      // 上限一天
            g_toastSec = t;
            SaveListenCfg();
            if (g_listenOn) {                              // 改配置立即生效：按新配置重启
                StopListener();
                StartListener();
            }
            DestroyWindow(h);
            return 0;
        }
        if (LOWORD(w) == IDCANCEL) { DestroyWindow(h); return 0; }
        break;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_NCDESTROY:
        g_hLstn = nullptr; g_hPort = g_hAllow = g_hTSec = nullptr;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void OpenListenDlg() {
    if (g_hLstn) { SetForegroundWindow(g_hLstn); return; }
    if (g_hDlg) DestroyWindow(g_hDlg);                     // 按钮数组全局共享，禁止对话框并发
    if (g_hHK) DestroyWindow(g_hHK);
    if (g_listenOn) {
        wchar_t b[96];
        wsprintfW(b, L"监听中 · 端口 %d · 修改后点确定将重启监听", g_listenActual);
        g_listenStatus = b;
    } else {
        g_listenStatus = L"未监听 · 端口留空 = 随机 · 秒数 0 = 不自动消失";
    }
    int cw = S(380), ch = S(250);
    RECT mr{};
    if (g_hwnd) GetWindowRect(g_hwnd, &mr);
    g_hLstn = CreateWindowExW(0, LSTN_CLASS, L"监听设置", WS_POPUP,
                              mr.left + ((mr.right - mr.left) - cw) / 2,
                              mr.top + ((mr.bottom - mr.top) - ch) / 2,
                              cw, ch, g_hwnd, nullptr, g_hInst, nullptr);
    if (g_hLstn) {
        EnableRoundedCorner(g_hLstn, C_EDGE);
        ApplyRoundRegion(g_hLstn, true);
        FadeIn(g_hLstn, 120);
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
    case WM_NCLBUTTONDBLCLK:
        if (w == HTCAPTION) return 0;     // 双击标题栏不最大化
        break;
    case WM_NCCALCSIZE: {
        if (!w) break;
        if (IsZoomed(h)) {                // 最大化：窗口扩出屏幕一圈边框，只内缩边框量；
            RECT br{ 0, 0, 0, 0 };        // 不含 CAPTION，否则顶部留出一条系统标题栏空位
            AdjustWindowRect(&br, WS_THICKFRAME, FALSE);
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
            !(hh.reg >= R_BTN_CLOSE && hh.reg <= R_BTN_PLAY))
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
    case WM_SIZE: LayoutNow(); ApplyRoundRegion(h, false); InvalidateRect(h, nullptr, FALSE); break;
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
        if (g_pressIdx >= 0) {                            // 按住左键：先判定拖拽，松手未拖则按单击处理
            bool changed = false;
            if (!g_dragging &&
                (abs(pt.x - g_pressPt.x) > S(6) || abs(pt.y - g_pressPt.y) > S(6))) {
                g_dragging = (g_sortMode == 0);           // 名称排序下拖不出可见效果，禁用拖拽
                changed = g_dragging;
            }
            if (g_dragging && hh.reg == R_CELL && hh.idx != g_pressIdx) {
                DragMove(g_pressIdx, hh.idx);             // 实时换位
                g_pressIdx = hh.idx;
                changed = true;
            }
            if (g_dragging && (g_hoverReg != R_CELL || g_hoverIdx != g_pressIdx)) {
                g_hoverReg = R_CELL; g_hoverIdx = g_pressIdx;
                changed = true;
            }
            if (changed) InvalidateRect(h, nullptr, FALSE);   // 只在画面真变化时重绘，拖拽不空转 GPU/CPU
            break;
        }
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
            bool hand = hh.reg == R_CELL || hh.reg == R_TAG ||
                        (hh.reg >= R_BTN_CLOSE && hh.reg <= R_BTN_PLAY);
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
        if (!g_menuDone) { MenuCloseAll(); return 0; }   // 菜单展开时，首击只用于收起
        POINT pt{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        Hit hh = HitTest(pt);
        switch (hh.reg) {
        case R_CELL:                                      // 按住待判定：拖拽换位 或 松手单击启动
            g_pressIdx = hh.idx; g_pressPt = pt;
            SetCapture(h);
            break;
        case R_BTN_CLOSE: HideWin(); break;
        case R_BTN_MENU: ShowMainMenu(); break;
        case R_BTN_PIN:
            g_pinned = !g_pinned; ApplyPin(); SaveWinPos();
            InvalidateRect(h, nullptr, FALSE);
            break;
        case R_BTN_PLAY:
            if (g_listenOn) StopListener(); else StartListener();  // 纯开关；设置在主菜单
            break;
        case R_TAG: {
            auto tl = TagList();
            if (hh.idx == 0) SelectTag(0);
            else if (hh.idx - 1 < (int)tl.size()) SelectTag(tl[hh.idx - 1]->id);
            break;
        }
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (g_dragging) {                                 // 拖拽收尾：落盘新顺序
            ReleaseCapture();
            g_dragging = false;
            SaveOrders();
            InvalidateRect(h, nullptr, FALSE);
        } else if (g_pressIdx >= 0 && g_pressIdx < (int)g_view.size()) {
            LaunchLink(const_cast<Node*>(g_view[g_pressIdx]), false);   // 单击行为不变
        }
        g_pressIdx = -1;
        return 0;
    }
    case WM_CAPTURECHANGED:                               // 捕获被打断（弹窗/失活）→ 丢弃本次按压
        g_dragging = false; g_pressIdx = -1;
        break;
    case WM_CONTEXTMENU: {
        if (!g_menuDone) { MenuCloseAll(); return 0; }   // 菜单展开时，首击只用于收起
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
            nd.order = (int)g_nodes.size();
            g_nodes.push_back(nd); SaveNode(nd);
        }
        if (n) SaveNextId();
        DragFinish(d);
        if (n) Refresh();
        return 0;
    }
    case WM_TIMER:
        if (w == 1 && g_hideOnFocus && g_menuDone && IsWindowVisible(h) && !g_pinned && !g_hDlg && !g_hHK) {
            HWND fw = GetForegroundWindow();
            if (fw != h && (!fw || GetWindow(fw, GW_OWNER) != h)) HideWin();
        }
        break;
    case WM_HOTKEY:
        if (w == 1) { if (!g_menuDone) MenuCloseAll(); ToggleWin(); }
        break;
    case WM_TRAY: {
        if (LOWORD(l) == WM_LBUTTONUP) ToggleWin();
        else if (LOWORD(l) == WM_RBUTTONUP) {
            POINT pt; GetCursorPos(&pt);
            std::vector<MItem> mu;
            MAdd(mu, TR_SHOW, IsWindowVisible(h) ? L"隐藏" : L"显示");
            MSep(mu);
            MAdd(mu, TR_EXIT, L"退出");
            int cmd = MPopup(std::move(mu), pt, GetDesktopWindow());
            if (cmd == TR_SHOW) ToggleWin();
            if (cmd == TR_EXIT) Quit();
        }
        return 0;
    }
    case WM_MSG: {
        wchar_t* p = (wchar_t*)l;
        if (p) { ShowToast(p); delete[] p; }
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
    CreateDirectoryW((dir + L"data").c_str(), nullptr);   // 目录缺失时 links.ini 所有写入会静默失败
    g_iniLinks = dir + L"data\\links.ini";
    EnsureUnicodeIni(g_iniConf);
    EnsureUnicodeIni(g_iniLinks);

    if (already) {
        HWND h = FindWindowW(MAIN_CLASS, nullptr);
        if (h) { ShowWindow(h, SW_SHOW); SetForegroundWindow(h); }
        CloseHandle(mutex);
        gp::GdiplusShutdown(g_gdipToken);
        return 0;
    }

    g_hideOnFocus      = GetPrivateProfileIntW(L"behavior", L"hideOnFocus", 1, g_iniConf.c_str()) != 0;
    g_hideAfterLaunch  = GetPrivateProfileIntW(L"behavior", L"hideAfterLaunch", 1, g_iniConf.c_str()) != 0;
    g_autoStart        = GetPrivateProfileIntW(L"behavior", L"autostart", 1, g_iniConf.c_str()) != 0;
    g_pinned           = GetPrivateProfileIntW(L"ui", L"pinned", 0, g_iniConf.c_str()) != 0;
    g_iconIdx          = std::min(2, std::max(0, (int)GetPrivateProfileIntW(L"ui", L"iconSize", 1, g_iniConf.c_str())));
    g_sortMode         = std::min(2, std::max(0, (int)GetPrivateProfileIntW(L"ui", L"sort", 0, g_iniConf.c_str())));
    ApplyTheme((int)GetPrivateProfileIntW(L"ui", L"theme", 0, g_iniConf.c_str()));   // 深色为默认
    g_startHidden      = GetPrivateProfileIntW(L"behavior", L"startHidden", 0, g_iniConf.c_str()) != 0;
    g_listenPort       = GetPrivateProfileIntW(L"listen", L"port", 22222, g_iniConf.c_str());
    if (g_listenPort < 0 || g_listenPort > 65535) g_listenPort = 22222;
    g_toastSec         = GetPrivateProfileIntW(L"listen", L"toast", 5, g_iniConf.c_str());
    if (g_toastSec < 0 || g_toastSec > 86400) g_toastSec = 5;
    {
        wchar_t ab[512];
        GetPrivateProfileStringW(L"listen", L"allow", L"", ab, 512, g_iniConf.c_str());
        g_listenAllow = ab;
    }
    int wx = GetPrivateProfileIntW(L"ui", L"x", -32000, g_iniConf.c_str());
    int wy = GetPrivateProfileIntW(L"ui", L"y", -32000, g_iniConf.c_str());
    int ww = std::max(S(430), (int)GetPrivateProfileIntW(L"ui", L"w", S(660), g_iniConf.c_str()));
    int wh = std::max(S(340), (int)GetPrivateProfileIntW(L"ui", L"h", S(460), g_iniConf.c_str()));

    LoadNodes();
    g_curTag = GetPrivateProfileIntW(L"ui", L"curTag", 0, g_iniConf.c_str());   // 记住上次选中标签
    if (g_curTag) {
        bool ok = false;
        for (auto& n : g_nodes) if (n.type == NT_TAG && n.id == g_curTag) ok = true;
        if (!ok) g_curTag = 0;                                  // 标签已被删除则回落"全部"
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
    dc.hbrBackground = nullptr;
    dc.lpszClassName = DLG_CLASS;
    RegisterClassW(&dc);

    WNDCLASSW hk = {};
    hk.lpfnWndProc = HKProc;
    hk.hInstance = hInst;
    hk.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    hk.hbrBackground = nullptr;
    hk.lpszClassName = HK_CLASS;
    RegisterClassW(&hk);

    WNDCLASSW ls = {};                                    // 监听设置窗口类
    ls.lpfnWndProc = LstnProc;
    ls.hInstance = hInst;
    ls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    ls.hbrBackground = nullptr;
    ls.lpszClassName = LSTN_CLASS;
    RegisterClassW(&ls);

    WNDCLASSW mc = {};                                    // 自绘菜单窗口类
    mc.lpfnWndProc = MenuProc;
    mc.hInstance = hInst;
    mc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    mc.hbrBackground = nullptr;
    mc.lpszClassName = MENU_CLASS;
    RegisterClassW(&mc);

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
    g_dwmRound = EnableRoundedCorner(g_hwnd, C_EDGE);       // Win11 走 DWM 抗锯齿圆角
    ApplyRoundRegion(g_hwnd, false);                        // Win10 无该属性时用区域兜底
    Refresh();
    if (g_pinned) ApplyPin();
    // 强制 DWM 立即按我们的 NCCALCSIZE 重排非客户区（否则首帧残留系统标题栏，resize 后才消失）
    SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    if (!g_startHidden) ShowWin();
    if (g_autoStart) ApplyAutoStart(true);   // 默认开：每次启动刷新 Run 路径，exe 挪位后自动跟上

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
