@echo off
REM ============================================================
REM   清理 in-source build 污染 — 仅删源码根目录散落的构建产物
REM   out-of-source 构建后所有产物只落在 build/ 下
REM ============================================================
setlocal
cd /d "%~dp0"

echo.
echo [1/4] 删除根目录散落的 CMake 产物...
if exist "CMakeCache.txt"        del /q "CMakeCache.txt"
if exist "CTestTestfile.cmake"   del /q "CTestTestfile.cmake"
if exist "cmake_install.cmake"   del /q "cmake_install.cmake"
if exist "CMakeFiles"            rmdir /s /q "CMakeFiles"
if exist ".cmake"                rmdir /s /q ".cmake"
if exist "cmake-build-debug"     rmdir /s /q "cmake-build-debug"

echo [2/4] 删除根目录散落的 VS/MSBuild 工程文件...
if exist "eda_engine.sln"        del /q "eda_engine.sln"
if exist "eda_engine.slnx"       del /q "eda_engine.slnx"
for %%f in (*.vcxproj *.vcxproj.filters *.vcxproj.user) do if exist "%%f" del /q "%%f"
for /d %%d in (*.dir) do if exist "%%d" rmdir /s /q "%%d"
if exist "Debug"                 rmdir /s /q "Debug"
if exist "x64"                   rmdir /s /q "x64"
if exist "_deps"                 rmdir /s /q "_deps"

echo [3/4] 删除各子模块目录散落的 CMake 产物...
for %%m in (core servers platform drivers scene main eda editor tests) do (
    if exist "%%m\CMakeFiles"          rmdir /s /q "%%m\CMakeFiles"
    if exist "%%m\CTestTestfile.cmake" del /q   "%%m\CTestTestfile.cmake"
    if exist "%%m\cmake_install.cmake" del /q   "%%m\cmake_install.cmake"
    if exist "%%m\Debug"               rmdir /s /q "%%m\Debug"
    if exist "%%m\*.dir"               rmdir /s /q "%%m\*.dir" 2>nul
    REM VS 在子目录生成的 *.vcxproj / *.dir
    if exist "%%m\*.vcxproj"           del /q   "%%m\*.vcxproj" 2>nul
    if exist "%%m\*.vcxproj.filters"   del /q   "%%m\*.vcxproj.filters" 2>nul
)

echo [4/4] 删除 tests 目录中 *.dir 子目录（VS 生成器残留）...
if exist "tests\*.dir" rmdir /s /q "tests\*.dir" 2>nul

echo.
echo ✅ 清理完成。现在用 out-of-source 方式重新构建：
echo    cmake -B build
echo    cmake --build build --config Debug
echo    build\bin\eda_main.exe
echo.
endlocal
