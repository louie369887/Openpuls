// OpenPuls 响应优化器 - 入口
#include "diag.h"
#include "monitor.h"
#include "ui.h"
#include "util.h"

#include <shellapi.h>

static std::vector<std::wstring> SplitArgs() {
    std::vector<std::wstring> v;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return v;
    for (int i = 0; i < argc; ++i) v.emplace_back(argv[i]);
    LocalFree(argv);
    return v;
}

int APIENTRY wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    auto args = SplitArgs();

    // 测量子进程模式：不进入单实例逻辑
    if (args.size() >= 2 && args[1].rfind(L"--bench", 0) == 0) {
        int code = 0;
        if (op::diag::ChildMain(args, &code)) return code;
    }

    bool takeover = false;
    for (auto& a : args)
        if (a == L"--takeover") takeover = true;

    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"Local\\OpenPuls.ShellOpt.SingleInstance");
    bool already = (GetLastError() == ERROR_ALREADY_EXISTS);

    if (already && !takeover) {
        HWND w = FindWindowW(L"OP.MainWindow", nullptr);
        if (w) {
            AllowSetForegroundWindow(ASFW_ANY);
            PostMessageW(w, op::WM_OP_ACTIVATE, 0, 0);
        }
        if (mtx) CloseHandle(mtx);
        return 0;
    }

    int rc = op::RunApp(inst, nCmdShow);

    if (mtx) CloseHandle(mtx);
    return rc;
}
