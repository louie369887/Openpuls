@echo off
rem OpenPuls Shell Response Optimizer - build script (MSVC x64)
setlocal
cd /d "%~dp0.."

set "VCVARS="
for %%P in (
  "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
) do if not defined VCVARS if exist %%P set "VCVARS=%%~P"

if not defined VCVARS goto :novs

echo [*] vcvars: %VCVARS%
call "%VCVARS%" >nul
if errorlevel 1 goto :fail

if not exist build mkdir build

echo [*] icon + resources
where python >nul 2>nul
if not errorlevel 1 python scripts\make_icon.py >nul 2>nul
rc /nologo /fo build\app.res res\app.rc
if errorlevel 1 goto :fail

echo [*] compile
cl /nologo /std:c++17 /EHsc /O2 /MT /utf-8 /W3 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Fobuild\ /Fe:build\OpenPulsShellOpt.exe src\main.cpp src\util.cpp src\tweaks.cpp src\diag.cpp src\monitor.cpp src\ui.cpp build\app.res /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /OPT:REF /OPT:ICF
if errorlevel 1 goto :fail

echo.
echo [+] done: build\OpenPulsShellOpt.exe
exit /b 0

:novs
echo [x] vcvars64.bat not found. Install Visual Studio Build Tools with C++ workload.
exit /b 1

:fail
echo [x] build failed.
exit /b 1
