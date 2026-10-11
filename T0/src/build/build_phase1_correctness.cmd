@echo off
setlocal
set "SRC_DIR=%~dp0.."
set "OUT=%~dp0"
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 -vcvars_ver=14.44.35207 >nul
if errorlevel 1 exit /b 1
ml64 /nologo /c /Fo "%OUT%phase1_instr_qa_x64.obj" "%SRC_DIR%\phase1_instr_x64.asm"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /Bv /c /O2 /std:c++17 /arch:AVX2 /fp:precise /EHsc /Brepro /D_WIN32_WINNT=0x0A00 /DT0_Q4_QA "%SRC_DIR%\q4_kernel.cpp" /Fo:"%OUT%\phase1_q4_kernel_qa.obj"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /Bv /c /O2 /std:c++17 /arch:AVX2 /fp:precise /EHsc /Brepro /D_WIN32_WINNT=0x0A00 /DT0_Q4_QA "%SRC_DIR%\phase1_correctness.cpp" /Fo:"%OUT%\phase1_correctness.obj"
if errorlevel 1 exit /b %errorlevel%
link /Brepro /INCREMENTAL:NO "%OUT%\phase1_q4_kernel_qa.obj" "%OUT%\phase1_correctness.obj" "%OUT%phase1_instr_qa_x64.obj" /OUT:"%OUT%\phase1_correctness.exe"
exit /b %errorlevel%
