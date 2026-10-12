@echo off
setlocal
cd /d "%~dp0"
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 -vcvars_ver=14.44.35207 >nul 2>&1
if errorlevel 1 exit /b %errorlevel%
cd /d "%~dp0"
cl /nologo /c /O2 /std:c++17 /EHsc /MD /DNDEBUG /arch:AVX2 /D_WIN32_WINNT=0x0A00 /I"..\common" evict.cpp /Fo:evict_sync.obj
if errorlevel 1 exit /b %errorlevel%
cl /nologo /c /O2 /std:c++17 /EHsc /MD /DNDEBUG /arch:AVX2 /D_WIN32_WINNT=0x0A00 /I"..\common" evict_sync_qa.cpp /Fo:evict_sync_qa.obj
if errorlevel 1 exit /b %errorlevel%
cl /nologo /c /O2 /std:c++17 /EHsc /MD /DNDEBUG /D_WIN32_WINNT=0x0A00 /I"..\common" ..\common\common.cpp /Fo:common_sync.obj
if errorlevel 1 exit /b %errorlevel%
cl /nologo /c /O2 /std:c++17 /EHsc /MD /D_WIN32_WINNT=0x0A00 ..\telemetry\reader\sync\emon_window_sync.cpp /Fo:emon_window_sync.obj
if errorlevel 1 exit /b %errorlevel%
link /INCREMENTAL:NO evict_sync.obj evict_sync_qa.obj common_sync.obj emon_window_sync.obj advapi32.lib /OUT:evict_sync_qa.exe
if errorlevel 1 exit /b %errorlevel%
exit /b 0
