#include "util.h"

#include <shlobj.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace op {

// ------------------------------------------------------------------ 字符串
std::wstring Fmt(const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    if (n < 0) n = 0;
    return std::wstring(buf, (size_t)n);
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n);
    return out;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n, nullptr, nullptr);
    return out;
}

std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool IEquals(const std::wstring& a, const wchar_t* b) {
    return _wcsicmp(a.c_str(), b) == 0;
}

std::wstring ToLower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

// ------------------------------------------------------------------ 路径
std::wstring ExePath() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    return std::wstring(buf, n);
}

std::wstring ExeDir() {
    std::wstring p = ExePath();
    size_t pos = p.find_last_of(L'\\');
    return pos == std::wstring::npos ? L"." : p.substr(0, pos);
}

std::wstring DataDir() {
    static std::wstring cached;
    if (cached.empty()) {
        std::wstring base = KnownFolder(FOLDERID_LocalAppData);
        if (base.empty()) base = ExeDir();
        cached = base + L"\\OpenPuls";
        EnsureDir(cached);
    }
    return cached;
}

bool EnsureDir(const std::wstring& dir) {
    if (dir.empty()) return false;
    if (GetFileAttributesW(dir.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    int r = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS;
}

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p) {
        std::wstring s = p;
        CoTaskMemFree(p);
        return s;
    }
    return L"";
}

std::wstring TempFile(const wchar_t* prefix, const wchar_t* ext) {
    wchar_t dir[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    wchar_t path[MAX_PATH];
    GetTempFileNameW(dir, prefix, 0, path);
    std::wstring p = path;
    if (ext && *ext) {
        DeleteFileW(p.c_str());
        p += ext;
    }
    return p;
}

bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool ReadTextFile(const std::wstring& path, std::string* out, size_t maxBytes) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    out->clear();
    char buf[8192];
    DWORD rd = 0;
    while (out->size() < maxBytes && ReadFile(h, buf, sizeof(buf), &rd, nullptr) && rd > 0)
        out->append(buf, rd);
    CloseHandle(h);
    return true;
}

bool WriteTextFile(const std::wstring& path, const std::string& text, bool bom) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    if (bom) WriteFile(h, "\xEF\xBB\xBF", 3, &wr, nullptr);
    WriteFile(h, text.data(), (DWORD)text.size(), &wr, nullptr);
    CloseHandle(h);
    return true;
}

// ------------------------------------------------------------------ 计时
void Timer::Start() { QueryPerformanceCounter(&a); }

double Timer::Ms() const {
    LARGE_INTEGER b, f;
    QueryPerformanceCounter(&b);
    QueryPerformanceFrequency(&f);
    return (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)f.QuadPart;
}

double Timer::Restart() {
    double v = Ms();
    Start();
    return v;
}

double QpcMs() {
    LARGE_INTEGER b, f;
    QueryPerformanceCounter(&b);
    QueryPerformanceFrequency(&f);
    return (double)b.QuadPart * 1000.0 / (double)f.QuadPart;
}

// ------------------------------------------------------------------ 日志
static std::mutex g_logLock;

std::wstring LogFilePath() { return DataDir() + L"\\OpenPuls.log"; }

void LogLine(const std::wstring& s) {
    std::lock_guard<std::mutex> lk(g_logLock);
    std::wstring path = LogFilePath();
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
        LARGE_INTEGER sz;
        sz.HighPart = (LONG)fad.nFileSizeHigh;
        sz.LowPart = fad.nFileSizeLow;
        if (sz.QuadPart > 2 * 1024 * 1024) DeleteFileW(path.c_str());
    }
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::wstring line = Fmt(L"%04d-%02d-%02d %02d:%02d:%02d.%03d  %s\r\n", st.wYear, st.wMonth,
                            st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, s.c_str());
    if (GetFileSize(h, nullptr) == 0) {
        DWORD wr = 0;
        WriteFile(h, "\xFF\xFE", 2, &wr, nullptr);
    }
    DWORD wr = 0;
    WriteFile(h, line.c_str(), (DWORD)(line.size() * sizeof(wchar_t)), &wr, nullptr);
    CloseHandle(h);
}

void Log(const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    if (n < 0) n = 0;
    LogLine(std::wstring(buf, (size_t)n));
}

