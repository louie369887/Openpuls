// OpenPuls 响应优化器 - 主界面（托盘常驻 + 设置面板）
#include "ui.h"

#include "diag.h"
#include "monitor.h"
#include "tweaks.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <thread>
#include <utility>
#include <vector>

namespace op {
namespace {

// ---------------------------------------------------------------- 常量
const wchar_t* kClassMain = L"OP.MainWindow";
const wchar_t* kClassRow = L"OP.Row";
const wchar_t* kClassBtn = L"OP.Btn";
const wchar_t* kClassLat = L"OP.Latency";

constexpr int kSideW = 214;
constexpr int kHeaderH = 88;
constexpr int kPadX = 30;
constexpr int kRowH = 58;
constexpr int kRowGap = 8;

constexpr UINT WM_OP_TRAY = WM_APP + 20;
constexpr UINT WM_OP_DIAG_PROGRESS = WM_APP + 21;
constexpr UINT WM_OP_DIAG_DONE = WM_APP + 22;
constexpr UINT WM_OP_QUIT = WM_APP + 23;
constexpr UINT WM_OP_SCAN_DONE = WM_APP + 24;

enum {
    ID_TOGGLE_BASE = 2000,
    ID_SET_AUTOSTART = 3001,
    ID_SET_STARTMIN,
    ID_SET_CLOSETRAY,
    ID_SET_GUARD,
    ID_SET_ALERTS,
    ID_BTN_DIAG = 4001,
    ID_BTN_OPTIMIZE,
    ID_BTN_RESTORE,
    ID_BTN_SCAN,
    ID_BTN_BENCH_ALL,
    ID_BTN_RESTART,
    ID_BTN_LATENCY,
    ID_BTN_EXPORT,
    ID_BTN_REBUILD,
    ID_BTN_ELEVATE,
    ID_BTN_OPENLOG,
};

struct PageDef {
    const wchar_t* title;
    const wchar_t* sub;
};
const PageDef kPages[] = {
    {L"总览", L"右键、打开与触控的关键指标"},
    {L"右键菜单", L"菜单延迟与第三方扩展"},
    {L"打开与启动", L"窗口打开、程序启动"},
    {L"触控与输入", L"长按、惯性滚动与延迟"},
    {L"诊断", L"实测耗时与趋势"},
    {L"设置", L"常驻、开机启动与还原"},
};
constexpr int kPageCount = 6;

// ---------------------------------------------------------------- 调色板
struct Pal {
    COLORREF bg, side, sideHot, sideSel, sideText, sideDim, card, cardHot, border, text, dim, accent,
        accentText, soft, track, good, warn, bad, white;
};

Pal MakePal(bool dark) {
    Pal p{};
    if (!dark) {
        p.bg = RGB(0xF5, 0xF6, 0xF9);
        p.card = RGB(0xFF, 0xFF, 0xFF);
        p.cardHot = RGB(0xFB, 0xFC, 0xFE);
        p.border = RGB(0xE2, 0xE6, 0xEC);
        p.text = RGB(0x1A, 0x1F, 0x28);
        p.dim = RGB(0x6A, 0x72, 0x80);
        p.accent = RGB(0x0A, 0x74, 0xE8);
        p.accentText = RGB(0xFF, 0xFF, 0xFF);
        p.soft = RGB(0xE8, 0xF1, 0xFE);
        p.track = RGB(0xC9, 0xD0, 0xDA);
        p.good = RGB(0x0E, 0x8A, 0x4F);
        p.warn = RGB(0xB5, 0x6B, 0x00);
        p.bad = RGB(0xC3, 0x2F, 0x2F);
        p.white = RGB(0xFF, 0xFF, 0xFF);
    } else {
        p.bg = RGB(0x18, 0x1C, 0x24);
        p.card = RGB(0x22, 0x28, 0x32);
        p.cardHot = RGB(0x28, 0x2F, 0x3B);
        p.border = RGB(0x31, 0x39, 0x46);
        p.text = RGB(0xE9, 0xEC, 0xF2);
        p.dim = RGB(0x9B, 0xA4, 0xB3);
        p.accent = RGB(0x4C, 0x9A, 0xFF);
        p.accentText = RGB(0x0A, 0x12, 0x1E);
        p.soft = RGB(0x1E, 0x2C, 0x42);
        p.track = RGB(0x41, 0x4A, 0x59);
        p.good = RGB(0x4C, 0xC3, 0x8A);
        p.warn = RGB(0xE0, 0xA0, 0x3C);
        p.bad = RGB(0xF0, 0x6A, 0x6A);
        p.white = RGB(0xFF, 0xFF, 0xFF);
    }
    p.side = dark ? RGB(0x12, 0x15, 0x1C) : RGB(0x1E, 0x23, 0x2D);
    p.sideHot = dark ? RGB(0x1C, 0x21, 0x2B) : RGB(0x28, 0x2E, 0x3A);
    p.sideSel = dark ? RGB(0x22, 0x2B, 0x39) : RGB(0x2C, 0x34, 0x42);
    p.sideText = RGB(0xF2, 0xF5, 0xF9);
    p.sideDim = RGB(0x93, 0x9E, 0xB0);
    return p;
}

Pal P;  // 全局调色板

struct RowInfo {
    HWND hwnd = nullptr;
    int id = 0;
    int page = 0;
    int kind = 0;  // 0 开关 1 动作
    bool on = false;
    bool disabled = false;
    bool hover = false;
    std::wstring title, desc, actionText, tweakId, badge;
};

struct BtnInfo {
    HWND hwnd = nullptr;
    int id = 0;
    int page = 0;
    bool primary = false;
    bool disabled = false;
    bool hover = false;
    std::wstring text;
};

struct App {
    HINSTANCE inst = nullptr;
    HWND hwnd = nullptr, hList = nullptr, hLat = nullptr;
    TweakEngine eng;
    std::vector<ShellExt> exts;
    diag::Report rep;
    bool repValid = false;
    bool diagRunning = false;
    std::wstring diagText;
    int diagPct = 0;
    int page = 0;
    int dpi = 96;
    bool dark = false;
    bool admin = false;
    Pal pal;
    HFONT fBase = nullptr, fBold = nullptr, fSmall = nullptr, fTitle = nullptr, fBig = nullptr;
    HICON iconBig = nullptr, iconSmall = nullptr;
    std::vector<RowInfo*> rows;
    std::vector<BtnInfo*> btns;
    int navHot = -1;
    bool trayAdded = false;
    bool closeToTray = true;
    bool startMin = false;
    bool alerts = true;
    bool listUpdating = false;
    std::wstring statusText;
    std::wstring scanText;
    int listY = 0, listH = 0, btnY = 0;
    std::vector<std::wstring> listClsids;
    double lastTrayUpdate = 0;
    int layerTop = 0;
};

App* g = nullptr;
int g_dpiOverride = 0;  // OPENPULS_UI_SCALE 环境变量覆盖（用于不常见缩放比例）

inline int S(int v) { return MulDiv(v, g->dpi, 96); }

// ---------------------------------------------------------------- 绘图工具
void FillRound(HDC dc, RECT r, int radius, COLORREF fill, COLORREF border, int bw = 1) {
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, bw, border);
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius * 2, radius * 2);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

void TextOut2(HDC dc, const std::wstring& s, RECT r, HFONT f, COLORREF c, UINT flags) {
    SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s.c_str(), (int)s.size(), &r, flags);
}

HFONT MakeFont(int pt10, bool bold, int dpi) {
    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(pt10, dpi, 720);
    lf.lfWeight = bold ? FW_SEMIBOLD : FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&lf);
}

void RebuildFonts() {
    for (HFONT* f : {&g->fBase, &g->fBold, &g->fSmall, &g->fTitle, &g->fBig}) {
        if (*f) DeleteObject(*f);
        *f = nullptr;
    }
    g->fBase = MakeFont(90, false, g->dpi);
    g->fBold = MakeFont(95, true, g->dpi);
    g->fSmall = MakeFont(82, false, g->dpi);
    g->fTitle = MakeFont(175, true, g->dpi);
    g->fBig = MakeFont(200, true, g->dpi);
}

// ---------------------------------------------------------------- 自绘控件
void PaintSwitch(HDC dc, RECT r, bool on, bool disabled) {
    int h = r.bottom - r.top;
    int w = h * 2;
    RECT t{r.right - w, r.top, r.right, r.bottom};
    COLORREF track = disabled ? P.track : (on ? P.accent : P.track);
    FillRound(dc, t, h / 2, track, track, 1);
    int kr = h / 2 - 3;
    int cx = on ? t.right - h / 2 : t.left + h / 2;
    int cy = t.top + h / 2;
    HBRUSH b = CreateSolidBrush(P.white);
    HGDIOBJ ob = SelectObject(dc, b);
    HPEN pen = CreatePen(PS_SOLID, 1, P.white);
    HGDIOBJ op = SelectObject(dc, pen);
    Ellipse(dc, cx - kr, cy - kr, cx + kr, cy + kr);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(pen);
}

