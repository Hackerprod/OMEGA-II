@echo off
setlocal
set "SRC_DIR=%~dp0.."
set "OUT=%~dp0"
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 -vcvars_ver=14.44.35207 >nul
if errorlevel 1 exit /b 1

rem H0-only outputs; this script never references or overwrites T0.0 executables.
cl /nologo /Bv /c /O2 /std:c++17 /arch:AVX2 /fp:precise /EHsc /Brepro /D_WIN32_WINNT=0x0A00 /FAs /Fa"%OUT%h0_scaffold.asm" "%SRC_DIR%\h0_scaffold.cpp" /Fo:"%OUT%h0_scaffold.obj"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /Bv /c /O2 /std:c++17 /arch:AVX2 /fp:precise /EHsc /Brepro /D_WIN32_WINNT=0x0A00 "%SRC_DIR%\h0_measure.cpp" /Fo:"%OUT%h0_measure.obj"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /Bv /c /O2 /std:c++17 /arch:AVX2 /fp:precise /EHsc /Brepro /D_WIN32_WINNT=0x0A00 "%SRC_DIR%\h0_scaffold_main.cpp" /Fo:"%OUT%h0_scaffold_main.obj"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /Bv /c /O2 /std:c++17 /arch:AVX2 /fp:precise /EHsc /Brepro /D_WIN32_WINNT=0x0A00 "%SRC_DIR%\q4_kernel.cpp" /Fo:"%OUT%h0_q4_kernel.obj"
if errorlevel 1 exit /b %errorlevel%
link /Brepro /INCREMENTAL:NO "%OUT%h0_scaffold.obj" "%OUT%h0_measure.obj" "%OUT%h0_scaffold_main.obj" "%OUT%h0_q4_kernel.obj" /OUT:"%OUT%h0_scaffold.exe"
if errorlevel 1 exit /b %errorlevel%

cl /nologo /Bv /c /O2 /std:c++17 /arch:AVX2 /fp:precise /EHsc /Brepro /D_WIN32_WINNT=0x0A00 /DT0_Q4_QA "%SRC_DIR%\q4_kernel.cpp" /Fo:"%OUT%h0_q4_kernel_qa.obj"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /Bv /c /O2 /std:c++17 /arch:AVX2 /fp:precise /EHsc /Brepro /D_WIN32_WINNT=0x0A00 /DT0_Q4_QA "%SRC_DIR%\h0_qa.cpp" /Fo:"%OUT%h0_qa.obj"
if errorlevel 1 exit /b %errorlevel%
link /Brepro /INCREMENTAL:NO "%OUT%h0_scaffold.obj" "%OUT%h0_qa.obj" "%OUT%h0_q4_kernel_qa.obj" /OUT:"%OUT%h0_qa.exe"
exit /b %errorlevel%
