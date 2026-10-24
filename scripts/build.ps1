# OpenPuls 响应优化器 - 编译脚本（PowerShell 版，中文提示）
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$candidates = @(
    'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
)
$vcvars = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $vcvars) {
    Write-Host '[x] 未找到 vcvars64.bat，请安装 Visual Studio 生成工具（C++ 桌面开发）。' -ForegroundColor Red
    exit 1
}
Write-Host "[*] 编译器环境: $vcvars"

if (-not (Test-Path build)) { New-Item -ItemType Directory build | Out-Null }

$python = ''
$py = Get-Command python -ErrorAction SilentlyContinue
if ($py) { $python = $py.Source }
if ($python) { & $python scripts\make_icon.py | Out-Null }

$sources = 'src\main.cpp src\util.cpp src\tweaks.cpp src\diag.cpp src\monitor.cpp src\ui.cpp'
$cmd = "call `"$vcvars`" >nul && rc /nologo /fo build\app.res res\app.rc && cl /nologo /std:c++17 /EHsc /O2 /MT /utf-8 /W3 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Fobuild\ /Fe:build\OpenPulsShellOpt.exe $sources build\app.res /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /OPT:REF /OPT:ICF"
Write-Host '[*] 编译中…'
cmd /c $cmd
if ($LASTEXITCODE -ne 0) {
    Write-Host '[x] 编译失败。' -ForegroundColor Red
    exit $LASTEXITCODE
}
Write-Host '[+] 构建完成: build\OpenPulsShellOpt.exe' -ForegroundColor Green