void PaintRowWindow(HWND h, HDC dc) {
    auto* ri = (RowInfo*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!ri) return;
    RECT rc;
    GetClientRect(h, &rc);
    int dpi = g->dpi;
    auto sx = [dpi](int v) { return MulDiv(v, dpi, 96); };

    // 先铺一层父窗口底色，避免圆角外露出未绘制像素
    HBRUSH back = CreateSolidBrush(P.bg);
    FillRect(dc, &rc, back);
    DeleteObject(back);

    COLORREF card = ri->disabled ? P.bg : (ri->hover ? P.cardHot : P.card);
    FillRound(dc, rc, sx(10), card, ri->hover ? P.border : (ri->disabled ? P.border : card), 1);
    if (ri->hover && !ri->disabled) {
        RECT bar{0, 0, sx(3), rc.bottom};
        HBRUSH b = CreateSolidBrush(P.accent);
        FillRect(dc, &bar, b);
        DeleteObject(b);
    }
    int pad = sx(16);
    int right = rc.right - sx(18);
    RECT tr{pad, sx(9), right - sx(60), sx(34)};
    TextOut2(dc, ri->title, tr, g->fBold, ri->disabled ? P.dim : P.text,
             DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT dr{pad, sx(30), right - sx(60), rc.bottom - sx(6)};
    TextOut2(dc, ri->desc, dr, g->fSmall, P.dim, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (ri->kind == 0) {
        if (ri->disabled && !ri->badge.empty()) {
            RECT br{right - sx(86), rc.top + ((rc.bottom - rc.top) - sx(22)) / 2, right, 0};
            br.bottom = br.top + sx(22);
            FillRound(dc, br, sx(7), card, P.border, 1);
            TextOut2(dc, ri->badge, br, g->fSmall, P.dim, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        } else {
            RECT sr{right - sx(48), rc.top + ((rc.bottom - rc.top) - sx(24)) / 2, right, 0};
            sr.bottom = sr.top + sx(24);
            PaintSwitch(dc, sr, ri->on, ri->disabled);
        }
    } else {
        RECT br{right - sx(96), rc.top + ((rc.bottom - rc.top) - sx(30)) / 2, right, 0};
        br.bottom = br.top + sx(30);
        FillRound(dc, br, sx(8), ri->disabled ? P.bg : P.soft, ri->disabled ? P.border : P.soft, 1);
        TextOut2(dc, ri->actionText, br, g->fBold, ri->disabled ? P.dim : P.accent,
                 DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }
}

LRESULT CALLBACK RowProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* ri = (RowInfo*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            PaintRowWindow(h, dc);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_MOUSEMOVE:
            if (ri && !ri->hover) {
                ri->hover = true;
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, h, 0};
                TrackMouseEvent(&tme);
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSELEAVE:
            if (ri) {
                ri->hover = false;
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_SETCURSOR:
            if (ri && !ri->disabled) SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        case WM_LBUTTONUP:
            if (ri && !ri->disabled)
                SendMessageW(GetParent(h), WM_COMMAND, MAKEWPARAM(ri->id, BN_CLICKED), (LPARAM)h);
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void PaintBtnWindow(HWND h, HDC dc) {
    auto* bi = (BtnInfo*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!bi) return;
    RECT rc;
    GetClientRect(h, &rc);
    HBRUSH back = CreateSolidBrush(P.bg);
    FillRect(dc, &rc, back);
    DeleteObject(back);
    int dpi = g->dpi;
    int rad = MulDiv(8, dpi, 96);
    COLORREF fill, border, textc;
    if (bi->disabled) {
        fill = P.bg;
        border = P.border;
        textc = P.dim;
    } else if (bi->primary) {
        fill = bi->hover ? P.accent : P.accent;
        border = P.accent;
        textc = P.accentText;
        if (bi->hover) {
            // 轻微提亮
            fill = RGB(std::min(255, GetRValue(P.accent) + 18), std::min(255, GetGValue(P.accent) + 18),
                       std::min(255, GetBValue(P.accent) + 18));
        }
    } else {
        fill = bi->hover ? P.cardHot : P.card;
        border = bi->hover ? P.accent : P.border;
        textc = P.text;
    }
    FillRound(dc, rc, rad, fill, border, 1);
    TextOut2(dc, bi->text, rc, g->fBase, textc, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}

LRESULT CALLBACK BtnProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* bi = (BtnInfo*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            PaintBtnWindow(h, dc);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_MOUSEMOVE:
            if (bi && !bi->hover) {
                bi->hover = true;
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, h, 0};
                TrackMouseEvent(&tme);
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSELEAVE:
            if (bi) {
                bi->hover = false;
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_SETCURSOR:
            if (bi && !bi->disabled) SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        case WM_LBUTTONUP:
            if (bi && !bi->disabled)
                SendMessageW(GetParent(h), WM_COMMAND, MAKEWPARAM(bi->id, BN_CLICKED), (LPARAM)h);
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------------------------------------------------------------- 行/按钮构造
RowInfo* CreateRow(int page, const std::wstring& title, const std::wstring& desc, int id, int kind,
                   const std::wstring& actionText, const std::wstring& tweakId) {
    auto* ri = new RowInfo();
    ri->page = page;
    ri->id = id;
    ri->kind = kind;
    ri->title = title;
    ri->desc = desc;
    ri->actionText = actionText;
    ri->tweakId = tweakId;
    ri->hwnd = CreateWindowExW(0, kClassRow, L"", WS_CHILD, 0, 0, 10, 10, g->hwnd, nullptr, g->inst,
                               nullptr);
    SetWindowLongPtrW(ri->hwnd, GWLP_USERDATA, (LONG_PTR)ri);
    g->rows.push_back(ri);
    return ri;}

BtnInfo* CreateBtn(int page, const std::wstring& text, int id, bool primary) {
    auto* bi = new BtnInfo();
    bi->page = page;
    bi->id = id;
    bi->text = text;
    bi->primary = primary;
    bi->hwnd = CreateWindowExW(0, kClassBtn, L"", WS_CHILD, 0, 0, 10, 10, g->hwnd, nullptr, g->inst,
                               nullptr);
    SetWindowLongPtrW(bi->hwnd, GWLP_USERDATA, (LONG_PTR)bi);
    g->btns.push_back(bi);
    return bi;
}

void SetRowOn(const std::wstring& tweakId, bool on) {
    for (auto* r : g->rows)
        if (r->tweakId == tweakId) {
            r->on = on;
            InvalidateRect(r->hwnd, nullptr, FALSE);
        }
}

void RefreshStates() {
    for (auto* r : g->rows) {
        if (r->tweakId.empty()) continue;
        if (r->tweakId == L"@autostart") {
            std::wstring v;
            r->on = RegGetStr(HKEY_CURRENT_USER,
                              L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"OpenPulsShellOpt", &v);
        } else if (r->tweakId == L"@startmin") {
            r->on = g->startMin;
        } else if (r->tweakId == L"@closetray") {
            r->on = g->closeToTray;
        } else if (r->tweakId == L"@guard") {
            r->on = Monitor::Inst().GuardEnabled();
        } else if (r->tweakId == L"@alerts") {
            r->on = g->alerts;
        } else {
            const TweakDef* t = g->eng.Find(r->tweakId);
            if (t) {
                r->on = g->eng.Probe(*t) == TState::Applied;
                r->disabled = (t->admin && !g->admin) || !t->supported;
                r->badge = (t->admin && !g->admin) ? L"需管理员" : (!t->supported ? L"无触摸屏" : L"");
            }
        }
        InvalidateRect(r->hwnd, nullptr, FALSE);
    }
    InvalidateRect(g->hwnd, nullptr, FALSE);
}

// ---------------------------------------------------------------- 主题
bool SystemUsesDark() {
    DWORD v = 1;
    RegGetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                L"AppsUseLightTheme", &v);
    return v == 0;
}

void ApplyTheme() {
    g->dark = SystemUsesDark();
    P = MakePal(g->dark);
    BOOL dark = g->dark;
    DwmSetWindowAttribute(g->hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    // 标题栏配色跟随
    COLORREF cap = g->dark ? RGB(0x14, 0x18, 0x1F) : RGB(0xF5, 0xF6, 0xF9);
    COLORREF txt = g->dark ? RGB(0xE9, 0xEC, 0xF2) : RGB(0x1A, 0x1F, 0x28);
    DwmSetWindowAttribute(g->hwnd, 35 /*DWMWA_CAPTION_COLOR*/, &cap, sizeof(cap));
    DwmSetWindowAttribute(g->hwnd, 36 /*DWMWA_TEXT_COLOR*/, &txt, sizeof(txt));
    if (g->hList) {
        ListView_SetBkColor(g->hList, P.card);
        ListView_SetTextBkColor(g->hList, P.card);
        ListView_SetTextColor(g->hList, P.text);
        InvalidateRect(g->hList, nullptr, TRUE);
    }
    for (auto* r : g->rows) InvalidateRect(r->hwnd, nullptr, FALSE);
    for (auto* b : g->btns) InvalidateRect(b->hwnd, nullptr, FALSE);
    InvalidateRect(g->hwnd, nullptr, TRUE);
}

// ---------------------------------------------------------------- 布局
int ContentTop() { return S(kHeaderH) + S(6); }

void LayoutPage() {
    RECT rc;
    GetClientRect(g->hwnd, &rc);
    int x = S(kSideW) + S(kPadX);
    int w = rc.right - x - S(kPadX);
    int bottom = rc.bottom - S(14);

    // 隐藏全部
    for (auto* r : g->rows) ShowWindow(r->hwnd, SW_HIDE);
    for (auto* b : g->btns) ShowWindow(b->hwnd, SW_HIDE);
    if (g->hList) ShowWindow(g->hList, SW_HIDE);

    int y = ContentTop();
    int extra = 0;
    if (g->page == 1) extra = S(26);
    if (g->page == 2) extra = S(2);
    if (g->page == 3) extra = S(2);
    if (g->page == 5) extra = S(2);
    y += extra;

    for (auto* r : g->rows) {
        if (r->page != g->page) continue;
        SetWindowPos(r->hwnd, HWND_TOP, x, y, w, S(kRowH), SWP_NOACTIVATE);
        ShowWindow(r->hwnd, SW_SHOW);
        y += S(kRowH) + S(kRowGap);
    }

    if (g->page == 1 && g->hList) {
        g->listY = y + S(24);
        g->btnY = bottom - S(38);
        g->listH = std::max(S(120), g->btnY - S(14) - g->listY);
        SetWindowPos(g->hList, HWND_TOP, x, g->listY, w, g->listH, SWP_NOACTIVATE);
        ShowWindow(g->hList, SW_SHOW);
        int bw = S(126);
        int bx = x;
        for (auto* b : g->btns) {
            if (b->page != 1) continue;
            SetWindowPos(b->hwnd, HWND_TOP, bx, g->btnY, bw, S(36), SWP_NOACTIVATE);
            bx += bw + S(10);
            ShowWindow(b->hwnd, SW_SHOW);
        }
    } else {
        int by = 0;
        if (g->page == 0) {
            by = ContentTop() + 2 * (S(116) + S(12)) + S(16);
        } else if (g->page == 3) {
            by = y + S(8) + S(116) + S(14);
        } else if (g->page == 4) {
            by = bottom - S(38);
        } else if (g->page == 5) {
            by = bottom - S(38);
        }
        int bx = x;
        for (auto* b : g->btns) {
            if (b->page != g->page) continue;
            int bw = S(138);
            SetWindowPos(b->hwnd, HWND_TOP, bx, by, bw, S(36), SWP_NOACTIVATE);
            bx += bw + S(10);
            ShowWindow(b->hwnd, SW_SHOW);
        }
    }
    // 顶栏「以管理员身份重启」按钮（全部页面可见）
    for (auto* b : g->btns) {
        if (b->page != -1) continue;
        SetWindowPos(b->hwnd, HWND_TOP, rc.right - S(kPadX) - S(150), S(28), S(150), S(34),
                     SWP_NOACTIVATE);
        ShowWindow(b->hwnd, g->admin ? SW_HIDE : SW_SHOW);
    }
    InvalidateRect(g->hwnd, nullptr, FALSE);
}

// ---------------------------------------------------------------- 侧栏/顶栏绘制
void PaintSidebar(HDC dc, int H) {
    RECT side{0, 0, S(kSideW), H};
    HBRUSH b = CreateSolidBrush(P.side);
    FillRect(dc, &side, b);
    DeleteObject(b);

    // Logo
    RECT logo{S(22), S(26), S(22) + S(30), S(26) + S(30)};
    FillRound(dc, logo, S(8), P.accent, P.accent, 1);
    RECT lt{logo.left, logo.top, logo.right, logo.bottom};
    TextOut2(dc, L"O", lt, g->fBold, P.white, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    RECT nm{S(60), S(24), S(kSideW) - S(10), S(46)};
    TextOut2(dc, L"OpenPuls", nm, g->fBold, P.sideText, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    RECT vs{S(60), S(44), S(kSideW) - S(10), S(62)};
    TextOut2(dc, L"响应优化器 v1.0", vs, g->fSmall, P.sideDim, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    int y = S(96);
    for (int i = 0; i < kPageCount; ++i) {
        RECT r{S(12), y, S(kSideW) - S(12), y + S(40)};
        bool sel = (i == g->page);
        bool hot = (i == g->navHot);
        if (sel || hot) {
            FillRound(dc, r, S(8), sel ? P.sideSel : P.sideHot, sel ? P.sideSel : P.sideHot, 1);
        }
        if (sel) {
            RECT bar{S(12), y + S(10), S(12) + S(3), y + S(30)};
            HBRUSH ab = CreateSolidBrush(P.accent);
            FillRect(dc, &bar, ab);
            DeleteObject(ab);
        }
        RECT tr{r.left + S(16), y, r.right - S(8), y + S(40)};
        TextOut2(dc, kPages[i].title, tr, sel ? g->fBold : g->fBase, sel ? P.sideText : P.sideDim,
                 DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        y += S(42);
    }

    // 底部状态
    int fy = H - S(96);
    RECT ln{S(22), fy, S(kSideW) - S(18), fy + S(1)};
    HBRUSH lb = CreateSolidBrush(P.sideHot);
    FillRect(dc, &ln, lb);
    DeleteObject(lb);
    MonSample ms = Monitor::Inst().Last();
    std::wstring line1 = Monitor::Inst().Running() ? L"常驻监控运行中" : L"常驻监控未启动";
    RECT s1{S(22), fy + S(10), S(kSideW) - S(12), fy + S(28)};
    TextOut2(dc, line1, s1, g->fSmall, P.sideDim, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    std::wstring line2 = ms.ping >= 0 ? Fmt(L"资源管理器 %.0f ms · CPU %.0f%%", ms.ping, ms.cpu) : L"—";
    RECT s2{S(22), fy + S(30), S(kSideW) - S(12), fy + S(48)};
    TextOut2(dc, line2, s2, g->fSmall, P.sideDim, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
}

void PaintHeader(HDC dc, int W) {
    int x = S(kSideW) + S(kPadX);
    RECT t{x, S(22), W - S(kPadX) - (g->admin ? 0 : S(164)), S(54)};
    TextOut2(dc, kPages[g->page].title, t, g->fTitle, P.text, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT st{x, S(54), W - S(kPadX), S(76)};
    TextOut2(dc, kPages[g->page].sub, st, g->fSmall, P.dim, DT_LEFT | DT_SINGLELINE);
    if (!g->admin) {
        // 提示条
        RECT nb{x, S(66), W - S(kPadX) - S(164), S(80)};
        (void)nb;
    }
    RECT line{x, S(kHeaderH) - S(1), W - S(kPadX), S(kHeaderH)};
    HBRUSH lb = CreateSolidBrush(P.border);
    FillRect(dc, &line, lb);
    DeleteObject(lb);
}

// ---------------------------------------------------------------- 总览页
void PaintCard(HDC dc, RECT r, const std::wstring& label, const std::wstring& value,
               const std::wstring& sub, COLORREF accent) {
    FillRound(dc, r, S(10), P.card, P.border, 1);
    RECT lr{r.left + S(16), r.top + S(12), r.right - S(12), r.top + S(32)};
    TextOut2(dc, label, lr, g->fSmall, P.dim, DT_LEFT | DT_SINGLELINE);
    RECT vr{r.left + S(16), r.top + S(34), r.right - S(12), r.top + S(70)};
    TextOut2(dc, value, vr, g->fBig, accent, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT sr{r.left + S(16), r.top + S(74), r.right - S(12), r.bottom - S(8)};
    TextOut2(dc, sub, sr, g->fSmall, P.dim, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void PaintDashboard(HDC dc, int W, int H) {
    int x = S(kSideW) + S(kPadX);
    int w = W - x - S(kPadX);
    int gap = S(12);
    int cw = (w - gap) / 2;
    int ch = S(116);
    int y = ContentTop();
    const TweakDef* ct = g->eng.Find(L"classic_menu");
    bool classic = ct && g->eng.Probe(*ct) == TState::Applied;
    MonSample ms = Monitor::Inst().Last();

    PaintCard(dc, RECT{x, y, x + cw, y + ch}, L"右键菜单",
              classic ? L"经典菜单" : L"Win11 新版",
              g->repValid ? Fmt(L"构建 %.0f ms · %d 项", g->rep.ctxFileMs, g->rep.itemsFile)
                          : L"尚未体检，右键页可单独测试",
              classic ? P.good : P.warn);
    PaintCard(dc, RECT{x + cw + gap, y, x + cw + gap + cw, y + ch}, L"右键扩展",
              Fmt(L"%d 个", (int)g->exts.size()), Fmt(L"已屏蔽 %d 个", g->eng.BlockedCount()), P.text);
    y += ch + gap;
    PaintCard(dc, RECT{x, y, x + cw, y + ch}, L"资源管理器",
              ms.ping >= 0 ? Fmt(L"%.0f ms", ms.ping) : L"—",
              Fmt(L"CPU %.0f%% · 内存 %.0f MB", ms.cpu, ms.ws),
              ms.ping > 200 ? P.bad : (ms.ping > 50 ? P.warn : P.good));
    const TweakDef* pt = g->eng.Find(L"power_plan");
    bool high = pt && g->eng.Probe(*pt) == TState::Applied;
    PaintCard(dc, RECT{x + cw + gap, y, x + cw + gap + cw, y + ch}, L"电源",
              high ? L"高性能" : L"平衡", high ? L"调度响应更快" : L"低频时右键易卡顿",
              high ? P.good : P.warn);

    // 事件区
    int iy = S(kHeaderH) + 2 * (S(116) + S(12)) + S(16) + S(36) + S(18);
    RECT ir{x, iy, x + w, H - S(16)};
    FillRound(dc, ir, S(10), P.card, P.border, 1);
    RECT lr{ir.left + S(16), ir.top + S(10), ir.right - S(12), ir.top + S(28)};
    TextOut2(dc, L"运行状态", lr, g->fBold, P.text, DT_LEFT | DT_SINGLELINE);
    std::vector<std::wstring> lines;
    lines.push_back(Monitor::Inst().GuardEnabled()
                        ? L"守护：每 60 秒校验一次已开启的优化项，被系统或其它工具改动会自动恢复。"
                        : L"守护：已暂停，优化项只在你手动切换时写入。");
    std::wstring al = Monitor::Inst().LastAlertOr();
    lines.push_back(al.empty() ? L"告警：暂无。资源管理器响应超过 400ms 会在这里提示。" : L"告警：" + al);
    if (g->repValid) {
        lines.push_back(Fmt(L"体检：%s，右键 %.0fms，扩展 %d 个（慢扩展 %d 个）", g->rep.stamp.c_str(),
                            g->rep.ctxFileMs, g->rep.extTotal, g->rep.slowExtCount));
    } else {
        lines.push_back(L"体检：尚未运行，点上方「一键体检」测量真实耗时。");
    }
    if (!g->statusText.empty()) lines.push_back(L"提示：" + g->statusText);
    int ly = ir.top + S(32);
    for (auto& s : lines) {
        RECT r{ir.left + S(16), ly, ir.right - S(12), ly + S(22)};
        TextOut2(dc, s, r, g->fSmall, P.dim, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        ly += S(22);
    }
}

// ---------------------------------------------------------------- 诊断页
void PaintDiag(HDC dc, int W, int H) {
    int x = S(kSideW) + S(kPadX);
    int w = W - x - S(kPadX);
    int y = ContentTop();
    RECT card{x, y, x + w, H - S(56)};
    FillRound(dc, card, S(10), P.card, P.border, 1);
    int c1 = card.left + S(16);
    int c2 = card.left + S(16) + (w - S(32)) * 55 / 100;
    int c3 = card.left + S(16) + (w - S(32)) * 80 / 100;
    RECT hr{c1, y + S(12), card.right - S(16), y + S(32)};
    TextOut2(dc, L"项目", RECT{c1, hr.top, c2 - S(8), hr.bottom}, g->fSmall, P.dim, DT_LEFT | DT_SINGLELINE);
    TextOut2(dc, L"实测", RECT{c2, hr.top, c3 - S(8), hr.bottom}, g->fSmall, P.dim, DT_LEFT | DT_SINGLELINE);
    TextOut2(dc, L"评价", RECT{c3, hr.top, card.right - S(16), hr.bottom}, g->fSmall, P.dim,
             DT_LEFT | DT_SINGLELINE);
    int ry = y + S(36);
    if (!g->repValid) {
        std::wstring tip = g->diagRunning ? Fmt(L"正在体检：%s（%d%%）", g->diagText.c_str(), g->diagPct)
                                          : L"尚未体检。点「开始体检」会依次测量右键构建、逐个扩展耗时、"
                                            L"打开对话框与合成器帧间隔。";
        RECT r{c1, ry, card.right - S(16), ry + S(24)};
        TextOut2(dc, tip, r, g->fBase, P.dim, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        ry += S(30);
    } else {
        for (auto& m : g->rep.metrics) {
            if (ry > card.bottom - S(190)) break;
            COLORREF c = m.level == 0 ? P.good : (m.level == 1 ? P.text : (m.level == 2 ? P.warn : P.bad));
            RECT r1{c1, ry, c2 - S(8), ry + S(24)};
            RECT r2{c2, ry, c3 - S(8), ry + S(24)};
            RECT r3{c3, ry, card.right - S(16), ry + S(24)};
            TextOut2(dc, m.name, r1, g->fBase, P.text, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
            TextOut2(dc, m.value, r2, g->fBase, P.dim, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
            TextOut2(dc, m.verdict, r3, g->fBold, c, DT_LEFT | DT_SINGLELINE);
            ry += S(26);
        }
    }

    // 趋势图
    int cy = std::max<int>(ry + S(10), (int)card.bottom - S(170));
    RECT cr{c1, cy, card.right - S(16), card.bottom - S(12)};
    RECT clr{cr.left, cr.top - S(18), cr.right, cr.top};
    TextOut2(dc, L"近 5 分钟：资源管理器响应（蓝）与 CPU 占用（橙）", clr, g->fSmall, P.dim,
             DT_LEFT | DT_SINGLELINE);
    auto hist = Monitor::Inst().Snapshot();
    HBRUSH bg = CreateSolidBrush(g->dark ? RGB(0x1B, 0x20, 0x29) : RGB(0xF7, 0xF8, 0xFA));
    FillRect(dc, &cr, bg);
    DeleteObject(bg);
    HPEN gp = CreatePen(PS_SOLID, 1, P.border);
    HGDIOBJ op = SelectObject(dc, gp);
    for (int i = 1; i < 4; ++i) {
        int gy = cr.top + (cr.bottom - cr.top) * i / 4;
        MoveToEx(dc, cr.left, gy, nullptr);
        LineTo(dc, cr.right, gy);
    }
    SelectObject(dc, op);
    DeleteObject(gp);
    if (hist.size() > 1) {
        std::vector<POINT> p1, p2;
        int n = (int)hist.size();
        for (int i = 0; i < n; ++i) {
            double px = cr.left + (double)(cr.right - cr.left - 2) * i / (n - 1);
            double pv = hist[i].ping < 0 ? 0 : hist[i].ping;
            double yv = 1.0 - std::min(1.0, pv / 600.0);
            p1.push_back({(LONG)px, (LONG)(cr.top + (cr.bottom - cr.top) * yv)});
            double cv = std::min(1.0, hist[i].cpu / 100.0);
            p2.push_back({(LONG)px, (LONG)(cr.top + (cr.bottom - cr.top) * (1.0 - cv))});
        }
        HPEN pn1 = CreatePen(PS_SOLID, 2, P.accent);
        op = SelectObject(dc, pn1);
        Polyline(dc, p1.data(), (int)p1.size());
        SelectObject(dc, op);
        DeleteObject(pn1);
        HPEN pn2 = CreatePen(PS_SOLID, 2, P.warn);
        op = SelectObject(dc, pn2);
        Polyline(dc, p2.data(), (int)p2.size());
        SelectObject(dc, op);
        DeleteObject(pn2);
    }
}

// ---------------------------------------------------------------- 其它页绘制
void PaintSimpleContent(HDC dc, int W, int H) {
    int x = S(kSideW) + S(kPadX);
    int w = W - x - S(kPadX);
    if (g->page == 1) {
        RECT lr{x, ContentTop() - S(2), x + w, ContentTop() + S(20)};
        TextOut2(dc, L"菜单行为", lr, g->fSmall, P.dim, DT_LEFT | DT_SINGLELINE);
        RECT l2{x, g->listY - S(24), x + w, g->listY - S(2)};
        TextOut2(dc, L"第三方右键扩展（取消勾选＝屏蔽；勾选＝启用）", l2, g->fSmall, P.dim,
                 DT_LEFT | DT_SINGLELINE);
        if (g->exts.empty()) {
            RECT r{x + S(14), g->listY + S(14), x + w - S(14), g->listY + S(40)};
            TextOut2(dc, g->scanText.empty() ? L"点击「重新扫描」读取已注册的右键扩展。" : g->scanText, r,
                     g->fBase, P.dim, DT_LEFT | DT_SINGLELINE);
        }
    } else if (g->page == 3) {
        int y = ContentTop() + S(2);
        for (auto* r : g->rows) {
            if (r->page != g->page) continue;
            y += S(kRowH) + S(kRowGap);
        }
        RECT nr{x, y + S(8), x + w, y + S(116)};
        FillRound(dc, nr, S(10), P.soft, P.soft, 1);
        RECT t1{nr.left + S(16), nr.top + S(12), nr.right - S(16), nr.top + S(32)};
        TextOut2(dc, IsTouchPresent() ? L"已检测到触摸设备" : L"未检测到触摸设备（平板/触摸本才会生效）",
                 t1, g->fBold, P.text, DT_LEFT | DT_SINGLELINE);
        RECT t2{nr.left + S(16), nr.top + S(34), nr.right - S(16), nr.bottom - S(8)};
        TextOut2(dc,
                 L"Win11 的触控延迟主要来自：长按判定等待、惯性滚动尾巴、以及长按弹出的新版菜单。"
                 L"上面前两项可直接调整；点「输入延迟实测」用指针画一个圆圈，看按下到绘制完成的本地延迟。",
                 t2, g->fSmall, P.dim, DT_LEFT | DT_WORDBREAK);
    } else if (g->page == 5) {
        int y = ContentTop();
        for (auto* r : g->rows) {
            if (r->page != g->page) continue;
            y += S(kRowH) + S(kRowGap);
        }
        RECT ar{x, y + S(14), x + w, H - S(56)};
        std::wstring about = Fmt(
            L"OpenPuls 响应优化器 1.0 · 常驻守护 + 手动面板\n"
            L"写入前自动备份到 HKCU\\Software\\OpenPuls\\ShellOpt\\Backup，「还原全部」可精确恢复原值。\n"
            L"日志：%s\n"
            L"测量方法：右键耗时由独立子进程调用 IContextMenu::QueryContextMenu 得到，单个扩展超时 3 秒判定为阻塞。",
            LogFilePath().c_str());
        int ly = ar.top;
        size_t start = 0;
        while (start <= about.size()) {
            size_t nl = about.find(L'\n', start);
            std::wstring line = about.substr(start, nl == std::wstring::npos ? std::wstring::npos : nl - start);
            RECT lr{ar.left, ly, ar.right, ly + S(20)};
            TextOut2(dc, line, lr, g->fSmall, P.dim, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
            ly += S(20);
            if (nl == std::wstring::npos) break;
            start = nl + 1;
        }
    }
}

// ---------------------------------------------------------------- 主窗口绘制
void PaintMain(HWND h, HDC target) {
    RECT rc;
    GetClientRect(h, &rc);
    int W = rc.right, H = rc.bottom;
    HDC mem = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, W, H);
    HGDIOBJ old = SelectObject(mem, bmp);
    HBRUSH bg = CreateSolidBrush(P.bg);
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    PaintSidebar(mem, H);
    PaintHeader(mem, W);
    switch (g->page) {
        case 0: PaintDashboard(mem, W, H); break;
        case 4: PaintDiag(mem, W, H); break;
        default: PaintSimpleContent(mem, W, H); break;
    }
    BitBlt(target, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
}

// ---------------------------------------------------------------- 列表
void RefreshExtList() {
    if (!g->hList) return;
    g->listUpdating = true;
    ListView_DeleteAllItems(g->hList);
    g->listClsids.clear();
    int i = 0;
    for (auto& e : g->exts) {
        LVITEMW it{};
        it.mask = LVIF_TEXT;
        it.iItem = i;
        it.pszText = (LPWSTR)e.name.c_str();
        ListView_InsertItem(g->hList, &it);
        ListView_SetItemText(g->hList, i, 1, (LPWSTR)e.kinds.c_str());
        std::wstring t = e.blocked ? L"—" : (e.benchMs >= 0 ? Fmt(L"%.0f ms", e.benchMs) : L"未测");
        ListView_SetItemText(g->hList, i, 2, (LPWSTR)t.c_str());
        ListView_SetItemText(g->hList, i, 3, (LPWSTR)(e.blocked ? L"已屏蔽" : L"启用"));
        std::wstring mod = e.module.empty() ? e.clsid : e.module;
        ListView_SetItemText(g->hList, i, 4, (LPWSTR)mod.c_str());
        ListView_SetCheckState(g->hList, i, e.blocked ? FALSE : TRUE);
        g->listClsids.push_back(e.clsid);
        ++i;
    }
    g->listUpdating = false;
    InvalidateRect(g->hList, nullptr, TRUE);
}

// 体检完成后只刷新耗时单元格，保持行的顺序不变，避免用户点选时错位
void MergeExtTimes(const std::vector<ShellExt>& fresh) {
    if (!g->hList) return;
    g->listUpdating = true;
    int count = ListView_GetItemCount(g->hList);
    for (int i = 0; i < count && i < (int)g->listClsids.size() && i < (int)g->exts.size(); ++i) {
        for (auto& e : fresh) {
            if (_wcsicmp(e.clsid.c_str(), g->listClsids[i].c_str()) != 0) continue;
            g->exts[i].benchMs = e.benchMs;
            g->exts[i].benchNote = e.benchNote;
            std::wstring t = g->exts[i].blocked
                                 ? L"—"
                                 : (g->exts[i].benchMs >= 0 ? Fmt(L"%.0f ms", g->exts[i].benchMs)
                                                            : (g->exts[i].benchNote.empty() ? L"未测"
                                                                                            : g->exts[i].benchNote));
            ListView_SetItemText(g->hList, i, 2, (LPWSTR)t.c_str());
            break;
        }
    }
    g->listUpdating = false;
}

void ScanExts(bool bench) {
    SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    g->scanText = L"正在扫描…";
    g->exts = g->eng.EnumerateShellExts();
    if (bench) {
        int i = 0;
        for (auto& e : g->exts) {
            if (!e.blocked) g->eng.RunExtBench(&e);
            ++i;
            g->scanText = Fmt(L"逐个测试 %d/%d…", i, (int)g->exts.size());
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        std::stable_sort(g->exts.begin(), g->exts.end(),
                         [](const ShellExt& a, const ShellExt& b) { return a.benchMs > b.benchMs; });
    }
    g->scanText.clear();
    RefreshExtList();
    g->statusText = Fmt(L"扫描完成：%d 个扩展", (int)g->exts.size());
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    InvalidateRect(g->hwnd, nullptr, FALSE);
    RefreshStates();
}

// ---------------------------------------------------------------- 体检
struct DiagOut {
    diag::Report rep;
    std::vector<ShellExt> exts;
};

void StartDiag(bool silentIfRunning = true) {
    if (g->diagRunning) {
        if (!silentIfRunning) MessageBoxW(g->hwnd, L"体检正在进行中…", L"OpenPuls", MB_OK);
        return;
    }
    g->diagRunning = true;
    g->diagText = L"准备";
    g->diagPct = 0;
    InvalidateRect(g->hwnd, nullptr, FALSE);
    HWND wnd = g->hwnd;
    std::thread([wnd] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        auto* out = new DiagOut();
        diag::RunFull(&out->rep, &out->exts, [wnd](const std::wstring& t, int p) {
            auto* payload = new std::pair<std::wstring, int>(t, p);
            PostMessageW(wnd, WM_OP_DIAG_PROGRESS, 0, (LPARAM)payload);
        });
        CoUninitialize();
        PostMessageW(wnd, WM_OP_DIAG_DONE, 0, (LPARAM)out);
    }).detach();
}

// ---------------------------------------------------------------- 提示
void Info(const std::wstring& text, bool err = false) {
    MessageBoxW(g->hwnd, text.c_str(), L"OpenPuls 响应优化器", MB_OK | (err ? MB_ICONWARNING : MB_ICONINFORMATION));
}

// ---------------------------------------------------------------- 输入延迟窗
struct LatState {
    double lastMs = -1;
    double sum = 0;
    int count = 0;
    POINT pt{0, 0};
    double startMs = 0;
    bool pending = false;
};

void PaintLatency(HWND h, HDC dc) {
    auto* st = (LatState*)GetWindowLongPtrW(h, GWLP_USERDATA);
    RECT rc;
    GetClientRect(h, &rc);
    HBRUSH bg = CreateSolidBrush(RGB(0x10, 0x14, 0x1B));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0xE8, 0xEE, 0xF6));
    SelectObject(dc, g->fTitle);
    RECT t{20, 18, rc.right - 20, 60};
    DrawTextW(dc, L"输入延迟实测", -1, &t, DT_LEFT | DT_SINGLELINE);
    SetTextColor(dc, RGB(0x9B, 0xA4, 0xB3));
    SelectObject(dc, g->fSmall);
    RECT t2{20, 58, rc.right - 20, 96};
    DrawTextW(dc,
              L"在窗口内点击或触摸：测量「指针按下 → 绘制完成」的本地往返，越接近 1 帧（16.7ms）越好。"
              L"这是应用侧延迟，系统合成器与驱动延迟会叠加在上面。",
              -1, &t2, DT_LEFT | DT_WORDBREAK);
    if (st) {
        if (st->pending && st->lastMs >= 0) {
            HBRUSH b = CreateSolidBrush(RGB(0x2A, 0x6F, 0xD8));
            HGDIOBJ ob = SelectObject(dc, b);
            HPEN p = CreatePen(PS_SOLID, 2, RGB(0x6F, 0xB0, 0xFF));
            HGDIOBJ op = SelectObject(dc, p);
            Ellipse(dc, st->pt.x - 26, st->pt.y - 26, st->pt.x + 26, st->pt.y + 26);
            SelectObject(dc, ob);
            SelectObject(dc, op);
            DeleteObject(b);
            DeleteObject(p);
        }
        SetTextColor(dc, RGB(0xE8, 0xEE, 0xF6));
        SelectObject(dc, g->fBig);
        RECT v{20, rc.bottom - 110, rc.right - 20, rc.bottom - 56};
        std::wstring s = st->lastMs < 0 ? L"—" : Fmt(L"%.1f ms", st->lastMs);
        DrawTextW(dc, s.c_str(), -1, &v, DT_LEFT | DT_SINGLELINE);
        SelectObject(dc, g->fSmall);
        SetTextColor(dc, RGB(0x9B, 0xA4, 0xB3));
        RECT v2{20, rc.bottom - 52, rc.right - 20, rc.bottom - 20};
        std::wstring s2 = st->count ? Fmt(L"样本 %d · 平均 %.1f ms · 最差 %.1f ms", st->count,
                                          st->sum / st->count, st->lastMs)
                                    : L"尚未采样";
        DrawTextW(dc, s2.c_str(), -1, &v2, DT_LEFT | DT_SINGLELINE);
    }
}

LRESULT CALLBACK LatProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* st = (LatState*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
        case WM_CREATE: {
            auto* s = new LatState();
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)s);
            return 0;
        }
        case WM_DESTROY:
            if (st) {
                SetWindowLongPtrW(h, GWLP_USERDATA, 0);
                delete st;
            }
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_LBUTTONDOWN:
            if (st) {
                st->pt = POINT{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                st->pending = true;
                st->startMs = QpcMs();
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            PaintLatency(h, dc);
            EndPaint(h, &ps);
            if (st && st->pending) {
                double ms = QpcMs() - st->startMs;
                st->pending = false;
                st->lastMs = ms;
                st->sum += ms;
                st->count++;
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(h);
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void OpenLatencyWindow() {
    if (g->hLat && IsWindow(g->hLat)) {
        ShowWindow(g->hLat, SW_SHOW);
        SetForegroundWindow(g->hLat);
        return;
    }
    g->hLat = CreateWindowExW(WS_EX_TOOLWINDOW, kClassLat, L"输入延迟实测", WS_OVERLAPPED | WS_CAPTION |
                                  WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, S(520), S(360),
                              g->hwnd, nullptr, g->inst, nullptr);
    if (g->hLat) {
        int dpi = GetDpiForWindow(g->hLat);
        (void)dpi;
    }
}

// ---------------------------------------------------------------- 托盘
void AddTray() {
    if (g->trayAdded) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g->hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_OP_TRAY;
    nid.hIcon = g->iconSmall;
    wcscpy_s(nid.szTip, L"OpenPuls 响应优化器");
    Shell_NotifyIconW(NIM_ADD, &nid);
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
    g->trayAdded = true;
}

void UpdateTray() {
    if (!g->trayAdded) return;
    double now = QpcMs();
    if (now - g->lastTrayUpdate < 2500) return;
    g->lastTrayUpdate = now;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g->hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_TIP;
    MonSample ms = Monitor::Inst().Last();
    std::wstring tip = L"OpenPuls 响应优化器\n";
    if (ms.ping >= 0) tip += Fmt(L"资源管理器 %.0f ms · CPU %.0f%%", ms.ping, ms.cpu);
    wcscpy_s(nid.szTip, tip.c_str());
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void ShowTrayMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_BTN_DIAG, L"立即体检");
    AppendMenuW(m, MF_STRING | (Monitor::Inst().GuardEnabled() ? MF_CHECKED : 0), 5001, L"常驻守护");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 5002, L"打开设置面板");
    AppendMenuW(m, MF_STRING, ID_BTN_RESTART, L"重启资源管理器");
    AppendMenuW(m, MF_STRING, ID_BTN_OPENLOG, L"打开日志");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 5003, L"退出");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g->hwnd);
    int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0,
                                  g->hwnd, nullptr);
    DestroyMenu(m);
    if (cmd) PostMessageW(g->hwnd, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
}

void ShowMainWindow() {
    ShowWindow(g->hwnd, SW_SHOW);
    ShowWindow(g->hwnd, SW_RESTORE);
    SetForegroundWindow(g->hwnd);
    RefreshStates();
}

// ---------------------------------------------------------------- 动作
void RestartExplorerUi() {
    if (MessageBoxW(g->hwnd, L"重启资源管理器会关闭所有已打开的文件夹窗口，是否继续？",
                    L"OpenPuls", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    RestartExplorer();
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    g->statusText = L"资源管理器已重启";
    RefreshStates();
    InvalidateRect(g->hwnd, nullptr, FALSE);
}

void ToggleTweak(size_t idx) {
    auto& t = g->eng.All()[idx];
    if (t.admin && !g->admin) {
        Info(L"该优化项写在系统级注册表，需要管理员权限。\n请点右上角「以管理员身份重启」后再试。", true);
        return;
    }
    if (!t.supported) {
        Info(L"当前设备没有可用的触摸屏，该项不适用。", true);
        return;
    }
    bool turnOn = g->eng.Probe(t) != TState::Applied;
    std::wstring err;
    if (!g->eng.SetEnabled(t, turnOn, &err)) {
        Info(Fmt(L"操作失败：%s", err.c_str()), true);
        return;
    }
    g->statusText = t.title + (turnOn ? L"：已开启" : L"：已关闭");
    RefreshStates();
    InvalidateRect(g->hwnd, nullptr, FALSE);
    if (t.needsExplorer) {
        if (MessageBoxW(g->hwnd, L"该项需要重启资源管理器才能生效，现在重启？", L"OpenPuls",
                        MB_YESNO | MB_ICONQUESTION) == IDYES)
            RestartExplorerUi();
    }
}

void OptimizeAll() {
    const wchar_t* ids[] = {L"classic_menu", L"menu_delay", L"open_thispc", L"no_recent_track",
                            L"start_no_web",     L"no_sync_notify", L"no_anim",
                            L"power_plan",       L"power_throttle_off", L"prio_foreground"};
    int ok = 0, skip = 0;
    std::wstring err;
    for (auto id : ids) {
        const TweakDef* t = g->eng.Find(id);
        if (!t) continue;
        if (t->admin && !g->admin) {
            ++skip;
            continue;
        }
        if (g->eng.SetEnabled(*t, true, &err)) ++ok;
    }
    RefreshStates();
    Info(Fmt(L"已应用 %d 项优化%s。\n\n右键菜单改动需重启资源管理器后生效。", ok,
             skip ? Fmt(L"，另有 %d 项需要管理员权限被跳过", skip).c_str() : L""));
    if (MessageBoxW(g->hwnd, L"现在重启资源管理器让右键菜单生效？", L"OpenPuls",
                    MB_YESNO | MB_ICONQUESTION) == IDYES)
        RestartExplorerUi();
}

void RestoreAllUi() {
    if (MessageBoxW(g->hwnd, L"将所有优化项恢复为原始值（含扩展屏蔽），是否继续？", L"OpenPuls",
                    MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    std::wstring err;
    int n = g->eng.RestoreAll(&err);
    RegDelKeyTree(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Blocked");
    RefreshStates();
    g->statusText = Fmt(L"已还原 %d 项", n);
    Info(Fmt(L"已还原 %d 项。右键菜单相关改动需重启资源管理器。", n));
}

void ExportReport() {
    if (!g->repValid) {
        Info(L"请先运行一次体检。", true);
        return;
    }
    std::wstring md = diag::ToMarkdown(g->rep, g->exts);
    std::wstring path = DataDir() + L"\\report-" + g->rep.stamp;
    for (auto& c : path)
        if (c == L':' || c == L' ') c = L'-';
    path += L".md";
    WriteTextFile(path, WideToUtf8(md), true);
    std::wstring arg = L"/select,\"" + path + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOW);
}

void ToggleAutostart(bool on) {
    std::wstring runKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    if (on)
        RegSetStr(HKEY_CURRENT_USER, runKey, L"OpenPulsShellOpt", L"\"" + ExePath() + L"\" --tray");
    else
        RegDelValue(HKEY_CURRENT_USER, runKey, L"OpenPulsShellOpt");
}

void Elevate() {
    std::wstring args = L"--takeover";
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = ExePath().c_str();
    sei.lpParameters = args.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) {
        Info(L"提权被取消。", true);
        return;
    }
    PostMessageW(g->hwnd, WM_OP_QUIT, 0, 0);
}

// ---------------------------------------------------------------- 命令
void OnCommand(int id) {
    if (id >= ID_TOGGLE_BASE && id < ID_TOGGLE_BASE + 200) {
        size_t idx = (size_t)(id - ID_TOGGLE_BASE);
        if (idx < g->eng.All().size()) ToggleTweak(idx);
        return;
    }
    switch (id) {
        case ID_SET_AUTOSTART: {
            std::wstring v;
            bool on = RegGetStr(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                L"OpenPulsShellOpt", &v);
            ToggleAutostart(!on);
            RefreshStates();
            break;
        }
        case ID_SET_STARTMIN:
            g->startMin = !g->startMin;
            SettingsSetInt(L"start_min", g->startMin ? 1 : 0);
            RefreshStates();
            break;
        case ID_SET_CLOSETRAY:
            g->closeToTray = !g->closeToTray;
            SettingsSetInt(L"close_tray", g->closeToTray ? 1 : 0);
            RefreshStates();
            break;
        case ID_SET_GUARD:
            Monitor::Inst().SetGuardEnabled(!Monitor::Inst().GuardEnabled());
            SettingsSetInt(L"guard", Monitor::Inst().GuardEnabled() ? 1 : 0);
            RefreshStates();
            break;
        case ID_SET_ALERTS:
            g->alerts = !g->alerts;
            SettingsSetInt(L"alerts", g->alerts ? 1 : 0);
            RefreshStates();
            break;
        case ID_BTN_DIAG: StartDiag(); break;
        case ID_BTN_OPTIMIZE: OptimizeAll(); break;
        case ID_BTN_RESTORE: RestoreAllUi(); break;
        case ID_BTN_SCAN: ScanExts(false); break;
        case ID_BTN_BENCH_ALL: ScanExts(true); break;
        case ID_BTN_RESTART: RestartExplorerUi(); break;
        case ID_BTN_LATENCY: OpenLatencyWindow(); break;
        case ID_BTN_EXPORT: ExportReport(); break;
        case ID_BTN_REBUILD: {
            std::wstring err;
            SetCursor(LoadCursorW(nullptr, IDC_WAIT));
            g->eng.BuildIconCache(&err);
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            Info(Fmt(L"%s", err.c_str()));
            break;
        }
        case ID_BTN_ELEVATE: Elevate(); break;
        case ID_BTN_OPENLOG: {
            std::wstring p = LogFilePath();
            ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOW);
            break;
        }
        case 5001:
            Monitor::Inst().SetGuardEnabled(!Monitor::Inst().GuardEnabled());
            SettingsSetInt(L"guard", Monitor::Inst().GuardEnabled() ? 1 : 0);
            RefreshStates();
            break;
        case 5002: ShowMainWindow(); break;
        case 5003: PostMessageW(g->hwnd, WM_OP_QUIT, 0, 0); break;
    }
    InvalidateRect(g->hwnd, nullptr, FALSE);
}

// ---------------------------------------------------------------- 消息
void OnListNotify(LPARAM l) {
    if (g->listUpdating) return;
    auto* nv = (LPNMLISTVIEW)l;
    if (nv->hdr.code != LVN_ITEMCHANGED) return;
    if (!(nv->uChanged & LVIF_STATE)) return;
    UINT oldState = nv->uOldState & LVIS_STATEIMAGEMASK;
    UINT newState = nv->uNewState & LVIS_STATEIMAGEMASK;
    if (oldState == newState) return;
    int idx = nv->iItem;
    if (idx < 0 || idx >= (int)g->exts.size()) return;
    BOOL checked = ListView_GetCheckState(g->hList, idx);
    ShellExt& e = g->exts[idx];
    bool wantBlocked = checked == FALSE;
    if (wantBlocked == e.blocked) return;
    std::wstring err;
    if (!g->eng.SetExtBlocked(e, wantBlocked, &err)) {
        Info(err, true);
        g->listUpdating = true;
        ListView_SetCheckState(g->hList, idx, e.blocked ? FALSE : TRUE);
        g->listUpdating = false;
        return;
    }
    e.blocked = wantBlocked;
    g->listUpdating = true;
    ListView_SetItemText(g->hList, idx, 3, (LPWSTR)(e.blocked ? L"已屏蔽" : L"启用"));
    g->listUpdating = false;
    g->statusText = Fmt(L"%s：%s（重启资源管理器后生效）", e.name.c_str(),
                        e.blocked ? L"已屏蔽" : L"已启用");
    InvalidateRect(g->hwnd, nullptr, FALSE);
}

LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_CREATE:
            g->hwnd = h;
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            PaintMain(h, dc);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_SIZE:
            LayoutPage();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mm = (MINMAXINFO*)l;
            mm->ptMinTrackSize.x = S(980);
            mm->ptMinTrackSize.y = S(700);
            return 0;
        }
        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
            int hot = -1;
            if (x < S(kSideW)) {
                int iy = S(96);
                for (int i = 0; i < kPageCount; ++i) {
                    if (y >= iy && y < iy + S(40)) {
                        hot = i;
                        break;
                    }
                    iy += S(42);
                }
            }
            if (hot != g->navHot) {
                g->navHot = hot;
                InvalidateRect(h, nullptr, FALSE);
            }
            if (hot >= 0) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, h, 0};
                TrackMouseEvent(&tme);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (g->navHot != -1) {
                g->navHot = -1;
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP: {
            int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
            if (x < S(kSideW)) {
                int iy = S(96);
                for (int i = 0; i < kPageCount; ++i) {
                    if (y >= iy && y < iy + S(40)) {
                        if (g->page != i) {
                            g->page = i;
                            LayoutPage();
                            if (i == 1 && g->exts.empty()) ScanExts(false);
                        }
                        break;
                    }
                    iy += S(42);
                }
            }
            return 0;
        }
        case WM_COMMAND:
            OnCommand(LOWORD(w));
            return 0;
        case WM_NOTIFY:
            if (g->hList && ((NMHDR*)l)->hwndFrom == g->hList) {
                auto* nh = (NMHDR*)l;
                if (nh->code == LVN_ITEMCHANGED) {
                    OnListNotify(l);
                } else if (nh->code == NM_CUSTOMDRAW) {
                    auto* cd = (LPNMLVCUSTOMDRAW)l;
                    if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                    if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                        cd->clrTextBk = P.card;
                        cd->clrText = P.text;
                        return CDRF_NEWFONT;
                    }
                    return CDRF_DODEFAULT;
                }
            }
            return 0;
        case WM_OP_MONITOR:
            UpdateTray();
            if (g->page == 0 || g->page == 4) InvalidateRect(h, nullptr, FALSE);
            if (g->page == 5 || g->page == 0) {
                // 侧栏状态也刷新
                RECT r{0, 0, S(kSideW), 800};
                InvalidateRect(h, &r, FALSE);
            }
            return 0;
        case WM_OP_TRAY:
            if (LOWORD(l) == WM_LBUTTONUP || LOWORD(l) == NIN_SELECT || LOWORD(l) == WM_LBUTTONDBLCLK)
                ShowMainWindow();
            else if (LOWORD(l) == WM_RBUTTONUP || LOWORD(l) == WM_CONTEXTMENU)
                ShowTrayMenu();
            return 0;
        case WM_OP_DIAG_PROGRESS: {
            auto* payload = (std::pair<std::wstring, int>*)l;
            g->diagText = payload->first;
            g->diagPct = payload->second;
            delete payload;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_OP_DIAG_DONE: {
            auto* out = (DiagOut*)l;
            g->rep = out->rep;
            if (!g->hList || ListView_GetItemCount(g->hList) == 0) {
                g->exts = out->exts;
                RefreshExtList();
            } else {
                MergeExtTimes(out->exts);
            }
            g->repValid = true;
            g->diagRunning = false;
            delete out;
            RefreshStates();
            g->statusText = L"体检完成";
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_OP_ACTIVATE:
            ShowMainWindow();
            return 0;
        case WM_OP_QUIT:
            DestroyWindow(h);
            return 0;
        case WM_SETTINGCHANGE:
            if (w == SPI_SETNONCLIENTMETRICS || w == 0) {
                bool dark = SystemUsesDark();
                if (dark != g->dark) ApplyTheme();
            }
            return 0;
        case WM_DPICHANGED: {
            if (!g_dpiOverride) {
                g->dpi = HIWORD(w);
                RebuildFonts();
            }
            RECT* r = (RECT*)l;
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            LayoutPage();
            return 0;
        }
        case WM_TIMER:
            if (w == 1) {
                KillTimer(h, 1);
                StartDiag();
            }
            return 0;
        case WM_CLOSE:
            if (g->closeToTray) {
                ShowWindow(h, SW_HIDE);
                return 0;
            }
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            Monitor::Inst().Stop();
            if (g->trayAdded) {
                NOTIFYICONDATAW nid{};
                nid.cbSize = sizeof(nid);
                nid.hWnd = h;
                nid.uID = 1;
                Shell_NotifyIconW(NIM_DELETE, &nid);
            }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void BuildUi() {
    // 行
    int i = 0;
    for (auto& t : g->eng.All()) {
        int page = 2;
        switch (t.group) {
            case TGroup::Menu: page = 1; break;
            case TGroup::Launch: page = 2; break;
            case TGroup::Touch: page = 3; break;
            case TGroup::Advanced: page = 5; break;
        }
        CreateRow(page, t.title, t.desc, ID_TOGGLE_BASE + i, 0, L"", t.id);
        ++i;
    }
    // 设置页附加行
    CreateRow(5, L"开机自动启动", L"登录后自动进入托盘常驻，优化项由守护线程维持。", ID_SET_AUTOSTART, 0, L"",
              L"@autostart");
    CreateRow(5, L"启动时只进托盘", L"开机启动时不弹出面板，只在托盘区显示状态。", ID_SET_STARTMIN, 0, L"",
              L"@startmin");
    CreateRow(5, L"关闭窗口时最小化到托盘", L"点右上角关闭不退出程序，后台继续提供诊断与守护。", ID_SET_CLOSETRAY,
              0, L"", L"@closetray");
    CreateRow(5, L"常驻守护", L"每 60 秒检查一次已开启的优化项，被其它工具改回时自动重新应用。", ID_SET_GUARD,
              0, L"", L"@guard");
    CreateRow(5, L"异常提醒", L"资源管理器响应超过 400ms 时在总览页与日志中记录。", ID_SET_ALERTS, 0, L"",
              L"@alerts");

    // 按钮
    CreateBtn(0, L"一键体检", ID_BTN_DIAG, true);
    CreateBtn(0, L"应用推荐优化", ID_BTN_OPTIMIZE, false);
    CreateBtn(0, L"还原全部", ID_BTN_RESTORE, false);
    CreateBtn(1, L"重新扫描", ID_BTN_SCAN, false);
    CreateBtn(1, L"全部测试耗时", ID_BTN_BENCH_ALL, true);
    CreateBtn(1, L"重启资源管理器", ID_BTN_RESTART, false);
    CreateBtn(3, L"输入延迟实测", ID_BTN_LATENCY, true);
    CreateBtn(4, L"开始体检", ID_BTN_DIAG, true);
    CreateBtn(4, L"导出报告", ID_BTN_EXPORT, false);
    CreateBtn(5, L"重建图标缓存", ID_BTN_REBUILD, false);
    CreateBtn(5, L"打开日志", ID_BTN_OPENLOG, false);
    CreateBtn(-1, L"以管理员身份重启", ID_BTN_ELEVATE, false);

    // 列表
    g->hList = CreateWindowExW(0, WC_LISTVIEWW, L"",
                               WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP, 0,
                               0, 10, 10, g->hwnd, nullptr, g->inst, nullptr);
    ListView_SetExtendedListViewStyle(
        g->hList, LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    SendMessageW(g->hList, WM_SETFONT, (WPARAM)g->fBase, TRUE);
    SetWindowTheme(g->hList, L"", L"");  // 关掉视觉样式，让自绘配色生效
    struct Col {
        const wchar_t* name;
        int w;
    } cols[] = {{L"扩展名称", 198}, {L"作用对象", 148}, {L"单项耗时", 72}, {L"状态", 54}, {L"模块", 246}};
    for (int c = 0; c < 5; ++c) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.pszText = (LPWSTR)cols[c].name;
        col.cx = S(cols[c].w);
        col.iSubItem = c;
        ListView_InsertColumn(g->hList, c, &col);
    }
    ListView_SetBkColor(g->hList, P.card);
    ListView_SetTextBkColor(g->hList, P.card);
    ListView_SetTextColor(g->hList, P.text);
}

}  // namespace

int RunApp(HINSTANCE inst, int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    App app;
    g = &app;
    app.inst = inst;
    app.admin = IsAdmin();
    app.closeToTray = SettingsGetInt(L"close_tray", 1) != 0;
    app.startMin = SettingsGetInt(L"start_min", 0) != 0;
    app.alerts = SettingsGetInt(L"alerts", 1) != 0;
    app.eng.Init();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassMain;
    wc.hIcon = LoadIconW(inst, L"IDI_APP");
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    WNDCLASSEXW rc{};
    rc.cbSize = sizeof(rc);
    rc.lpfnWndProc = RowProc;
    rc.hInstance = inst;
    rc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    rc.lpszClassName = kClassRow;
    RegisterClassExW(&rc);

    rc.lpfnWndProc = BtnProc;
    rc.lpszClassName = kClassBtn;
    RegisterClassExW(&rc);

    rc.lpfnWndProc = LatProc;
    rc.lpszClassName = kClassLat;
    rc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    RegisterClassExW(&rc);

    app.iconBig = wc.hIcon ? wc.hIcon : LoadIconW(nullptr, IDI_APPLICATION);
    app.iconSmall = app.iconBig;

    wchar_t envbuf[16] = {};
    if (GetEnvironmentVariableW(L"OPENPULS_UI_SCALE", envbuf, 16) > 0) {
        int v = _wtoi(envbuf);
        if (v >= 72 && v <= 480) g_dpiOverride = v;
    }

    RECT wr{0, 0, 1080, 740};
    AdjustWindowRectEx(&wr, WS_OVERLAPPEDWINDOW, FALSE, 0);
    HWND h = CreateWindowExW(0, kClassMain, L"OpenPuls 响应优化器",
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top,
                             nullptr, nullptr, inst, nullptr);
    if (!h) return 1;
    g->hwnd = h;

    int dpi = g_dpiOverride ? g_dpiOverride : (int)GetDpiForWindow(h);
    if (dpi < 72) dpi = 96;
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int availW = wa.right - wa.left, availH = wa.bottom - wa.top;
    double fit = 1.0;
    if (availW > 0) fit = std::min(fit, (double)availW / (double)MulDiv(1100, dpi, 96));
    if (availH > 0) fit = std::min(fit, (double)availH / (double)MulDiv(770, dpi, 96));
    if (fit < 0.75) fit = 0.75;
    g->dpi = (int)(dpi * fit);
    if (g->dpi < 72) g->dpi = 72;
    RebuildFonts();

    RECT dr{0, 0, MulDiv(1080, g->dpi, 96), MulDiv(740, g->dpi, 96)};
    AdjustWindowRectEx(&dr, WS_OVERLAPPEDWINDOW, FALSE, 0);
    int ww = dr.right - dr.left, wh = dr.bottom - dr.top;
    if (availW > 0) ww = std::min(ww, availW);
    if (availH > 0) wh = std::min(wh, availH);
    SetWindowPos(h, nullptr, 0, 0, ww, wh, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    ApplyTheme();
    BuildUi();
    RefreshStates();
    LayoutPage();

    Monitor::Inst().SetGuardEnabled(SettingsGetInt(L"guard", 1) != 0);
    Monitor::Inst().SetIntervalMs(SettingsGetInt(L"interval_ms", 2000));
    Monitor::Inst().Start(h);
    AddTray();

    // 启动时自动体检一次（后台，稍等界面出现）
    SetTimer(h, 1, 1200, nullptr);

    bool mini = app.startMin;
    ShowWindow(h, mini ? SW_HIDE : (nCmdShow == SW_SHOWMINNOACTIVE ? SW_HIDE : SW_SHOW));
    UpdateWindow(h);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g = nullptr;
    CoUninitialize();
    return 0;
}

}  // namespace op
