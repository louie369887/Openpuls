#include "tweaks.h"
#include "diag.h"

#include <shlobj.h>
#include <powrprof.h>
#include <vector>

namespace op {

static const wchar_t* kApproved = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved";
static const wchar_t* kBlocked = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Blocked";
static const wchar_t* kAdv = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
static const wchar_t* kDesktop = L"Control Panel\\Desktop";
static const wchar_t* kWinMetrics = L"Control Panel\\Desktop\\WindowMetrics";
static const wchar_t* kTouch = L"Software\\Microsoft\\Wisp\\Touch";
static const wchar_t* kBackupRoot = L"Software\\OpenPuls\\ShellOpt\\Backup\\";
static const wchar_t* kOffSuffix = L"__OpenPulsOff";

// ---------------------------------------------------------------- 备份读写
struct RawVal {
    bool exists = false;
    DWORD type = 0;
    std::wstring sz;
    DWORD dw = 0;
};

static RawVal ReadRaw(const RegTarget& t) {
    RawVal v;
    HKEY k = nullptr;
    if (RegOpenKeyExW(t.root, t.sub.c_str(), 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return v;
    DWORD type = 0, cb = 0;
    LSTATUS st = RegQueryValueExW(k, t.name.c_str(), nullptr, &type, nullptr, &cb);
    if (st != ERROR_SUCCESS) {
        RegCloseKey(k);
        return v;
    }
    std::vector<BYTE> buf(cb + sizeof(wchar_t) * 2, 0);
    cb = (DWORD)buf.size();
    st = RegQueryValueExW(k, t.name.c_str(), nullptr, &type, buf.data(), &cb);
    RegCloseKey(k);
    if (st != ERROR_SUCCESS) return v;
    v.exists = true;
    v.type = type;
    if (type == REG_SZ || type == REG_EXPAND_SZ) {
        size_t chars = cb / sizeof(wchar_t);
        while (chars > 0 && ((wchar_t*)buf.data())[chars - 1] == 0) --chars;
        v.sz.assign((const wchar_t*)buf.data(), chars);
    } else if (type == REG_DWORD) {
        if (cb >= 4) v.dw = *(DWORD*)buf.data();
    }
    return v;
}

static bool MatchTarget(const RegTarget& t, const RawVal& v) {
    if (!v.exists) return false;
    if (t.emptyStr) return v.type == REG_SZ && v.sz.empty();
    if (t.dword) return v.type == REG_DWORD && v.dw == t.dw;
    return (v.type == REG_SZ || v.type == REG_EXPAND_SZ) && _wcsicmp(v.sz.c_str(), t.sz.c_str()) == 0;
}

static bool WriteTarget(const RegTarget& t) {
    if (t.emptyStr) return RegSetEmpty(t.root, t.sub, t.name);
    if (t.dword) return RegSetDword(t.root, t.sub, t.name, t.dw);
    return RegSetStr(t.root, t.sub, t.name, t.sz);
}

static std::wstring BoolPath(const std::wstring& id) { return std::wstring(kBackupRoot) + id; }

// ---------------------------------------------------------------- 优化项表
void TweakEngine::BuildTable() {
    tweaks_.clear();

    {   // 经典右键菜单
        TweakDef t;
        t.id = L"classic_menu";
        t.group = TGroup::Menu;
        t.title = L"经典右键菜单（Windows 10 风格）";
        t.desc = L"Win11 新菜单要先启动 XAML 宿主、再向每个打包应用查询命令，首次展开常要数百毫秒；改用旧菜单可省掉这一整层。";
        t.warn = L"需要重启资源管理器才生效。";
        t.needsExplorer = true;
        RegTarget a;
        a.root = HKEY_CURRENT_USER;
        a.sub = L"Software\\Classes\\CLSID\\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}\\InprocServer32";
        a.name = L"";
        a.emptyStr = true;
        t.targets = {a};
        t.deleteOnRevert = L"Software\\Classes\\CLSID\\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}";
        tweaks_.push_back(t);
    }
    {   // 菜单延迟
        TweakDef t;
        t.id = L"menu_delay";
        t.group = TGroup::Menu;
        t.title = L"取消菜单展开延迟（400ms 降到 0）";
        t.desc = L"系统默认在菜单弹出前等待 400 毫秒，判断用户是否要点击别处；把该等待清零后，右键与子菜单跟手即出。";
        RegTarget a;
        a.root = HKEY_CURRENT_USER;
        a.sub = kDesktop;
        a.name = L"MenuShowDelay";
        a.sz = L"0";
        t.targets = {a};
        tweaks_.push_back(t);
    }
    {   // 打开「此电脑」
        TweakDef t;
        t.id = L"open_thispc";
        t.group = TGroup::Launch;
        t.title = L"资源管理器直接打开「此电脑」";
        t.desc = L"跳过「主页」聚合页：它每次都要汇总最近文件、常用文件夹和云端内容，是窗口打开慢的主因之一。";
        RegTarget a;
        a.root = HKEY_CURRENT_USER;
        a.sub = kAdv;
        a.name = L"LaunchTo";
        a.dword = true;
        a.dw = 1;
        t.targets = {a};
        tweaks_.push_back(t);
    }
    {   // 最近使用记录
        TweakDef t;
        t.id = L"no_recent_track";
        t.group = TGroup::Launch;
        t.title = L"停止跟踪最近使用的文件与程序";
        t.desc = L"关闭 Jump List 与快速访问的最近记录统计，减少打开文件对话框和开始菜单时的磁盘与注册表查询。";
        RegTarget a;
        a.root = HKEY_CURRENT_USER;
        a.sub = kAdv;
        a.name = L"Start_TrackDocs";
        a.dword = true;
        RegTarget b = a;
        b.name = L"Start_TrackProgs";
        t.targets = {a, b};
        tweaks_.push_back(t);
    }
    {   // 开始菜单网络搜索
        TweakDef t;
        t.id = L"start_no_web";
        t.group = TGroup::Launch;
        t.title = L"开始菜单只搜本机（关闭联网联想）";
        t.desc = L"每次打开开始菜单都向后端请求搜索建议与网页结果，断网或网络抖动时会明显卡顿；关掉后响应更稳定。";
        RegTarget a;
        a.root = HKEY_CURRENT_USER;
        a.sub = L"Software\\Microsoft\\Windows\\CurrentVersion\\Search";
        a.name = L"BingSearchEnabled";
        a.dword = true;
        RegTarget b;
        b.root = HKEY_CURRENT_USER;
        b.sub = L"Software\\Policies\\Microsoft\\Windows\\Explorer";
        b.name = L"DisableSearchBoxSuggestions";
        b.dword = true;
        b.dw = 1;
        t.targets = {a, b};
        tweaks_.push_back(t);
    }
    {   // 同步提供程序通知
        TweakDef t;
        t.id = L"no_sync_notify";
        t.group = TGroup::Launch;
        t.title = L"关闭网盘与同步引擎的状态叠加";
        t.desc = L"OneDrive 等同步程序的资源管理器扩展会参与每次窗口打开与右键查询，关闭其通知叠加可减轻卡顿。";
        RegTarget a;
        a.root = HKEY_CURRENT_USER;
        a.sub = kAdv;
        a.name = L"ShowSyncProviderNotifications";
        a.dword = true;
        t.targets = {a};
        tweaks_.push_back(t);
    }
    {   // 动画
        TweakDef t;
        t.id = L"no_anim";
        t.group = TGroup::Launch;
        t.title = L"关闭窗口最小化与任务栏动画";
        t.desc = L"动画本身不慢，但它把每一次打开的观感延后了 150 到 200 毫秒；关掉后窗口瞬间出现，对老机器尤其明显。";
        RegTarget a;
        a.root = HKEY_CURRENT_USER;
        a.sub = kWinMetrics;
        a.name = L"MinAnimate";
        a.sz = L"0";
        RegTarget b;
        b.root = HKEY_CURRENT_USER;
        b.sub = kAdv;
        b.name = L"TaskbarAnimations";
        b.dword = true;
        t.targets = {a, b};
        tweaks_.push_back(t);
    }
    {   // 高性能电源计划
        TweakDef t;
        t.id = L"power_plan";
        t.group = TGroup::Launch;
        t.title = L"切换到高性能电源计划";
        t.desc = L"平衡计划会在负载波动时压低频率，右键、菜单这类短小的前台操作最容易撞上降频窗口。";
        t.admin = true;
        tweaks_.push_back(t);
    }
    {   // 电源节流
        TweakDef t;
        t.id = L"power_throttle_off";
        t.group = TGroup::Launch;
        t.title = L"关闭电源节流（Power Throttling）";
        t.desc = L"Win11 会把非前台进程限制在低功耗状态；资源管理器、搜索宿主被节流时，右键与窗口打开就会排队等待。";
        t.admin = true;
        RegTarget a;
        a.root = HKEY_LOCAL_MACHINE;
        a.sub = L"SYSTEM\\CurrentControlSet\\Control\\Power\\PowerThrottling";
        a.name = L"PowerThrottlingOff";
        a.dword = true;
        a.dw = 1;
        t.targets = {a};
        tweaks_.push_back(t);
    }
    {   // 前台优先
        TweakDef t;
        t.id = L"prio_foreground";
        t.group = TGroup::Launch;
        t.title = L"前台程序优先（缩短前台调度时片）";
        t.desc = L"Win32PrioritySeparation 设为 38，让被点击的窗口更快拿到 CPU 时间片，交互卡顿感下降。";
        t.admin = true;
        t.warn = L"会让后台任务（压缩、渲染、下载）稍慢。";
        RegTarget a;
        a.root = HKEY_LOCAL_MACHINE;
        a.sub = L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl";
        a.name = L"Win32PrioritySeparation";
        a.dword = true;
        a.dw = 38;
        t.targets = {a};
        tweaks_.push_back(t);
    }
    {   // 前台锁定
        TweakDef t;
        t.id = L"fg_lock_off";
        t.group = TGroup::Advanced;
        t.title = L"取消前台锁定超时（200 秒降到 0）";
        t.desc = L"新窗口默认要等待 200 秒的抢占保护才能到前台；清零后打开的程序立刻到最前，但可能出现抢焦点。";
        t.advanced = true;
        RegTarget a;
        a.root = HKEY_CURRENT_USER;
        a.sub = kDesktop;
        a.name = L"ForegroundLockTimeout";
        a.dword = true;
        a.dw = 0;
        t.targets = {a};
        tweaks_.push_back(t);
    }
    {   // 触摸长按
        TweakDef t;
        t.id = L"touch_hold_fast";
        t.group = TGroup::Touch;
        t.title = L"长按判定加速（按住到右键更快）";
        t.desc = L"缩短触摸按住到弹出菜单前的判定与动画等待，让长按右键更快响应；属于实验项，已备份可一键还原。";
        t.warn = L"实验项：部分触摸屏驱动可能会把长按识别成点击。";
        RegTarget a;
        a.root = HKEY_CURRENT_USER;
        a.sub = kTouch;
        a.name = L"TouchModeN_HoldTime_BeforeAnimation";
        a.dword = true;
        a.dw = 15;
        RegTarget b = a;
        b.name = L"TouchModeN_HoldTime_Animation";
        t.targets = {a, b};
        tweaks_.push_back(t);
    }
    {   // 触摸惯性
        TweakDef t;
        t.id = L"touch_inertia_off";
        t.group = TGroup::Touch;
        t.title = L"关闭触控惯性滚动与回弹";
        t.desc = L"手指抬起后内容继续滑动会让你觉得系统慢半拍；关掉惯性与回弹后，滑动立刻停住，跟手感更强。";
        RegTarget a;
        a.root = HKEY_CURRENT_USER;
        a.sub = kTouch;
        a.name = L"Inertia";
        a.dword = true;
        RegTarget b = a;
        b.name = L"Bouncing";
        t.targets = {a, b};
        tweaks_.push_back(t);
    }
    {   // flicks
        TweakDef t;
        t.id = L"touch_flicks_off";
        t.group = TGroup::Touch;
        t.title = L"关闭触摸手势（Flicks）识别";
        t.desc = L"每一下触摸都要先经过手势识别器判断是不是划动手势，关掉后点按响应更直接。";
        RegTarget a;
        a.root = HKEY_CURRENT_USER;
        a.sub = kTouch;
        a.name = L"Flicks";
        a.dword = true;
        t.targets = {a};
        tweaks_.push_back(t);
    }

    bool touch = IsTouchPresent();
    for (auto& t : tweaks_)
        if (t.group == TGroup::Touch) t.supported = touch;
}

void TweakEngine::Init() { BuildTable(); }

const TweakDef* TweakEngine::Find(const std::wstring& id) const {
    for (auto& t : tweaks_)
        if (t.id == id) return &t;
    return nullptr;
}

bool TweakEngine::IsWanted(const std::wstring& id) const {
    return SettingsGetInt(L"want." + id, 0) != 0;
}

void TweakEngine::SetWanted(const std::wstring& id, bool on) {
    SettingsSetInt(L"want." + id, on ? 1 : 0);
}

bool TweakEngine::HasBackup(const std::wstring& id) const {
    return RegKeyExists(HKEY_CURRENT_USER, BoolPath(id));
}

// ---------------------------------------------------------------- 探针
TState TweakEngine::Probe(const TweakDef& t) const {
    if (t.id == L"power_plan") return IsHighPerformancePlan() ? TState::Applied : TState::Default;
    if (t.targets.empty()) return TState::Unknown;
    int match = 0;
    for (auto& tg : t.targets)
        if (MatchTarget(tg, ReadRaw(tg))) ++match;
    if (match == (int)t.targets.size()) return TState::Applied;
    if (match == 0) return TState::Default;
    return TState::Partial;
}

// ---------------------------------------------------------------- 备份
bool TweakEngine::BackupOriginals(const TweakDef& t) {
    if (HasBackup(t.id)) return true;
    std::wstring path = BoolPath(t.id);
    if (!RegSetDword(HKEY_CURRENT_USER, path, L"n", (DWORD)t.targets.size())) return false;
    if (!t.deleteOnRevert.empty())
        RegSetDword(HKEY_CURRENT_USER, path, L"keyexisted",
                    RegKeyExists(HKEY_CURRENT_USER, t.deleteOnRevert) ? 1 : 0);
    for (size_t i = 0; i < t.targets.size(); ++i) {
        RawVal v = ReadRaw(t.targets[i]);
        std::wstring idx = std::to_wstring(i);
        RegSetDword(HKEY_CURRENT_USER, path, L"e" + idx, v.exists ? 1 : 0);
        RegSetDword(HKEY_CURRENT_USER, path, L"t" + idx, v.type);
        if (v.type == REG_DWORD)
            RegSetStr(HKEY_CURRENT_USER, path, L"s" + idx, std::to_wstring(v.dw));
        else
            RegSetStr(HKEY_CURRENT_USER, path, L"s" + idx, v.sz);
    }
    return true;
}

bool TweakEngine::RestoreOriginals(const TweakDef& t) {
    std::wstring path = BoolPath(t.id);
    bool hadBackup = RegKeyExists(HKEY_CURRENT_USER, path);
    DWORD keyExisted = 1;
    if (hadBackup && !t.deleteOnRevert.empty())
        RegGetDword(HKEY_CURRENT_USER, path, L"keyexisted", &keyExisted);
    DWORD n = 0;
    if (RegGetDword(HKEY_CURRENT_USER, path, L"n", &n)) {
        for (DWORD i = 0; i < n && i < t.targets.size(); ++i) {
            std::wstring idx = std::to_wstring(i);
            DWORD existed = 0, type = 0;
            RegGetDword(HKEY_CURRENT_USER, path, L"e" + idx, &existed);
            RegGetDword(HKEY_CURRENT_USER, path, L"t" + idx, &type);
            std::wstring sv;
            RegGetStr(HKEY_CURRENT_USER, path, L"s" + idx, &sv);
            if (!existed) {
                RegDelValue(t.targets[i].root, t.targets[i].sub, t.targets[i].name);
            } else if (type == REG_DWORD) {
                RegSetDword(t.targets[i].root, t.targets[i].sub, t.targets[i].name,
                            (DWORD)_wtoi(sv.c_str()));
            } else {
                RegSetStr(t.targets[i].root, t.targets[i].sub, t.targets[i].name, sv);
            }
        }
    }
    RegDelKeyTree(HKEY_CURRENT_USER, path);
    // 只有写入前该键不存在时才整体删除，避免破坏用户原本的经典菜单设置
    if (!t.deleteOnRevert.empty() && hadBackup && keyExisted == 0)
        RegDelKeyTree(HKEY_CURRENT_USER, t.deleteOnRevert);
    return true;
}

// ---------------------------------------------------------------- 应用
bool TweakEngine::SetEnabled(const TweakDef& t, bool on, std::wstring* err) {
    if (t.admin && !IsAdmin()) {
        if (err) *err = L"该项需要管理员权限";
        return false;
    }
    if (t.id == L"power_plan") return SetHighPerformancePlan(on, err);

    if (on) {
        if (!BackupOriginals(t)) {
            if (err) *err = L"无法写入备份";
            return false;
        }
        for (auto& tg : t.targets) {
            if (!WriteTarget(tg)) {
                if (err) *err = L"写入注册表失败：" + tg.sub;
                return false;
            }
        }
        if (t.id == L"menu_delay")
            SystemParametersInfoW(SPI_SETMENUSHOWDELAY, 0, nullptr, SPIF_SENDCHANGE);
        if (t.id == L"no_anim") SystemParametersInfoW(SPI_SETANIMATION, 0, nullptr, SPIF_SENDCHANGE);
    } else {
        RestoreOriginals(t);
        if (t.id == L"menu_delay")
            SystemParametersInfoW(SPI_SETMENUSHOWDELAY, 400, nullptr, SPIF_SENDCHANGE);
    }
    SetWanted(t.id, on);
    Log(L"优化项 %s -> %s", t.id.c_str(), on ? L"开启" : L"关闭");
    return true;
}

bool TweakEngine::RestoreTweak(const TweakDef& t, std::wstring* err) {
    if (t.admin && !IsAdmin()) {
        if (err) *err = L"该项需要管理员权限";
        return false;
    }
    if (t.id == L"power_plan") return SetHighPerformancePlan(false, err);
    RestoreOriginals(t);
    SetWanted(t.id, false);
    return true;
}

int TweakEngine::RestoreAll(std::wstring* err) {
    int n = 0;
    for (auto& t : tweaks_) {
        if (HasBackup(t.id) || IsWanted(t.id) || Probe(t) == TState::Applied) {
            if (RestoreTweak(t, err)) ++n;
        }
    }
    return n;
}

int TweakEngine::EnforceWanted(std::wstring* err) {
    int applied = 0;
    for (auto& t : tweaks_) {
        if (!t.supported || !IsWanted(t.id)) continue;
        if (t.admin && !IsAdmin()) continue;
        if (Probe(t) == TState::Applied) continue;
        if (SetEnabled(t, true, err)) ++applied;
    }
    return applied;
}

// ---------------------------------------------------------------- 电源
bool TweakEngine::IsHighPerformancePlan() const {
    GUID* g = nullptr;
    bool high = false;
    if (PowerGetActiveScheme(nullptr, &g) == ERROR_SUCCESS && g) {
        high = IsEqualGUID(*g, GUID_MIN_POWER_SAVINGS) != FALSE;
        LocalFree(g);
    }
    return high;
}

bool TweakEngine::SetHighPerformancePlan(bool on, std::wstring* err) {
    const GUID& target = on ? GUID_MIN_POWER_SAVINGS : GUID_TYPICAL_POWER_SAVINGS;
    DWORD rc = PowerSetActiveScheme(nullptr, &target);
    if (rc != ERROR_SUCCESS) {
        DWORD code = 1;
        RunAndWait(on ? L"powercfg.exe /setactive SCHEME_MIN"
                      : L"powercfg.exe /setactive SCHEME_BALANCED",
                   15000, &code);
        if (code != 0) {
            if (err) *err = Fmt(L"切换电源计划失败（0x%08X）", (unsigned)rc);
            return false;
        }
    }
    SetWanted(L"power_plan", on);
    Log(L"电源计划 -> %s", on ? L"高性能" : L"平衡");
    return true;
}

bool TweakEngine::BuildIconCache(std::wstring* err) {
    RestartExplorer();
    std::wstring local = KnownFolder(FOLDERID_LocalAppData);
    DeleteFileW((local + L"\\IconCache.db").c_str());
    std::wstring exp = local + L"\\Microsoft\\Windows\\Explorer";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((exp + L"\\*cache*.db").c_str(), &fd);
    int removed = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (DeleteFileW((exp + L"\\" + fd.cFileName).c_str())) ++removed;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    Log(L"已重建图标/缩略图缓存，删除 %d 个文件", removed);
    Sleep(400);
    RestartExplorer();
    if (err) *err = Fmt(L"已清理 %d 个缓存文件并重启资源管理器", removed);
    return true;
}

// ---------------------------------------------------------------- 外壳扩展
struct HandlerKey {
    const wchar_t* path;
    const wchar_t* label;
    const wchar_t* kind;
};

static const HandlerKey kHandlerKeys[] = {
    {L"Software\\Classes\\*\\shellex\\ContextMenuHandlers", L"所有文件", L"file"},
    {L"Software\\Classes\\Directory\\shellex\\ContextMenuHandlers", L"文件夹", L"dir"},
    {L"Software\\Classes\\Directory\\Background\\shellex\\ContextMenuHandlers", L"空白处", L"bg"},
    {L"Software\\Classes\\Folder\\shellex\\ContextMenuHandlers", L"文件夹", L"dir"},
    {L"Software\\Classes\\AllFilesystemObjects\\shellex\\ContextMenuHandlers", L"所有对象", L"file"},
    {L"Software\\Classes\\Drive\\shellex\\ContextMenuHandlers", L"驱动器", L"drive"},
    {L"Software\\Classes\\*\\shellex\\PropertySheetHandlers", L"属性页", L"prop"},
    {L"Software\\Classes\\Directory\\shellex\\DragDropHandlers", L"拖放", L"drop"},
};

static bool IsGuid(const std::wstring& s) {
    return s.size() == 38 && s.front() == L'{' && s.back() == L'}';
}

std::wstring ResolveExtName(const std::wstring& clsid) {
    std::wstring v;
    if (RegGetStr(HKEY_LOCAL_MACHINE, kApproved, clsid, &v) && !v.empty()) return v;
    if (RegGetStr(HKEY_CURRENT_USER, kApproved, clsid, &v) && !v.empty()) return v;
    if (RegGetStr(HKEY_CLASSES_ROOT, L"CLSID\\" + clsid, L"", &v) && !v.empty()) return v;
    return L"";
}

std::wstring ResolveExtModule(const std::wstring& clsid) {
    std::wstring v;
    RegGetStr(HKEY_CLASSES_ROOT, L"CLSID\\" + clsid + L"\\InprocServer32", L"", &v);
    if (v.empty())
        RegGetStr(HKEY_CLASSES_ROOT, L"CLSID\\" + clsid + L"\\InprocServer32", L"Assembly", &v);
    wchar_t ex[1024];
    if (!v.empty() && ExpandEnvironmentStringsW(v.c_str(), ex, 1024)) return ex;
    return v;
}

std::vector<ShellExt> TweakEngine::EnumerateShellExts() const {
    std::vector<ShellExt> out;
    HKEY roots[2] = {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER};
    size_t sfx = wcslen(kOffSuffix);
    for (HKEY root : roots) {
        for (auto& hk : kHandlerKeys) {
            std::vector<std::wstring> subs;
            if (!RegEnumSubKeys(root, hk.path, &subs)) continue;
            for (auto& rawName : subs) {
                bool off = rawName.size() > sfx &&
                           _wcsicmp(rawName.c_str() + rawName.size() - sfx, kOffSuffix) == 0;
                std::wstring name = off ? rawName.substr(0, rawName.size() - sfx) : rawName;
                std::wstring clsid;
                std::wstring sub = std::wstring(hk.path) + L"\\" + rawName;
                RegGetStr(root, sub, L"", &clsid);
                if (clsid.empty()) RegGetStr(root, sub, L"CLSID", &clsid);
                if (!IsGuid(clsid)) continue;
                ShellExt* e = FindExt(out, clsid);
                if (!e) {
                    ShellExt ne;
                    ne.clsid = clsid;
                    e = &out.emplace_back(ne);
                    e->name = ResolveExtName(clsid);
                    if (e->name.empty()) e->name = Trim(name);
                    e->module = ResolveExtModule(clsid);
                    std::wstring windir = KnownFolder(FOLDERID_Windows);
                    e->system = e->module.empty() ||
                                _wcsnicmp(e->module.c_str(), windir.c_str(), windir.size()) == 0;
                    e->origin = HkeyName(root);
                }
                e->regs.push_back(HkeyName(root) + L"|" + hk.path + L"|" + name);
                if (e->kinds.find(hk.label) == std::wstring::npos) {
                    if (!e->kinds.empty()) e->kinds += L" / ";
                    e->kinds += hk.label;
                }
                if (e->kindKey.empty() || _wcsicmp(hk.kind, L"file") == 0) e->kindKey = hk.kind;
                if (off) e->blocked = true;
            }
        }
    }
    for (auto& e : out) {
        std::wstring v;
        if (RegGetStr(HKEY_CURRENT_USER, kBlocked, e.clsid, &v) ||
            RegGetStr(HKEY_LOCAL_MACHINE, kBlocked, e.clsid, &v))
            e.blocked = true;
    }
    return out;
}

ShellExt* FindExt(std::vector<ShellExt>& v, const std::wstring& clsid) {
    for (auto& e : v)
        if (_wcsicmp(e.clsid.c_str(), clsid.c_str()) == 0) return &e;
    return nullptr;
}

static bool SplitReg(const std::wstring& r, std::wstring* root, std::wstring* path,
                     std::wstring* name) {
    size_t a = r.find(L'|');
    if (a == std::wstring::npos) return false;
    size_t b = r.find(L'|', a + 1);
    if (b == std::wstring::npos) return false;
    *root = r.substr(0, a);
    *path = r.substr(a + 1, b - a - 1);
    *name = r.substr(b + 1);
    return true;
}

static HKEY RootOf(const std::wstring& n) {
    return _wcsicmp(n.c_str(), L"HKCU") == 0 ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
}

bool TweakEngine::SetExtBlocked(const ShellExt& ext, bool blocked, std::wstring* err) {
    bool ok = true;
    if (blocked) {
        // 1) 标准屏蔽列表：写入 Blocked，多数系统上 explorer 会跳过该 CLSID
        RegSetStr(HKEY_CURRENT_USER, kBlocked, ext.clsid, ext.name);
        // 2) 再把本机注册项改名（内容不变、可无损还原），保证菜单里真的消失
        for (auto& r : ext.regs) {
            std::wstring rootName, path, name;
            if (!SplitReg(r, &rootName, &path, &name)) continue;
            HKEY root = RootOf(rootName);
            if (root == HKEY_LOCAL_MACHINE && !IsAdmin()) continue;
            std::wstring full = path + L"\\" + name;
            if (!RegKeyExists(root, full)) continue;
            LSTATUS st = RegRenameKey(root, full.c_str(), (name + kOffSuffix).c_str());
            if (st != ERROR_SUCCESS) {
                ok = false;
                Log(L"改名注册项失败 %s (0x%08X)", full.c_str(), (unsigned)st);
            }
        }
    } else {
        RegDelValue(HKEY_CURRENT_USER, kBlocked, ext.clsid);
        RegDelValue(HKEY_LOCAL_MACHINE, kBlocked, ext.clsid);
        for (auto& hk : kHandlerKeys) {
            for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
                if (root == HKEY_LOCAL_MACHINE && !IsAdmin()) continue;
                std::vector<std::wstring> subs;
                if (!RegEnumSubKeys(root, hk.path, &subs)) continue;
                size_t n = wcslen(kOffSuffix);
                for (auto& s : subs) {
                    if (s.size() <= n) continue;
                    if (_wcsicmp(s.c_str() + s.size() - n, kOffSuffix) != 0) continue;
                    std::wstring original = s.substr(0, s.size() - n);
                    std::wstring full = std::wstring(hk.path) + L"\\" + s;
                    RegRenameKey(root, full.c_str(), original.c_str());
                }
            }
        }
    }
    if (!ok && err) *err = L"部分注册项无法修改（位于 HKLM 的需要管理员权限）";
    Log(L"外壳扩展 %s (%s) -> %s", ext.name.c_str(), ext.clsid.c_str(), blocked ? L"屏蔽" : L"启用");
    return ok;
}

int TweakEngine::BlockedCount() const {
    std::vector<std::wstring> v;
    if (RegEnumValues(HKEY_CURRENT_USER, kBlocked, &v)) return (int)v.size();
    return 0;
}

// ---------------------------------------------------------------- 基准代理
static std::wstring BenchSampleFile() {
    std::wstring p = DataDir() + L"\\bench_sample.txt";
    if (!FileExists(p)) WriteTextFile(p, "openpuls bench sample\n");
    return p;
}

static std::wstring BenchPathFor(const std::wstring& kind) {
    if (kind == L"file" || kind == L"prop") return BenchSampleFile();
    if (kind == L"drive") return L"C:\\";
    return KnownFolder(FOLDERID_Windows);
}

bool TweakEngine::RunExtBench(ShellExt* ext) const {
    std::wstring kind = ext->kindKey.empty() ? L"file" : ext->kindKey;
    if (kind == L"drop" || kind == L"prop") kind = L"file";
    double ms = 0;
    int items = 0;
    std::wstring note;
    bool ok = diag::RunExtBenchProcess(ext->clsid, kind, BenchPathFor(kind), &ms, &items, &note);
    ext->benchMs = ok ? ms : -1;
    ext->benchNote = ok ? Fmt(L"%d 项", items) : note;
    return ok;
}

bool TweakEngine::RunMenuBench(const std::wstring& path, const std::wstring& kind, double* totalMs,
                               int* items, std::wstring* err) const {
    std::wstring note;
    bool ok = diag::RunMenuBenchProcess(kind, path, 3, totalMs, items, &note);
    if (!ok && err) err->assign(note);
    return ok;
}

}  // namespace op
