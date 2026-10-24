#include "diag.h"

#include <shlobj.h>
#include <shobjidl.h>
#include <dwmapi.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <cstdio>
#include <algorithm>

namespace op {
namespace diag {

// ---------------------------------------------------------------- 工具
static std::wstring ReadKV(const std::wstring& text, const wchar_t* key) {
    std::wstring k = std::wstring(key) + L"=";
    size_t p = text.find(k);
    if (p == std::wstring::npos) return L"";
    size_t e = text.find_first_of(L"\r\n", p);
    return text.substr(p + k.size(), e == std::wstring::npos ? std::wstring::npos : e - p - k.size());
}

static std::wstring KV(const std::wstring& text, const wchar_t* key) { return ReadKV(text, key); }

static bool SpawnSelf(const std::wstring& args, DWORD timeoutMs, std::wstring* note, bool* timedOut) {
    std::wstring cmd = L"\"" + ExePath() + L"\" " + args;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                        &pi)) {
        *note = L"无法启动测量子进程";
        return false;
    }
    DWORD w = WaitForSingleObject(pi.hProcess, timeoutMs);
    *timedOut = (w == WAIT_TIMEOUT);
    if (*timedOut) {
        TerminateProcess(pi.hProcess, 9);
        WaitForSingleObject(pi.hProcess, 1000);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// ---------------------------------------------------------------- 子进程测量
struct BenchCtx {
    PIDLIST_ABSOLUTE folderPidl = nullptr;
    PIDLIST_ABSOLUTE childPidl = nullptr;
    IShellFolder* parent = nullptr;
    IShellFolder* folder = nullptr;
    IDataObject* dataObj = nullptr;

    ~BenchCtx() {
        if (dataObj) dataObj->Release();
        if (folder) folder->Release();
        if (parent) parent->Release();
        if (folderPidl) ILFree(folderPidl);
        if (childPidl) ILFree(childPidl);
    }
};

static bool BuildCtx(const std::wstring& kind, const std::wstring& path, BenchCtx* c, std::wstring* err) {
    PIDLIST_ABSOLUTE pidl = nullptr;
    SFGAOF attrs = 0;
    if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, &attrs)) || !pidl) {
        *err = L"路径解析失败";
        return false;
    }
    IShellFolder* parent = nullptr;
    PCUITEMID_CHILD ch = nullptr;
    if (FAILED(SHBindToParent(pidl, IID_IShellFolder, (void**)&parent, &ch))) {
        ILFree(pidl);
        *err = L"无法绑定外壳文件夹";
        return false;
    }
    c->parent = parent;
    if (kind == L"bg") {
        IShellFolder* self = nullptr;
        if (FAILED(parent->BindToObject(ch, nullptr, IID_IShellFolder, (void**)&self)) || !self) {
            ILFree(pidl);
            *err = L"无法打开目录";
            return false;
        }
        c->folder = self;
        c->folderPidl = pidl;
        SHCreateDataObject(pidl, 0, nullptr, nullptr, IID_PPV_ARGS(&c->dataObj));
    } else {
        c->childPidl = ILClone(ch);
        c->folderPidl = ILClone(pidl);
        ILRemoveLastID(c->folderPidl);
        PCUITEMID_CHILD arr[1] = {c->childPidl};
        SHCreateDataObject(c->folderPidl, 1, arr, nullptr, IID_PPV_ARGS(&c->dataObj));
        ILFree(pidl);
    }
    return true;
}

