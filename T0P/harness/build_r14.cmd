@echo off
setlocal
set "HERE=%~dp0"
for %%I in ("%HERE%..\..") do set "ROOT=%%~fI"
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "OBJ=%HERE%logs_r14\obj_r14"
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
if not exist "%OBJ%" mkdir "%OBJ%"
if not exist "%OBJ%\tmp" mkdir "%OBJ%\tmp"
set "TEMP=%OBJ%\tmp"
set "TMP=%OBJ%\tmp"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 -vcvars_ver=14.44.35207 >nul
if errorlevel 1 (echo HARNESS_R14_BUILD REJECT vcvars64_failed & exit /b 1)
set "HARNESS_DIR=%HERE:~0,-1%"
set "INC=/I"%ROOT%\T0P\common" /I"%ROOT%\T0P\kernel" /I"%ROOT%\T0P\evict" /I"%HARNESS_DIR%""
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%ROOT%\T0P\common\common.cpp" /Fo:"%OBJ%\common.obj" > "%OBJ%\build_r14.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:precise /arch:AVX2 /D__AVXVNNI__ /D_WIN32_WINNT=0x0A00 %INC% "%ROOT%\T0P\kernel\q4_kernel.cpp" /Fo:"%OBJ%\q4_kernel.obj" >> "%OBJ%\build_r14.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%HERE%topology_r14.cpp" /Fo:"%OBJ%\topology_r14.obj" >> "%OBJ%\build_r14.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /arch:AVX2 /D__AVXVNNI__ /D_WIN32_WINNT=0x0A00 %INC% "%HERE%residency_r14.cpp" /Fo:"%OBJ%\residency_r14.obj" >> "%OBJ%\build_r14.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%HERE%main_r14.cpp" /Fo:"%OBJ%\main_r14.obj" >> "%OBJ%\build_r14.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%ROOT%\T0P\evict\evict.cpp" /Fo:"%OBJ%\evict_linked_r14.obj" >> "%OBJ%\build_r14.log" 2>&1
if errorlevel 1 goto fail
link /INCREMENTAL:NO /OPT:REF "%OBJ%\main_r14.obj" "%OBJ%\residency_r14.obj" "%OBJ%\topology_r14.obj" "%OBJ%\common.obj" "%OBJ%\q4_kernel.obj" "%OBJ%\evict_linked_r14.obj" bcrypt.lib advapi32.lib psapi.lib /OUT:"%OBJ%\harness_r14.exe" >> "%OBJ%\build_r14.log" 2>&1
if errorlevel 1 goto fail
echo HARNESS_R14_BUILD PASS linked=T0P\evict\evict.hpp+evict.cpp output=%OBJ%\harness_r14.exe
exit /b 0
:fail
type "%OBJ%\build_r14.log"
exit /b 1
