cmake_minimum_required(VERSION 3.20)
project(OpenPulsShellOpt VERSION 1.0.0 LANGUAGES CXX)

if(NOT MSVC)
    message(WARNING "本项目按 MSVC / Windows SDK 编写，其它编译器未验证。")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(OpenPulsShellOpt WIN32
    src/main.cpp
    src/util.cpp
    src/tweaks.cpp
    src/diag.cpp
    src/monitor.cpp
    src/ui.cpp
    res/app.rc
)

target_compile_definitions(OpenPulsShellOpt PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
target_compile_options(OpenPulsShellOpt PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /W3 /MT>)
target_link_libraries(OpenPulsShellOpt PRIVATE
    comctl32 dwmapi powrprof shlwapi psapi uxtheme msimg32 ole32 oleaut32 uuid advapi32 shell32 user32 gdi32
)
set_target_properties(OpenPulsShellOpt PROPERTIES
    OUTPUT_NAME "OpenPulsShellOpt"
    MSVC_RUNTIME_LIBRARY "MultiThreaded"
)
