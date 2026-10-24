#include "monitor.h"
#include "diag.h"
#include "tweaks.h"

#include <atomic>
#include <thread>
#include <mutex>

namespace op {

Monitor& Monitor::Inst() {
    static Monitor m;
    return m;
}

void Monitor::Start(HWND notify) {
    if (run_) {
        notify_ = notify;
        return;
    }
    if (!csInit_) {
        InitializeCriticalSection(&cs_);
        csInit_ = true;
    }
    notify_ = notify;
    run_ = true;
    th_ = std::thread([this] { Loop(); });
}

void Monitor::Stop() {
    run_ = false;
    if (th_.joinable()) th_.join();
}

bool Monitor::Running() const { return run_; }

std::vector<MonSample> Monitor::Snapshot() {
    std::vector<MonSample> v;
    if (!csInit_) return v;
    EnterCriticalSection(&cs_);
    v = hist_;
    LeaveCriticalSection(&cs_);
    return v;
}

MonSample Monitor::Last() {
    MonSample s;
    if (!csInit_) return s;
    EnterCriticalSection(&cs_);
    if (!hist_.empty()) s = hist_.back();
    LeaveCriticalSection(&cs_);
    return s;
}

std::wstring Monitor::LastAlertOr() {
    std::wstring v;
    if (!csInit_) return v;
    EnterCriticalSection(const_cast<CRITICAL_SECTION*>(&cs_));
    v = alert_;
    LeaveCriticalSection(const_cast<CRITICAL_SECTION*>(&cs_));
    return v;
}

void Monitor::Loop() {
    auto st0 = diag::GetExplorerStatNow();
    double prevCpu = st0.cpuSeconds;
    double prevT = QpcMs();
    int slowStreak = 0;
    ULONGLONG lastGuard = GetTickCount64();
    ULONGLONG lastAlert = 0;

    while (run_) {
        for (int i = 0; i < intervalMs_ / 100; ++i) {
            if (!run_) return;
            Sleep(100);
        }
        double now = QpcMs();
        auto st = diag::GetExplorerStatNow();
        double dt = (now - prevT) / 1000.0;
        double cpu = dt > 0.05 ? (st.cpuSeconds - prevCpu) / dt * 100.0 : 0;
        if (cpu < 0) cpu = 0;
        prevCpu = st.cpuSeconds;
        prevT = now;

        double ping = diag::ShellPingMs();
        MonSample s;
        s.ping = ping;
        s.cpu = cpu;
        s.ws = st.wsMb;

        EnterCriticalSection(&cs_);
        hist_.push_back(s);
        if (hist_.size() > 300) hist_.erase(hist_.begin(), hist_.begin() + (hist_.size() - 300));
        LeaveCriticalSection(&cs_);

        if (ping > 400) {
            if (++slowStreak >= 2 && GetTickCount64() - lastAlert > 300000) {
                lastAlert = GetTickCount64();
                EnterCriticalSection(&cs_);
                alert_ = Fmt(L"资源管理器加载缓慢：%.0f ms（%s）", ping, st.count ? L"主线程繁忙" : L"进程缺失");
                LeaveCriticalSection(&cs_);
                Log(L"[监控] 资源管理器响应 %.0f ms，CPU %.1f%%，内存 %.0f MB", ping, cpu, st.wsMb);
            }
        } else {
            slowStreak = 0;
        }

        if (notify_) PostMessageW(notify_, WM_OP_MONITOR, 0, 0);

        if (guard_ && GetTickCount64() - lastGuard > 60000) {
            lastGuard = GetTickCount64();
            TweakEngine eng;
            eng.Init();
            std::wstring err;
            int n = eng.EnforceWanted(&err);
            if (n > 0) {
                lastEnforced_ = n;
                Log(L"[守护] 重新应用了 %d 个被改动过的优化项", n);
            }
        }
    }
}

}  // namespace op
