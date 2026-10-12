@echo off
setlocal
set "HERE=%~dp0"
set "ROOT=%HERE%..\.."
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 -vcvars_ver=14.44.35207 >nul
if errorlevel 1 exit /b 1
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:precise /arch:AVX2 /D__AVXVNNI__ /D_WIN32_WINNT=0x0A00 /I"%ROOT%\T0P\common" /I"%ROOT%\T0P\kernel" "%HERE%q4_kernel.cpp" /Fo:"%HERE%obj\q4_kernel_harnessflags.obj" > "%HERE%logs\compile_harness_flags_r4.log" 2>&1
if errorlevel 1 (type "%HERE%logs\compile_harness_flags_r4.log" & exit /b 1)
exit /b 0
