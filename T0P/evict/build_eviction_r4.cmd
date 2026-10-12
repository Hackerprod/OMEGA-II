@echo off
setlocal
set "HERE=%~dp0"
set "ROOT=%HERE%..\.."
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 -vcvars_ver=14.44.35207 >nul
if errorlevel 1 exit /b 1
cd /d "%HERE%"
cl /nologo /c /O2 /std:c++17 /EHsc /MD /DNDEBUG /arch:AVX2 /D_WIN32_WINNT=0x0A00 evict.cpp /Fo:evict_r4.obj > build_eviction_r4.log 2>&1
if errorlevel 1 (type build_eviction_r4.log & exit /b 1)
cl /nologo /c /O2 /std:c++17 /EHsc /MD /DNDEBUG /arch:AVX2 /D_WIN32_WINNT=0x0A00 /I"..\common" evict_qualification_r4.cpp /Fo:evict_qualification_r4.obj >> build_eviction_r4.log 2>&1
if errorlevel 1 (type build_eviction_r4.log & exit /b 1)
link /INCREMENTAL:NO evict_r4.obj evict_qualification_r4.obj common.obj advapi32.lib /OUT:evict_qualification_r4.exe >> build_eviction_r4.log 2>&1
if errorlevel 1 (type build_eviction_r4.log & exit /b 1)
echo EVICT_BUILD=PASS LINKED_OBJECT=evict_r4.obj
exit /b 0