static int ChildExt(const std::wstring& clsidStr, const std::wstring& kind, const std::wstring& path,
                    const std::wstring& outFile) {
    std::wstring body;
    CLSID clsid{};
    if (FAILED(CLSIDFromString(clsidStr.c_str(), &clsid))) {
        body = L"ok=0\nnote=CLSID 无效\n";
        WriteTextFile(outFile, WideToUtf8(body));
        return 1;
    }
    BenchCtx ctx;
    std::wstring err;
    if (!BuildCtx(kind, path, &ctx, &err)) {
        body = L"ok=0\nnote=" + err + L"\n";
        WriteTextFile(outFile, WideToUtf8(body));
        return 1;
    }
    IShellExtInit* sei = nullptr;
    HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_IShellExtInit, (void**)&sei);
    if (FAILED(hr) || !sei) {
        body = Fmt(L"ok=0\nnote=无法创建扩展对象 (0x%08X)\n", (unsigned)hr);
        WriteTextFile(outFile, WideToUtf8(body));
        return 1;
    }
    Timer t;
    t.Start();
    sei->Initialize(ctx.folderPidl, ctx.dataObj, nullptr);
    IContextMenu* pcm = nullptr;
    int items = 0;
    bool hasMenu = false;
    if (SUCCEEDED(sei->QueryInterface(IID_IContextMenu, (void**)&pcm)) && pcm) {
        hasMenu = true;
        HMENU hm = CreatePopupMenu();
        pcm->QueryContextMenu(hm, 0, 1, 0x6FFF, CMF_NORMAL | CMF_EXPLORE);
        items = GetMenuItemCount(hm);
        DestroyMenu(hm);
        pcm->Release();
    }
    double ms = t.Ms();
    sei->Release();
    body = Fmt(L"ok=1\nms=%.2f\nitems=%d\niface=%d\n", ms, items, hasMenu ? 1 : 0);
    WriteTextFile(outFile, WideToUtf8(body));
    return 0;
}

static int ChildMenu(const std::wstring& kind, const std::wstring& path, int runs,
                     const std::wstring& outFile) {
    BenchCtx ctx;
    std::wstring err;
    if (!BuildCtx(kind, path, &ctx, &err)) {
        WriteTextFile(outFile, WideToUtf8(L"ok=0\nnote=" + err + L"\n"));
        return 1;
    }
    IContextMenu* pcm = nullptr;
    HRESULT hr;
    if (kind == L"bg")
        hr = ctx.folder->CreateViewObject(nullptr, IID_IContextMenu, (void**)&pcm);
    else {
        PCUITEMID_CHILD arr[1] = {ctx.childPidl};
        hr = ctx.parent->GetUIObjectOf(nullptr, 1, arr, IID_IContextMenu, nullptr, (void**)&pcm);
    }
    if (FAILED(hr) || !pcm) {
        WriteTextFile(outFile, WideToUtf8(Fmt(L"ok=0\nnote=无法获取右键菜单 (0x%08X)\n", (unsigned)hr)));
        return 1;
    }
    std::wstring body = L"ok=1\n";
    std::vector<double> times;
    int items = 0;
    for (int i = 0; i < (runs < 1 ? 1 : runs); ++i) {
        HMENU hm = CreatePopupMenu();
        Timer t;
        t.Start();
        pcm->QueryContextMenu(hm, 0, 1, 0x6FFF, CMF_NORMAL | CMF_EXPLORE);
        double ms = t.Ms();
        times.push_back(ms);
        items = GetMenuItemCount(hm);
        DestroyMenu(hm);
        body += Fmt(L"run%d=%.2f\n", i, ms);
    }
    std::sort(times.begin(), times.end());
    double median = times[times.size() / 2];
    body += Fmt(L"ms=%.2f\nmin=%.2f\nmax=%.2f\nn=%d\nitems=%d\n", median, times.front(), times.back(),
                (int)times.size(), items);
    pcm->Release();
    WriteTextFile(outFile, WideToUtf8(body));
    return 0;
}

bool ChildMain(const std::vector<std::wstring>& args, int* exitCode) {
    if (args.size() < 2) return false;
    const std::wstring& mode = args[1];
    if (mode != L"--bench-ext" && mode != L"--bench-menu") return false;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    int rc = 0;
    if (mode == L"--bench-ext" && args.size() >= 6)
        rc = ChildExt(args[2], args[3], args[4], args[5]);
    else if (mode == L"--bench-menu" && args.size() >= 6)
        rc = ChildMenu(args[2], args[3], _wtoi(args[4].c_str()), args[5]);
    else
        rc = 1;
    CoUninitialize();
    if (exitCode) *exitCode = rc;
    return true;
}

