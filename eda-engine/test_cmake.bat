@echo off
call "D:\software\VisualStudio\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d D:\code\vscode\PCB\eda-engine
echo === Testing cl.exe ===
where cl
echo.
echo === Testing cmake ===
cmake --version
echo.
echo === Running CMake Configure ===
cmake -S . -B cmake-build-debug -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Debug
echo.
echo === EXIT CODE: %ERRORLEVEL% ===
