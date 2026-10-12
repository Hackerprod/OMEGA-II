@echo off
setlocal
set "HERE=%~dp0"
for %%I in ("%HERE%..\..") do set "ROOT=%%~fI"
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "TORCH=C:\Projects\OMEGA\.venv_cuda\Lib\site-packages\torch"
set "GG=%ROOT%\T0P\ref_ggml\llama.cpp-0.6.0\ggml"
set "GB=%ROOT%\T0P\ref_ggml\build_llama\ggml\src"
set "PATH=%ROOT%\T0P\ref_ggml\build_llama\bin;%TORCH%\lib;%PATH%"
if not exist "%HERE%obj" mkdir "%HERE%obj"
if not exist "%HERE%obj\tmp" mkdir "%HERE%obj\tmp"
if not exist "%HERE%logs" mkdir "%HERE%logs"
set "TEMP=%HERE%obj\tmp"
set "TMP=%HERE%obj\tmp"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 -vcvars_ver=14.44.35207 >nul
if errorlevel 1 (echo BLOCKED: vcvars64.bat failed & exit /b 1)
set "INC=/I"%ROOT%\T0P\common" /I"%ROOT%\T0P\kernel" /I"%GG%\include" /I"%GG%\src" /I"%GG%\src\ggml-cpu" /I"%TORCH%\include" /I"%TORCH%\include\torch\csrc\api\include""
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%ROOT%\T0P\common\common.cpp" /Fo:"%HERE%obj\common.obj" > "%HERE%logs\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:precise /arch:AVX2 /D__AVXVNNI__ /D_WIN32_WINNT=0x0A00 %INC% "%ROOT%\T0P\kernel\q4_kernel.cpp" /Fo:"%HERE%obj\q4.obj" >> "%HERE%logs\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%HERE%main.cpp" /Fo:"%HERE%obj\main.obj" >> "%HERE%logs\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%HERE%provider_omega.cpp" /Fo:"%HERE%obj\omega.obj" >> "%HERE%logs\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%HERE%provider_ggml.cpp" /Fo:"%HERE%obj\ggml_provider.obj" >> "%HERE%logs\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /Gy /O2 /std:c++20 /EHsc /MD /DNDEBUG /arch:AVX2 /DGGML_AVX2 /DGGML_FMA /DGGML_F16C /DGGML_BMI2 /D__BMI2__ /D__AVXVNNI__ /D_WIN32_WINNT=0x0A00 /I"%GG%\include" /I"%GG%\src" /I"%GG%\src\ggml-cpu" "%GG%\src\ggml-cpu\arch\x86\repack.cpp" /Fo:"%HERE%obj\repack.obj" >> "%HERE%logs\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%HERE%provider_torch.cpp" /Fo:"%HERE%obj\torch_provider.obj" >> "%HERE%logs\build.log" 2>&1
if errorlevel 1 goto fail
link /INCREMENTAL:NO /OPT:REF "%HERE%obj\main.obj" "%HERE%obj\omega.obj" "%HERE%obj\ggml_provider.obj" "%HERE%obj\torch_provider.obj" "%HERE%obj\common.obj" "%HERE%obj\q4.obj" "%HERE%obj\repack.obj" "%GB%\ggml.lib" "%GB%\ggml-cpu.lib" "%GB%\ggml-base.lib" /LIBPATH:"%TORCH%\lib" torch.lib torch_cpu.lib c10.lib bcrypt.lib psapi.lib advapi32.lib /OUT:"%HERE%harness.exe" >> "%HERE%logs\build.log" 2>&1
if errorlevel 1 goto fail
powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%run_smoke.ps1" -Executable "%HERE%harness.exe" > "%HERE%logs\smoke_runner.stdout.txt" 2> "%HERE%logs\smoke_runner.stderr.txt"
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (type "%HERE%logs\smoke_runner.stdout.txt" & type "%HERE%logs\smoke_runner.stderr.txt" & exit /b %RC%)
type "%HERE%logs\smoke_runner.stdout.txt"
powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%hashes.ps1"
if errorlevel 1 exit /b 1
echo HARNESS_BUILD_AND_SMOKE PASS
exit /b 0
:fail
type "%HERE%logs\build.log"
exit /b 1