// ------------------------------------------------------------------ 进程
bool RunAndWait(const std::wstring& cmdline, DWORD timeoutMs, DWORD* exitCode, std::string* stdoutText) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (stdoutText && !CreatePipe(&rd, &wr, &sa, 0)) return false;
    if (rd) SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (wr) {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = wr;
        si.hStdError = wr;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }
    PROCESS_INFORMATION pi{};
    std::wstring cmd = cmdline;
    BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, wr ? TRUE : FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (wr) CloseHandle(wr);
    if (!ok) {
        if (rd) CloseHandle(rd);
        return false;
    }
    DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs);
    if (wait == WAIT_TIMEOUT) TerminateProcess(pi.hProcess, 1);
    if (stdoutText) {
        stdoutText->clear();
        if (rd) {
            char buf[4096];
            DWORD got = 0;
            while (ReadFile(rd, buf, sizeof(buf), &got, nullptr) && got > 0) stdoutText->append(buf, got);
        }
    }
    if (exitCode) GetExitCodeProcess(pi.hProcess, exitCode);
    if (rd) CloseHandle(rd);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return wait != WAIT_TIMEOUT;
}

bool IsAdmin() {
    BOOL ok = FALSE;
    PSID admin = nullptr;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0,
                                 0, 0, 0, &admin)) {
        CheckTokenMembership(nullptr, admin, &ok);
        FreeSid(admin);
    }
    return ok != FALSE;
}

// ------------------------------------------------------------------ 注册表
std::wstring HkeyName(HKEY h) {
    if (h == HKEY_CURRENT_USER) return L"HKCU";
    if (h == HKEY_LOCAL_MACHINE) return L"HKLM";
    if (h == HKEY_CLASSES_ROOT) return L"HKCR";
    if (h == HKEY_USERS) return L"HKU";
    return L"HKLM";
}

bool RegGetStr(HKEY root, const std::wstring& sub, const std::wstring& name, std::wstring* out,
               DWORD* type) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    DWORD t = 0, cb = 0;
    LSTATUS st = RegQueryValueExW(k, name.c_str(), nullptr, &t, nullptr, &cb);
    if (st != ERROR_SUCCESS) {
        RegCloseKey(k);
        return false;
    }
    std::vector<BYTE> buf(cb + sizeof(wchar_t) * 2, 0);
    cb = (DWORD)buf.size();
    st = RegQueryValueExW(k, name.c_str(), nullptr, &t, buf.data(), &cb);
    RegCloseKey(k);
    if (st != ERROR_SUCCESS) return false;
    if (type) *type = t;
    if (t == REG_SZ || t == REG_EXPAND_SZ) {
        size_t chars = cb / sizeof(wchar_t);
        while (chars > 0 && ((wchar_t*)buf.data())[chars - 1] == 0) --chars;
        out->assign((const wchar_t*)buf.data(), chars);
    } else if (t == REG_DWORD) {
        out->clear();
    } else {
        out->assign((const wchar_t*)buf.data(), cb / sizeof(wchar_t));
    }
    return true;
}

bool RegGetDword(HKEY root, const std::wstring& sub, const std::wstring& name, DWORD* out) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    DWORD t = 0, cb = sizeof(DWORD), v = 0;
    LSTATUS st = RegQueryValueExW(k, name.c_str(), nullptr, &t, (LPBYTE)&v, &cb);
    RegCloseKey(k);
    if (st != ERROR_SUCCESS || t != REG_DWORD) return false;
    *out = v;
    return true;
}

bool RegSetStr(HKEY root, const std::wstring& sub, const std::wstring& name, const std::wstring& val) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(root, sub.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) !=
        ERROR_SUCCESS)
        return false;
    LSTATUS st = RegSetValueExW(k, name.c_str(), 0, REG_SZ, (const BYTE*)val.c_str(),
                                (DWORD)((val.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
    return st == ERROR_SUCCESS;
}

bool RegSetEmpty(HKEY root, const std::wstring& sub, const std::wstring& name) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(root, sub.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) !=
        ERROR_SUCCESS)
        return false;
    LSTATUS st = RegSetValueExW(k, name.c_str(), 0, REG_SZ, (const BYTE*)L"", 0);
    RegCloseKey(k);
    return st == ERROR_SUCCESS;
}

bool RegSetDword(HKEY root, const std::wstring& sub, const std::wstring& name, DWORD val) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(root, sub.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) !=
        ERROR_SUCCESS)
        return false;
    LSTATUS st = RegSetValueExW(k, name.c_str(), 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
    RegCloseKey(k);
    return st == ERROR_SUCCESS;
}

bool RegDelValue(HKEY root, const std::wstring& sub, const std::wstring& name) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return false;
    LSTATUS st = RegDeleteValueW(k, name.c_str());
    RegCloseKey(k);
    return st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND;
}

bool RegDelKeyTree(HKEY root, const std::wstring& sub) {
    RegDeleteTreeW(root, sub.c_str());
    LSTATUS st = RegDeleteKeyW(root, sub.c_str());
    return st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND;
}

bool RegKeyExists(HKEY root, const std::wstring& sub) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    RegCloseKey(k);
    return true;
}