bool RunExtBenchProcess(const std::wstring& clsid, const std::wstring& kind, const std::wstring& path,
                        double* ms, int* items, std::wstring* note) {
    std::wstring outFile = TempFile(L"opx", L".tmp");
    std::wstring args = Fmt(L"--bench-ext \"%s\" %s \"%s\" \"%s\"", clsid.c_str(), kind.c_str(),
                            path.c_str(), outFile.c_str());
    bool timedOut = false;
    if (!SpawnSelf(args, 3000, note, &timedOut)) return false;
    std::string raw;
    ReadTextFile(outFile, &raw);
    DeleteFileW(outFile.c_str());
    std::wstring text = Utf8ToWide(raw);
    if (timedOut) {
        *note = L"无响应（>3s）";
        return false;
    }
    if (KV(text, L"ok") != L"1") {
        *note = KV(text, L"note");
        if (note->empty()) *note = L"不可测量";
        return false;
    }
    *ms = _wtof(KV(text, L"ms").c_str());
    *items = _wtoi(KV(text, L"items").c_str());
    if (KV(text, L"iface") == L"0") *note = L"无右键接口";
    return true;
}

bool RunMenuBenchProcess(const std::wstring& kind, const std::wstring& path, int runs, double* ms,
                         int* items, std::wstring* note, double* minMs, double* maxMs) {
    std::wstring outFile = TempFile(L"opm", L".tmp");
    std::wstring args =
        Fmt(L"--bench-menu %s \"%s\" %d \"%s\"", kind.c_str(), path.c_str(), runs, outFile.c_str());
    bool timedOut = false;
    if (!SpawnSelf(args, 20000, note, &timedOut)) return false;
    std::string raw;
    ReadTextFile(outFile, &raw);
    DeleteFileW(outFile.c_str());
    std::wstring text = Utf8ToWide(raw);
    if (timedOut) {
        *note = L"右键菜单构建超时（>20s），有扩展卡住";
        return false;
    }
    if (KV(text, L"ok") != L"1") {
        *note = KV(text, L"note");
        if (note->empty()) *note = L"测量失败";
        return false;
    }
    *ms = _wtof(KV(text, L"ms").c_str());
    *items = _wtoi(KV(text, L"items").c_str());
    if (minMs) *minMs = _wtof(KV(text, L"min").c_str());
    if (maxMs) *maxMs = _wtof(KV(text, L"max").c_str());
    return true;
}

// ---------------------------------------------------------------- 单点探针
double ShellPingMs() {
    HWND h = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!h) h = FindWindowW(L"Progman", nullptr);
    if (!h) h = GetShellWindow();
    if (!h) return -1;
    DWORD_PTR res = 0;
    Timer t;
    t.Start();
    SendMessageTimeoutW(h, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &res);
    return t.Ms();
}

struct ExplorerStatLocal {};

ExplorerStat GetExplorerStatNow() {
    ExplorerStat st;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return st;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"explorer.exe") != 0) continue;
            HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (!p) continue;
            FILETIME c, e, k, u;
            if (GetProcessTimes(p, &c, &e, &k, &u)) {
                ULARGE_INTEGER kk, uu;
                kk.LowPart = k.dwLowDateTime;
                kk.HighPart = k.dwHighDateTime;
                uu.LowPart = u.dwLowDateTime;
                uu.HighPart = u.dwHighDateTime;
                st.cpuSeconds += (double)(kk.QuadPart + uu.QuadPart) / 1e7;
            }
            PROCESS_MEMORY_COUNTERS pmc{};
            if (GetProcessMemoryInfo(p, &pmc, sizeof(pmc))) st.wsMb += pmc.WorkingSetSize / (1024.0 * 1024.0);
            st.count++;
            if (st.pid == 0) st.pid = pe.th32ProcessID;
            CloseHandle(p);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return st;
}

double ExplorerCpuPercent() { return GetExplorerStatNow().cpuSeconds; }

static bool MeasureEnum(double* ms, int* count) {
    PIDLIST_ABSOLUTE pidl = nullptr;
    std::wstring path = KnownFolder(FOLDERID_Windows);
    if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr)) || !pidl) return false;
    IShellFolder* psf = nullptr;
    HRESULT hr = SHBindToObject(nullptr, pidl, nullptr, IID_IShellFolder, (void**)&psf);
    ILFree(pidl);
    if (FAILED(hr) || !psf) return false;
    IEnumIDList* en = nullptr;
    hr = psf->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &en);
    psf->Release();
    if (FAILED(hr) || !en) return false;
    Timer t;
    t.Start();
    LPITEMIDLIST idl = nullptr;
    int n = 0;
    ULONG got = 0;
    while (n < 3000 && en->Next(1, &idl, &got) == S_OK && got == 1) {
        ++n;
        CoTaskMemFree(idl);
    }
    *ms = t.Ms();
    *count = n;
    en->Release();
    return true;
}

