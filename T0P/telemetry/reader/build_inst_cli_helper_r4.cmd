@echo off
setlocal
set "ROOT=%~dp0"
set "VSROOT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 2
ml64 /nologo /c /Fo"%ROOT%known_loop_cli_r4.obj" "%ROOT%known_loop.asm"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /W4 /D_CRT_SECURE_NO_WARNINGS /Fo"%ROOT%inst_cli_helper_r4.obj" "%ROOT%inst_cli_helper_r4.cpp" "%ROOT%known_loop_cli_r4.obj" /Fe:"%ROOT%inst_cli_helper_r4.exe"
if errorlevel 1 exit /b %errorlevel%
