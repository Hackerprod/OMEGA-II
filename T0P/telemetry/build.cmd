@echo off
setlocal
set "ROOT=%~dp0"
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
set "VSROOT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto :build_env_error
set "VTUNE=C:\Program Files (x86)\Intel\oneAPI\vtune\2026.4"
ml64 /nologo /c /Fo"%ROOT%reader\known_loop.obj" "%ROOT%reader\known_loop.asm"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%VTUNE%\config\emon_api" /Fo"%ROOT%reader\telemetry_reader.obj" "%ROOT%reader\telemetry_reader.cpp" /link /LIBPATH:"%VTUNE%\lib64" prog_api.lib "%ROOT%reader\known_loop.obj" /OUT:"%ROOT%reader\telemetry_reader.exe"
if errorlevel 1 exit /b %errorlevel%
dumpbin /nologo /disasm "%ROOT%reader\known_loop.obj" > "%ROOT%logs\known_loop_disassembly.txt"
copy /y "%VTUNE%\bin64\prog_api.dll" "%ROOT%reader\prog_api.dll" >nul
if errorlevel 1 exit /b %errorlevel%
exit /b %errorlevel%
:build_env_error
echo ERROR: VS Build Tools C++ environment could not be initialized.
exit /b 2
