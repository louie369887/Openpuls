// OpenPuls 响应优化器 - 基础工具层
#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "powrprof.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "msimg32.lib")

namespace op {

// ---------- 字符串 ----------
std::wstring Fmt(const wchar_t* fmt, ...);
std::wstring Utf8ToWide(const std::string& s);
std::string  WideToUtf8(const std::wstring& s);
std::wstring Trim(const std::wstring& s);
bool         IEquals(const std::wstring& a, const wchar_t* b);
std::wstring ToLower(std::wstring s);

// ---------- 路径 ----------
std::wstring ExePath();
std::wstring ExeDir();
std::wstring DataDir();      // %LOCALAPPDATA%\OpenPuls
bool         EnsureDir(const std::wstring& dir);
std::wstring KnownFolder(REFKNOWNFOLDERID id);
std::wstring TempFile(const wchar_t* prefix, const wchar_t* ext);
bool         FileExists(const std::wstring& p);
bool         ReadTextFile(const std::wstring& path, std::string* out, size_t maxBytes = 1 << 20);
bool         WriteTextFile(const std::wstring& path, const std::string& text, bool bom = false);

// ---------- 计时 ----------
struct Timer {
    LARGE_INTEGER a{};
    void Start();
    double Ms() const;     // 毫秒（浮点）
    double Restart();
};
double QpcMs();

// ---------- 日志 ----------
void Log(const wchar_t* fmt, ...);
void LogLine(const std::wstring& s);
std::wstring LogFilePath();

// ---------- 进程 ----------
bool RunAndWait(const std::wstring& cmdline, DWORD timeoutMs, DWORD* exitCode = nullptr,
                std::string* stdoutText = nullptr);
bool IsAdmin();

// ---------- 注册表 ----------
std::wstring HkeyName(HKEY h);
bool RegGetStr(HKEY root, const std::wstring& sub, const std::wstring& name,
               std::wstring* out, DWORD* type = nullptr);
bool RegGetDword(HKEY root, const std::wstring& sub, const std::wstring& name, DWORD* out);
bool RegSetStr(HKEY root, const std::wstring& sub, const std::wstring& name, const std::wstring& val);
bool RegSetDword(HKEY root, const std::wstring& sub, const std::wstring& name, DWORD val);
bool RegSetEmpty(HKEY root, const std::wstring& sub, const std::wstring& name);
bool RegDelValue(HKEY root, const std::wstring& sub, const std::wstring& name);
bool RegDelKeyTree(HKEY root, const std::wstring& sub);
bool RegKeyExists(HKEY root, const std::wstring& sub);
bool RegEnumSubKeys(HKEY root, const std::wstring& sub, std::vector<std::wstring>* out);
bool RegEnumValues(HKEY root, const std::wstring& sub, std::vector<std::wstring>* out);

// ---------- 设置存储（HKCU\Software\OpenPuls\ShellOpt） ----------
std::wstring SettingsGet(const std::wstring& name, const std::wstring& def = L"");
int          SettingsGetInt(const std::wstring& name, int def);
void         SettingsSet(const std::wstring& name, const std::wstring& val);
void         SettingsSetInt(const std::wstring& name, int val);
void         SettingsDel(const std::wstring& name);

// ---------- 系统 ----------
bool RestartExplorer();
bool IsTouchPresent();
std::wstring OsBuildString();
std::wstring CpuName();
unsigned long long TotalRamMb();

}  // namespace op
