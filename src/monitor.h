// 后台常驻监控
#pragma once
#include "util.h"

#include <atomic>
#include <thread>
#include <mutex>

namespace op {

constexpr UINT WM_OP_MONITOR = WM_APP + 10;   // 采样完成
constexpr UINT WM_OP_ACTIVATE = WM_APP + 11;  // 第二实例请求显示窗口

struct MonSample {
    double ping = -1;   // 资源管理器消息往返
    double cpu = 0;     // explorer 进程组 CPU 占用 %
    double ws = 0;      // 工作集 MB
};

class Monitor {
public:
    static Monitor& Inst();

    void Start(HWND notify);
    void Stop();
    bool Running() const;

    int IntervalMs() const { return intervalMs_; }
    void SetIntervalMs(int v) { intervalMs_ = v < 500 ? 500 : (v > 10000 ? 10000 : v); }

    std::vector<MonSample> Snapshot();
    MonSample Last();

    bool GuardEnabled() const { return guard_; }
    void SetGuardEnabled(bool v) { guard_ = v; }

    int LastEnforced() const { return lastEnforced_; }
    std::wstring LastAlertOr();

private:
    void Loop();
    HWND notify_ = nullptr;
    std::atomic<bool> run_{false};
    std::atomic<bool> guard_{true};
    std::atomic<int> intervalMs_{2000};
    std::atomic<int> lastEnforced_{0};
    std::vector<MonSample> hist_;
    CRITICAL_SECTION cs_{};
    bool csInit_ = false;
    std::wstring alert_;
    std::thread th_;
};

}  // namespace op