static bool MeasureOpenDialog(double* ms) {
    Timer t;
    t.Start();
    IFileOpenDialog* dlg = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
    if (FAILED(hr) || !dlg) return false;
    IShellItem* item = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(L"C:\\Windows", nullptr, IID_PPV_ARGS(&item))) && item) {
        dlg->SetFolder(item);
        item->Release();
    }
    IShellItem* desk = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(KnownFolder(FOLDERID_Desktop).c_str(), nullptr,
                                              IID_PPV_ARGS(&desk))) &&
        desk) {
        dlg->SetDefaultFolder(desk);
        desk->Release();
    }
    dlg->Release();
    *ms = t.Ms();
    return true;
}

static bool MeasureLaunch(double* ms) {
    wchar_t comspec[MAX_PATH] = {};
    GetEnvironmentVariableW(L"COMSPEC", comspec, MAX_PATH);
    std::wstring cmd = std::wstring(L"\"") + (comspec[0] ? comspec : L"C:\\Windows\\System32\\cmd.exe") +
                       L"\" /c exit 0";
    Timer t;
    t.Start();
    DWORD code = 1;
    bool ok = RunAndWait(cmd, 15000, &code);
    *ms = t.Ms();
    return ok;
}

static bool MeasureDwm(double* hz, double* frameMs) {
    DWM_TIMING_INFO ti{};
    ti.cbSize = sizeof(ti);
    if (FAILED(DwmGetCompositionTimingInfo(nullptr, &ti))) return false;
    if (ti.qpcRefreshPeriod > 0) *hz = 10000000.0 / (double)ti.qpcRefreshPeriod;
    Timer t;
    t.Start();
    int frames = 0;
    for (int i = 0; i < 30; ++i) {
        if (SUCCEEDED(DwmFlush())) ++frames;
    }
    *frameMs = frames > 1 ? t.Ms() / frames : t.Ms();
    return true;
}

// ---------------------------------------------------------------- 汇总
static int Lvl(double v, double good, double ok, double bad, bool inverse = false) {
    if (inverse) v = -v;
    if (v <= good) return 0;
    if (v <= ok) return 1;
    if (v <= bad) return 2;
    return 3;
}

static const wchar_t* LevelText(int l) {
    switch (l) {
        case 0: return L"优秀";
        case 1: return L"良好";
        case 2: return L"偏慢";
        default: return L"很慢";
    }
}

static void Add(std::vector<Metric>* v, const wchar_t* key, const std::wstring& name,
                const std::wstring& value, int level, const wchar_t* advice) {
    Metric m;
    m.key = key;
    m.name = name;
    m.value = value;
    m.level = level;
    m.verdict = LevelText(level);
    m.advice = advice ? advice : L"";
    v->push_back(m);
}

