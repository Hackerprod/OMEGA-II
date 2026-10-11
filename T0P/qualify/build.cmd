@echo off
setlocal
set "HERE=%~dp0"
for %%I in ("%HERE%..\..") do set "ROOT=%%~fI"
set "COMMON=%ROOT%\T0P\common"
set "GGROOT=%ROOT%\T0P\ref_ggml"
set "GG=%GGROOT%\llama.cpp-0.6.0\ggml"
set "GGBUILD=%GGROOT%\build_llama"
set "TORCH=C:\Projects\OMEGA\.venv_cuda\Lib\site-packages\torch"
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "TEMP=%HERE%obj\tmp"
set "TMP=%TEMP%"
if not exist "%HERE%obj" mkdir "%HERE%obj"
if not exist "%TEMP%" mkdir "%TEMP%"
if not exist "%HERE%logs" mkdir "%HERE%logs"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 -vcvars_ver=14.44.35207 >nul
if errorlevel 1 exit /b 1

cl /nologo /c /O2 /std:c++17 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 /I"%COMMON%" "%COMMON%\common.cpp" /Fo:"%HERE%obj\common.obj" > "%HERE%logs\build.log" 2>&1
if errorlevel 1 goto :fail
cl /nologo /c /O2 /std:c++17 /EHsc /MD /DNDEBUG /fp:precise /arch:AVX2 /D__AVXVNNI__ /D_WIN32_WINNT=0x0A00 /I"%COMMON%" "%ROOT%\T0P\kernel\q4_kernel.cpp" /Fo:"%HERE%obj\q4_kernel.obj" >> "%HERE%logs\build.log" 2>&1
if errorlevel 1 goto :fail
cl /nologo /c /O2 /std:c++17 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 /I"%COMMON%" "%ROOT%\T0P\kernel\qa_main.cpp" /Fo:"%HERE%obj\qa_main.obj" >> "%HERE%logs\build.log" 2>&1
if errorlevel 1 goto :fail
link /INCREMENTAL:NO /OPT:REF "%HERE%obj\qa_main.obj" "%HERE%obj\common.obj" "%HERE%obj\q4_kernel.obj" bcrypt.lib psapi.lib advapi32.lib /OUT:"%HERE%qualify_kernel_qa.exe" >> "%HERE%logs\build.log" 2>&1
if errorlevel 1 goto :fail
cl /nologo /c /O2 /std:c++17 /EHsc /MD /DNDEBUG /fp:strict /arch:AVX2 /D__AVXVNNI__ /D_WIN32_WINNT=0x0A00 /I"%COMMON%" "%HERE%qualify_kernel_controls.cpp" /Fo:"%HERE%obj\kernel_controls.obj" >> "%HERE%logs\build.log" 2>&1
if errorlevel 1 goto :fail
link /INCREMENTAL:NO /OPT:REF "%HERE%obj\kernel_controls.obj" "%HERE%obj\common.obj" "%HERE%obj\q4_kernel.obj" bcrypt.lib psapi.lib advapi32.lib /OUT:"%HERE%qualify_kernel_controls.exe" >> "%HERE%logs\build.log" 2>&1
if errorlevel 1 goto :fail

cl /nologo /c /O2 /std:c++17 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 /I"%COMMON%" /I"%GG%\include" /I"%GG%\src" /I"%GG%\src\ggml-cpu" "%HERE%qualify_ggml_bridge.cpp" /Fo:"%HERE%obj\ggml_main.obj" >> "%HERE%logs\build.log" 2>&1
if errorlevel 1 goto :fail
cl /nologo /c /Gy /O2 /std:c++17 /EHsc /MD /DNDEBUG /D_WIN32_WINNT=0x0A00 /arch:AVX2 /DGGML_AVX2 /DGGML_FMA /DGGML_F16C /DGGML_BMI2 /D__BMI2__ /D__AVXVNNI__ /I"%GG%\include" /I"%GG%\src" /I"%GG%\src\ggml-cpu" "%GG%\src\ggml-cpu\arch\x86\repack.cpp" /Fo:"%HERE%obj\repack_x86.obj" >> "%HERE%logs\build.log" 2>&1
if errorlevel 1 goto :fail
link /INCREMENTAL:NO /OPT:REF "%HERE%obj\ggml_main.obj" "%HERE%obj\common.obj" "%HERE%obj\repack_x86.obj" "%GGBUILD%\ggml\src\ggml.lib" "%GGBUILD%\ggml\src\ggml-cpu.lib" "%GGBUILD%\ggml\src\ggml-base.lib" bcrypt.lib psapi.lib advapi32.lib /OUT:"%HERE%qualify_ggml_qa.exe" >> "%HERE%logs\build.log" 2>&1
if errorlevel 1 goto :fail

cl /nologo /c /O2 /std:c++17 /arch:AVX2 /fp:strict /EHsc /MD /DNDEBUG /D_WIN32_WINNT=0x0A00 /I"%COMMON%" /I"%TORCH%\include" /I"%TORCH%\include\torch\csrc\api\include" "%HERE%qualify_torch_bridge.cpp" /Fo:"%HERE%obj\torch_main.obj" >> "%HERE%logs\build.log" 2>&1
if errorlevel 1 goto :fail
link /INCREMENTAL:NO "%HERE%obj\torch_main.obj" "%HERE%obj\common.obj" /LIBPATH:"%TORCH%\lib" torch.lib torch_cpu.lib c10.lib Psapi.lib Bcrypt.lib Advapi32.lib /OUT:"%HERE%qualify_torch_qa.exe" >> "%HERE%logs\build.log" 2>&1
if errorlevel 1 goto :fail

cl /nologo /c /O2 /std:c++17 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 "%HERE%qualify.cpp" /Fo:"%HERE%obj\qualify.obj" >> "%HERE%logs\build.log" 2>&1
if errorlevel 1 goto :fail
link /INCREMENTAL:NO "%HERE%obj\qualify.obj" bcrypt.lib advapi32.lib /OUT:"%HERE%qualify.exe" >> "%HERE%logs\build.log" 2>&1
if errorlevel 1 goto :fail

"%HERE%qualify.exe" --self-test > "%HERE%logs\self_test_build.log" 2>&1
if errorlevel 1 goto :fail

echo BUILD_AND_SYNTHETIC_SELF_TEST PASS
exit /b 0
:fail
type "%HERE%logs\build.log"
exit /b 1
