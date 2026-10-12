@echo off
setlocal
cd /d "%~dp0"
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 -vcvars_ver=14.44.35207 >nul 2>&1
if errorlevel 1 exit /b %errorlevel%
cl /nologo /O2 /std:c++17 /EHsc /MD /D_WIN32_WINNT=0x0A00 emon_window_sync.cpp sync_qa.cpp /Fe:sync_qa.exe
if errorlevel 1 exit /b %errorlevel%
"%~dp0sync_qa.exe" > "%~dp0sync_qa_raw_r1.txt" 2>&1
exit /b %errorlevel%
