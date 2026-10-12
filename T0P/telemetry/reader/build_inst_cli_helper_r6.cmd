@echo off
setlocal
set "ROOT=%~dp0"
if exist "%ROOT%known_loop_cli_r6.obj" exit /b 20
if exist "%ROOT%inst_cli_helper_r6.obj" exit /b 21
if exist "%ROOT%inst_cli_helper_r6.exe" exit /b 22
if exist "%ROOT%known_loop_r6_disassembly.txt" exit /b 23
set "VSROOT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 2
ml64 /nologo /c /Fo"%ROOT%known_loop_cli_r6.obj" "%ROOT%known_loop_r6.asm"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /W4 /D_CRT_SECURE_NO_WARNINGS /Fo"%ROOT%inst_cli_helper_r6.obj" "%ROOT%inst_cli_helper_r6.cpp" "%ROOT%known_loop_cli_r6.obj" /Fe:"%ROOT%inst_cli_helper_r6.exe"
if errorlevel 1 exit /b %errorlevel%
dumpbin /DISASM "%ROOT%known_loop_cli_r6.obj" > "%ROOT%known_loop_r6_disassembly.txt"
if errorlevel 1 exit /b %errorlevel%
