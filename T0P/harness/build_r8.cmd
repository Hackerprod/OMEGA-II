@echo off
setlocal
set "HERE=%~dp0"
for %%I in ("%HERE%..\..") do set "ROOT=%%~fI"
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "TORCH=C:\Projects\OMEGA\.venv_cuda\Lib\site-packages\torch"
set "GG=%ROOT%\T0P\ref_ggml\llama.cpp-0.6.0\ggml"
set "GB=%ROOT%\T0P\ref_ggml\build_llama\ggml\src"
set "PATH=%ROOT%\T0P\ref_ggml\build_llama\bin;%TORCH%\lib;%PATH%"
if not exist "%HERE%obj_r8" mkdir "%HERE%obj_r8"
if not exist "%HERE%obj_r8\tmp" mkdir "%HERE%obj_r8\tmp"
if not exist "%HERE%logs_r8" mkdir "%HERE%logs_r8"
set "TEMP=%HERE%obj_r8\tmp"
set "TMP=%HERE%obj_r8\tmp"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 -vcvars_ver=14.44.35207 >nul
if errorlevel 1 (echo BLOCKED: vcvars64.bat failed & exit /b 1)
set "INC=/I"%ROOT%\T0P\common" /I"%ROOT%\T0P\kernel" /I"%GG%\include" /I"%GG%\src" /I"%GG%\src\ggml-cpu" /I"%TORCH%\include" /I"%TORCH%\include\torch\csrc\api\include""
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%ROOT%\T0P\common\common.cpp" /Fo:"%HERE%obj_r8\common.obj" > "%HERE%logs_r8\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:precise /arch:AVX2 /D__AVXVNNI__ /D_WIN32_WINNT=0x0A00 %INC% "%ROOT%\T0P\kernel\q4_kernel.cpp" /Fo:"%HERE%obj_r8\q4.obj" >> "%HERE%logs_r8\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%HERE%main_r8.cpp" /Fo:"%HERE%obj_r8\main.obj" >> "%HERE%logs_r8\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%HERE%provider_omega_r8.cpp" /Fo:"%HERE%obj_r8\omega.obj" >> "%HERE%logs_r8\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%HERE%provider_ggml_r8.cpp" /Fo:"%HERE%obj_r8\ggml_provider.obj" >> "%HERE%logs_r8\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /Gy /O2 /std:c++20 /EHsc /MD /DNDEBUG /arch:AVX2 /DGGML_AVX2 /DGGML_FMA /DGGML_F16C /DGGML_BMI2 /D__BMI2__ /D__AVXVNNI__ /D_WIN32_WINNT=0x0A00 /I"%GG%\include" /I"%GG%\src" /I"%GG%\src\ggml-cpu" "%GG%\src\ggml-cpu\arch\x86\repack.cpp" /Fo:"%HERE%obj_r8\repack.obj" >> "%HERE%logs_r8\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%HERE%provider_torch_r8.cpp" /Fo:"%HERE%obj_r8\torch_provider.obj" >> "%HERE%logs_r8\build.log" 2>&1
if errorlevel 1 goto fail
link /INCREMENTAL:NO /OPT:REF "%HERE%obj_r8\main.obj" "%HERE%obj_r8\omega.obj" "%HERE%obj_r8\ggml_provider.obj" "%HERE%obj_r8\torch_provider.obj" "%HERE%obj_r8\common.obj" "%HERE%obj_r8\q4.obj" "%HERE%obj_r8\repack.obj" "%GB%\ggml.lib" "%GB%\ggml-cpu.lib" "%GB%\ggml-base.lib" /LIBPATH:"%TORCH%\lib" torch.lib torch_cpu.lib c10.lib bcrypt.lib psapi.lib advapi32.lib /OUT:"%HERE%harness_r8.exe" >> "%HERE%logs_r8\build.log" 2>&1
if errorlevel 1 goto fail
powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%preflight_r8.ps1" > "%HERE%logs_r8\preflight.stdout.txt" 2> "%HERE%logs_r8\preflight.stderr.txt"
if errorlevel 1 goto fail
powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%run_smoke_r8.ps1" -Executable "%HERE%harness_r8.exe" > "%HERE%logs_r8\smoke_runner.stdout.txt" 2> "%HERE%logs_r8\smoke_runner.stderr.txt"
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (type "%HERE%logs_r8\smoke_runner.stdout.txt" & type "%HERE%logs_r8\smoke_runner.stderr.txt" & exit /b %RC%)
type "%HERE%logs_r8\smoke_runner.stdout.txt"
if exist "%HERE%obj_r8\tmp\*" del /s /q "%HERE%obj_r8\tmp\*" >nul 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%hashes_r8.ps1"
if errorlevel 1 exit /b 1
echo HARNESS_BUILD_AND_SMOKE PASS
exit /b 0
:fail
type "%HERE%logs_r8\build.log"
exit /b 1