bool RegEnumSubKeys(HKEY root, const std::wstring& sub, std::vector<std::wstring>* out) {
    out->clear();
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE, &k) !=
        ERROR_SUCCESS)
        return false;
    wchar_t name[512];
    for (DWORD i = 0;; ++i) {
        DWORD cb = 512;
        LSTATUS st = RegEnumKeyExW(k, i, name, &cb, nullptr, nullptr, nullptr, nullptr);
        if (st == ERROR_NO_MORE_ITEMS) break;
        if (st != ERROR_SUCCESS) break;
        out->emplace_back(name, cb);
    }
    RegCloseKey(k);
    return true;
}

bool RegEnumValues(HKEY root, const std::wstring& sub, std::vector<std::wstring>* out) {
    out->clear();
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    wchar_t name[1024];
    for (DWORD i = 0;; ++i) {
        DWORD cb = 1024;
        LSTATUS st = RegEnumValueW(k, i, name, &cb, nullptr, nullptr, nullptr, nullptr);
        if (st == ERROR_NO_MORE_ITEMS) break;
        if (st != ERROR_SUCCESS) break;
        out->emplace_back(name, cb);
    }
    RegCloseKey(k);
    return true;
}

// ------------------------------------------------------------------ 设置
static const wchar_t* kStateKey = L"Software\\OpenPuls\\ShellOpt\\State";

std::wstring SettingsGet(const std::wstring& name, const std::wstring& def) {
    std::wstring v;
    if (RegGetStr(HKEY_CURRENT_USER, kStateKey, name, &v)) return v;
    return def;
}

int SettingsGetInt(const std::wstring& name, int def) {
    DWORD v = 0;
    if (RegGetDword(HKEY_CURRENT_USER, kStateKey, name, &v)) return (int)v;
    std::wstring s = SettingsGet(name);
    return s.empty() ? def : _wtoi(s.c_str());
}

void SettingsSet(const std::wstring& name, const std::wstring& val) {
    RegSetStr(HKEY_CURRENT_USER, kStateKey, name, val);
}

void SettingsSetInt(const std::wstring& name, int val) {
    RegSetDword(HKEY_CURRENT_USER, kStateKey, name, (DWORD)val);
}

void SettingsDel(const std::wstring& name) { RegDelValue(HKEY_CURRENT_USER, kStateKey, name); }

// ------------------------------------------------------------------ 系统
static std::vector<DWORD> ExplorerPids() {
    std::vector<DWORD> pids;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return pids;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"explorer.exe") == 0) pids.push_back(pe.th32ProcessID);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pids;
}

bool RestartExplorer() {
    Log(L"正在重启资源管理器…");
    HWND shell = GetShellWindow();
    DWORD oldPid = 0;
    if (shell) GetWindowThreadProcessId(shell, &oldPid);
    auto pids = ExplorerPids();
    for (DWORD pid : pids) {
        HANDLE p = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        if (!p) continue;
        TerminateProcess(p, 1);
        WaitForSingleObject(p, 3000);
        CloseHandle(p);
    }
    // 等待系统自动重启
    for (int i = 0; i < 100; ++i) {
        Sleep(100);
        auto now = ExplorerPids();
        for (DWORD pid : now) {
            if (pid != oldPid) {
                Log(L"资源管理器已重启，pid=%u", pid);
                return true;
            }
        }
    }
    Log(L"资源管理器未自动重启，手动启动。");
    ShellExecuteW(nullptr, L"open", L"explorer.exe", nullptr, nullptr, SW_SHOWNORMAL);
    Sleep(600);
    return !ExplorerPids().empty();
}

bool IsTouchPresent() {
    int d = GetSystemMetrics(SM_DIGITIZER);
    return (d & (NID_INTEGRATED_TOUCH | NID_EXTERNAL_TOUCH)) != 0 || (d & NID_MULTI_INPUT) != 0;
}

std::wstring OsBuildString() {
    typedef LONG(WINAPI * RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    std::wstring out;
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        auto fn = (RtlGetVersionPtr)GetProcAddress(ntdll, "RtlGetVersion");
        RTL_OSVERSIONINFOW vi{};
        vi.dwOSVersionInfoSize = sizeof(vi);
        if (fn && fn(&vi) == 0)
            out = Fmt(L"%u.%u.%u", vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber);
    }
    return out.empty() ? L"unknown" : out;
}

std::wstring CpuName() {
    wchar_t buf[512] = {};
    DWORD cb = sizeof(buf);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                     L"ProcessorNameString", RRF_RT_REG_SZ, nullptr, buf, &cb) == ERROR_SUCCESS)
        return Trim(buf);
    return L"未知 CPU";
}

unsigned long long TotalRamMb() {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) return ms.ullTotalPhys / (1024ull * 1024ull);
    return 0;
}

}  // namespace op
