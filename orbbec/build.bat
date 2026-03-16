@echo off
if not defined VSCMD_VER (
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
)
cmake --preset msvc-release
cmake --build build --parallel
