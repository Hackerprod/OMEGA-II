@echo off
setlocal
set "SRC_DIR=%~dp0.."
set "OUT=%~dp0"
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 -vcvars_ver=14.44.35207 >nul
if errorlevel 1 exit /b 1
cl /nologo /Bv /c /O2 /std:c++17 /arch:AVX2 /EHsc /Brepro /D_WIN32_WINNT=0x0A00 "%SRC_DIR%\q4_kernel.cpp" /Fo:"%OUT%\phase0_q4_kernel.obj"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /Bv /c /O2 /std:c++17 /arch:AVX2 /EHsc /Brepro /D_WIN32_WINNT=0x0A00 "%SRC_DIR%\phase0_bench.cpp" /Fo:"%OUT%\phase0_bench.obj"
if errorlevel 1 exit /b %errorlevel%
link /Brepro /INCREMENTAL:NO "%OUT%\phase0_q4_kernel.obj" "%OUT%\phase0_bench.obj" /OUT:"%OUT%\phase0_bench.exe"
exit /b %errorlevel%
