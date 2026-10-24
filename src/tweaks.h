// 优化项定义与注册表动作
#pragma once
#include "util.h"

namespace op {

enum class TGroup { Menu, Launch, Touch, Advanced };
enum class TState { Unknown, Default, Applied, Partial };

struct RegTarget {
    HKEY root = HKEY_CURRENT_USER;
    std::wstring sub;
    std::wstring name;   // 空 = 默认值
    bool dword = false;
    DWORD dw = 0;
    std::wstring sz;
    bool emptyStr = false;  // 写入 0 长度 REG_SZ（经典右键菜单用）
};

struct TweakDef {
    std::wstring id;
    TGroup group = TGroup::Menu;
    std::wstring title;
    std::wstring desc;
    std::wstring warn;
    bool admin = false;
    bool needsExplorer = false;
    bool advanced = false;
    bool supported = true;
    std::vector<RegTarget> targets;
    std::wstring deleteOnRevert;  // HKCU 下需整体删除的键（可空）
};

// 单条外壳扩展
struct ShellExt {
    std::wstring clsid;
    std::wstring name;
    std::wstring module;
    std::wstring kinds;    // 文件 / 文件夹 / 空白处 / 驱动器 / 属性页 / 拖放
    std::wstring kindKey;  // file|dir|bg|drive|prop|drop
    std::wstring origin;   // HKLM / HKCU
    std::vector<std::wstring> regs;  // "HKCU|Software\\Classes\\*\\shellex\\...|<子键名>"
    bool blocked = false;
    bool system = false;
    double benchMs = -1;
    std::wstring benchNote;
};

class TweakEngine {
public:
    void Init();

    std::vector<TweakDef>& All() { return tweaks_; }
    const TweakDef* Find(const std::wstring& id) const;

    bool IsWanted(const std::wstring& id) const;
    void SetWanted(const std::wstring& id, bool on);

    TState Probe(const TweakDef& t) const;
    bool SetEnabled(const TweakDef& t, bool on, std::wstring* err);
    bool HasBackup(const std::wstring& id) const;
    bool RestoreTweak(const TweakDef& t, std::wstring* err);
    int  RestoreAll(std::wstring* err);
    int  EnforceWanted(std::wstring* err);

    // 特殊动作
    bool BuildIconCache(std::wstring* err);
    bool SetHighPerformancePlan(bool on, std::wstring* err);
    bool IsHighPerformancePlan() const;

    // 外壳扩展
    std::vector<ShellExt> EnumerateShellExts() const;
    bool SetExtBlocked(const ShellExt& ext, bool blocked, std::wstring* err);
    int  BlockedCount() const;

    // 后台基准测试（子进程隔离）
    bool RunExtBench(ShellExt* ext) const;
    bool RunMenuBench(const std::wstring& path, const std::wstring& kind, double* totalMs,
                      int* items, std::wstring* err) const;

private:
    std::vector<TweakDef> tweaks_;
    void BuildTable();
    bool BackupOriginals(const TweakDef& t);
    bool RestoreOriginals(const TweakDef& t);
};

ShellExt* FindExt(std::vector<ShellExt>& v, const std::wstring& clsid);

}  // namespace op
