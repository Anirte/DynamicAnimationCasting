@echo off
rem Builds loki_DynamicAnimationCasting.dll into build\.
rem Needs Visual Studio 2022 or 2026 (or Build Tools) with "Desktop development with C++" and its vcpkg component.
rem Optional: set COMMONLIB_DIR to an existing CommonLibSSE-NG checkout to skip downloading it.
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT (echo Visual Studio with C++ tools not found & exit /b 1)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
if not defined VCPKG_ROOT set "VCPKG_ROOT=%VSROOT%\VC\vcpkg"
cd /d "%~dp0"
set "EXTRA="
if defined COMMONLIB_DIR set "EXTRA=-DCOMMONLIB_DIR=%COMMONLIB_DIR%"
cmake -S . -B build -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL %EXTRA% || exit /b 1
cmake --build build
