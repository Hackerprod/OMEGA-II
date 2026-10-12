@echo off
setlocal
cd /d "%~dp0"
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 -vcvars_ver=14.44.35207 >nul 2>&1
if errorlevel 1 exit /b %errorlevel%
cd /d "%~dp0"
cl /nologo /c /O2 /std:c++17 /EHsc /MD /DNDEBUG /arch:AVX2 /D_WIN32_WINNT=0x0A00 /I"..\common" evict.cpp /Fo:evict.obj
if errorlevel 1 exit /b %errorlevel%
cl /nologo /c /O2 /std:c++17 /EHsc /MD /DNDEBUG /arch:AVX2 /D_WIN32_WINNT=0x0A00 /I"..\common" evict_qa.cpp /Fo:evict_qa.obj
if errorlevel 1 exit /b %errorlevel%
cl /nologo /c /O2 /std:c++17 /EHsc /MD /DNDEBUG /D_WIN32_WINNT=0x0A00 /I"..\common" ..\common\common.cpp /Fo:common.obj
if errorlevel 1 exit /b %errorlevel%
link /INCREMENTAL:NO evict.obj evict_qa.obj common.obj advapi32.lib /OUT:evict_qa.exe
if errorlevel 1 exit /b %errorlevel%
"%~dp0evict_qa.exe" > "%~dp0evict_qa_raw.log" 2>&1
exit /b %errorlevel%
