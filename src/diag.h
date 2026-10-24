// 诊断与基准测试（含崩溃隔离的子进程测量）
#pragma once
#include "util.h"
#include "tweaks.h"

namespace op {
namespace diag {

// level: 0 优秀 1 良好 2 一般 3 差
struct Metric {
    std::wstring key;
    std::wstring name;
    std::wstring value;
    std::wstring verdict;
    std::wstring advice;
    int level = 1;
};

struct Report {
    std::wstring stamp;
    std::vector<Metric> metrics;
    std::vector<std::wstring> recommendations;
    std::wstring os;
    std::wstring cpu;
    unsigned long long ramMb = 0;
    bool touch = false;
    bool admin = false;
    double ctxFileMs = 0, ctxDirMs = 0, ctxBgMs = 0;
    double openDialogMs = 0, enumMs = 0, launchMs = 0, shellPingMs = 0;
    double refreshHz = 0, frameMs = 0;
    int extTotal = 0, extThirdParty = 0, extBlocked = 0;
    int itemsFile = 0, itemsDir = 0;
    int slowExtCount = 0;
};

using ProgressFn = std::function<void(const std::wstring& text, int percent)>;

bool RunFull(Report* r, std::vector<ShellExt>* exts, ProgressFn progress);
std::wstring ToMarkdown(const Report& r, const std::vector<ShellExt>& exts);

bool ChildMain(const std::vector<std::wstring>& args, int* exitCode);
bool RunExtBenchProcess(const std::wstring& clsid, const std::wstring& kind, const std::wstring& path,
                        double* ms, int* items, std::wstring* note);
bool RunMenuBenchProcess(const std::wstring& kind, const std::wstring& path, int runs, double* ms,
                         int* items, std::wstring* note, double* minMs = nullptr,
                         double* maxMs = nullptr);

// 运行中的常驻监控用到的轻量探针
double ShellPingMs();

struct ExplorerStat {
    double cpuSeconds = 0;
    double wsMb = 0;
    int threads = 0;
    int count = 0;
    DWORD pid = 0;
};

ExplorerStat GetExplorerStatNow();
double ExplorerCpuPercent();

}  // namespace diag
}  // namespace op
