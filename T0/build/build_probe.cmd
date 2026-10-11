@echo off
setlocal
set "T0_ROOT=%~dp0.."
set "OUT=%T0_ROOT%\build\out"
if not exist "%OUT%" mkdir "%OUT%"
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 -vcvars_ver=14.44.35207 >nul
if errorlevel 1 exit /b 1
cl /nologo /Bv /O2 /std:c++17 /arch:AVX2 /EHsc /Brepro /D_WIN32_WINNT=0x0A00 "%T0_ROOT%\src\t0_probe.cpp" /Fe:"%OUT%\t0_probe.exe" /Fo"%OUT%\t0_probe.obj" /link /Brepro /INCREMENTAL:NO
exit /b %errorlevel%