bool RunFull(Report* r, std::vector<ShellExt>* exts, ProgressFn progress) {
    *r = Report();
    SYSTEMTIME st;
    GetLocalTime(&st);
    r->stamp = Fmt(L"%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                   st.wSecond);
    r->os = OsBuildString();
    r->cpu = CpuName();
    r->ramMb = TotalRamMb();
    r->touch = IsTouchPresent();
    r->admin = IsAdmin();

    if (progress) progress(L"测量资源管理器响应…", 5);
    r->shellPingMs = ShellPingMs();
    int l = r->shellPingMs < 0 ? 3 : Lvl(r->shellPingMs, 10, 50, 250);
    Add(&r->metrics, L"shell_ping", L"资源管理器响应（消息往返）",
        r->shellPingMs < 0 ? L"无响应" : Fmt(L"%.1f ms", r->shellPingMs), l,
        l >= 2 ? L"资源管理器主线程繁忙，右键/打开都会被连带拖慢" : L"正常");

    if (progress) progress(L"测量右键菜单构建耗时…", 15);
    std::wstring sample = DataDir() + L"\\bench_sample.txt";
    if (!FileExists(sample)) WriteTextFile(sample, "openpuls bench sample\n");
    std::wstring winDir = KnownFolder(FOLDERID_Windows);
    std::wstring note;
    int items = 0;
    double mn = 0, mx = 0;
    if (RunMenuBenchProcess(L"file", sample, 4, &r->ctxFileMs, &items, &note, &mn, &mx)) {
        r->itemsFile = items;
        l = Lvl(r->ctxFileMs, 50, 120, 250);
        std::wstring val = Fmt(L"%.0f ms / %d 项", r->ctxFileMs, items);
        if (mx > mn * 1.5 + 30) val += Fmt(L"（%0.f–%.0f 波动）", mn, mx);
        Add(&r->metrics, L"ctx_file", L"右键菜单构建（普通文件）", val, l,
            l >= 2 ? L"扩展太多或有扩展在首次加载时阻塞" : L"正常");
    } else {
        Add(&r->metrics, L"ctx_file", L"右键菜单构建（普通文件）", note, 3, L"测量失败");
    }
    if (RunMenuBenchProcess(L"dir", winDir, 2, &r->ctxDirMs, &items, &note)) {
        r->itemsDir = items;
        l = Lvl(r->ctxDirMs, 60, 140, 300);
        Add(&r->metrics, L"ctx_dir", L"右键菜单构建（文件夹）", Fmt(L"%.0f ms / %d 项", r->ctxDirMs, items),
            l, l >= 2 ? L"文件夹类扩展较多" : L"正常");
    }
    if (RunMenuBenchProcess(L"bg", winDir, 2, &r->ctxBgMs, &items, &note)) {
        l = Lvl(r->ctxBgMs, 40, 100, 220);
        Add(&r->metrics, L"ctx_bg", L"右键菜单构建（空白处）", Fmt(L"%.0f ms", r->ctxBgMs), l, L"");
    }

    if (progress) progress(L"逐个测试右键扩展…", 30);
    std::vector<ShellExt> list = TweakEngine().EnumerateShellExts();
    TweakEngine eng;
    r->extTotal = (int)list.size();
    for (auto& e : list) {
        if (e.blocked) {
            ++r->extBlocked;
            continue;
        }
        if (!e.system) ++r->extThirdParty;
    }
    int done = 0;
    for (auto& e : list) {
        if (e.blocked) continue;
        eng.RunExtBench(&e);
        if (e.benchMs >= 50) ++r->slowExtCount;
        ++done;
        if (progress && (done % 3 == 0 || done == (int)list.size()))
            progress(Fmt(L"逐个测试右键扩展… %d/%d", done, (int)list.size()),
                     30 + (int)(25.0 * done / (list.size() + 1)));
    }
    std::stable_sort(list.begin(), list.end(), [](const ShellExt& a, const ShellExt& b) {
        return a.benchMs > b.benchMs;
    });
    if (exts) *exts = list;

    if (progress) progress(L"测量打开对话框与目录枚举…", 62);
    if (MeasureOpenDialog(&r->openDialogMs)) {
        l = Lvl(r->openDialogMs, 40, 100, 250);
        Add(&r->metrics, L"open_dialog", L"打开文件对话框初始化", Fmt(L"%.0f ms", r->openDialogMs), l,
            l >= 2 ? L"外壳名称空间扩展或云盘钩子较重" : L"正常");
    }
    int cnt = 0;
    if (MeasureEnum(&r->enumMs, &cnt)) {
        l = Lvl(r->enumMs, 150, 400, 1000);
        Add(&r->metrics, L"enum", L"目录枚举（系统目录，" + std::to_wstring(cnt) + L" 项）",
            Fmt(L"%.0f ms", r->enumMs), l, l >= 2 ? L"图标叠加/属性扩展拖慢列表加载" : L"正常");
    }
    if (progress) progress(L"测量程序启动与合成器…", 78);
    if (MeasureLaunch(&r->launchMs)) {
        l = Lvl(r->launchMs, 80, 200, 450);
        Add(&r->metrics, L"launch", L"启动一个控制台进程（含外壳）", Fmt(L"%.0f ms", r->launchMs), l,
            l >= 2 ? L"启动前扫描或外壳激活链路较长" : L"正常");
    }
    if (MeasureDwm(&r->refreshHz, &r->frameMs)) {
        double expect = r->refreshHz > 1 ? 1000.0 / r->refreshHz : 16.7;
        l = r->frameMs < expect * 1.6 ? 0 : (r->frameMs < expect * 3 ? 2 : 3);
        Add(&r->metrics, L"dwm", L"桌面合成帧间隔",
            Fmt(L"%.1f ms（%.0f Hz，理想 %.1f ms）", r->frameMs, r->refreshHz, expect), l,
            l >= 2 ? L"合成器跟不上，动画与触摸会有拖影感" : L"正常");
    }
    l = r->extTotal <= 8 ? 0 : (r->extTotal <= 16 ? 1 : (r->extTotal <= 28 ? 2 : 3));
    Add(&r->metrics, L"ext_count", L"已注册右键扩展",
        Fmt(L"%d 个（第三方 %d，已屏蔽 %d）", r->extTotal, r->extThirdParty, r->extBlocked), l,
        l >= 2 ? L"扩展数量偏多，逐项测试后屏蔽不必要的项" : L"数量正常");
    Add(&r->metrics, L"touch", L"触摸设备", r->touch ? L"存在" : L"未检测到", r->touch ? 0 : 1, L"");

    // 建议
    if (r->ctxFileMs > 120) {
        std::wstring top;
        int n = 0;
        for (auto& e : list) {
            if (e.benchMs < 5) continue;
            if (n >= 3) break;
            top += (n ? L"、" : L"") + e.name + Fmt(L"(%.0fms)", e.benchMs);
            ++n;
        }
        r->recommendations.push_back(Fmt(L"右键菜单构建耗时 %.0f ms，最慢的扩展：%s，建议在「右键菜单」页屏蔽。",
                                         r->ctxFileMs, top.c_str()));
    }
    if (r->slowExtCount > 0)
        r->recommendations.push_back(
            Fmt(L"有 %d 个扩展单项耗时超过 50ms，它们会拖慢每一次右键。", r->slowExtCount));
    if (r->shellPingMs > 100)
        r->recommendations.push_back(L"资源管理器自身响应超过 100ms，先关闭占用较高的程序再复测；必要时重启资源管理器。");
    if (r->extTotal > 20) r->recommendations.push_back(L"右键扩展数量超过 20 个，建议只保留常用的几项。");
    if (r->launchMs > 200) r->recommendations.push_back(L"进程启动链路偏慢，可开启「高性能电源计划」并检查安全软件实时扫描。");
    if (!r->admin) r->recommendations.push_back(L"当前非管理员运行，涉及系统级的优化项（电源/优先级）不可用。");
    return true;
}

std::wstring ToMarkdown(const Report& r, const std::vector<ShellExt>& exts) {
    std::wstring md;
    md += L"# OpenPuls 响应优化器 · 体检报告\r\n\r\n";
    md += L"- 时间：" + r.stamp + L"\r\n";
    md += L"- 系统：Windows " + r.os + L"\r\n";
    md += L"- 处理器：" + r.cpu + L"\r\n";
    md += L"- 内存：" + std::to_wstring(r.ramMb) + L" MB\r\n";
    md += L"- 权限：" + std::wstring(r.admin ? L"管理员" : L"普通用户") + L"\r\n\r\n";
    md += L"## 指标\r\n\r\n| 项目 | 实测 | 评价 |\r\n| --- | --- | --- |\r\n";
    for (auto& m : r.metrics)
        md += L"| " + m.name + L" | " + m.value + L" | " + m.verdict + L" |\r\n";
    md += L"\r\n## 右键扩展耗时（单项，独立进程测量）\r\n\r\n| 扩展 | 类型 | 耗时 | 状态 | 模块 |\r\n| --- | --- | --- | --- | --- |\r\n";
    for (auto& e : exts) {
        std::wstring t = e.benchMs >= 0 ? Fmt(L"%.1f ms", e.benchMs) : (e.blocked ? L"—" : e.benchNote);
        md += L"| " + e.name + L" | " + e.kinds + L" | " + t + L" | " + (e.blocked ? L"已屏蔽" : L"启用") +
              L" | " + e.module + L" |\r\n";
    }
    md += L"\r\n## 建议\r\n\r\n";
    for (auto& s : r.recommendations) md += L"- " + s + L"\r\n";
    if (r.recommendations.empty()) md += L"- 未发现明显瓶颈，保持当前配置即可。\r\n";
    md += L"\r\n> 右键耗时用 IContextMenu::QueryContextMenu 在独立子进程中测量，"
          L"反映各扩展加载与建菜单的真实成本；Windows 11 新版菜单本身由 explorer 的 XAML 宿主绘制，"
          L"无法脱离交互进程测量，因此该数值代表「菜单里所有旧式扩展」这一共同成本。\r\n";
    return md;
}

}  // namespace diag
}  // namespace op
