@echo off
setlocal
set "SRC_DIR=%~dp0.."
set "OUT=%~dp0"
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 -vcvars_ver=14.44.35207 >nul
if errorlevel 1 exit /b 1
ml64 /nologo /c /Fo "%OUT%phase1_instr_x64.obj" "%SRC_DIR%\phase1_instr_x64.asm"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /Bv /c /O2 /std:c++17 /arch:AVX2 /fp:precise /EHsc /Brepro /D_WIN32_WINNT=0x0A00 /FAs /Fa"%OUT%phase1_q4_kernel.asm" "%SRC_DIR%\q4_kernel.cpp" /Fo:"%OUT%\phase1_q4_kernel.obj"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /Bv /c /O2 /std:c++17 /arch:AVX2 /fp:precise /EHsc /Brepro /D_WIN32_WINNT=0x0A00 /FAs /Fa"%OUT%phase1_measure_harness.asm" "%SRC_DIR%\phase1_measure_harness.cpp" /Fo:"%OUT%\phase1_measure_harness.obj"
if errorlevel 1 exit /b %errorlevel%
link /Brepro /INCREMENTAL:NO /OPT:REF "%OUT%\phase1_q4_kernel.obj" "%OUT%\phase1_measure_harness.obj" "%OUT%\phase1_instr_x64.obj" /OUT:"%OUT%\phase1_measure_harness.exe"
exit /b %errorlevel%
