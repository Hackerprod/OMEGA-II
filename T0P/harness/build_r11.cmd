@echo off
setlocal
set "HERE=%~dp0"
for %%I in ("%HERE%..\..") do set "ROOT=%%~fI"
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "TORCH=C:\Projects\OMEGA\.venv_cuda\Lib\site-packages\torch"
set "GG=%ROOT%\T0P\ref_ggml\llama.cpp-0.6.0\ggml"
set "GB=%ROOT%\T0P\ref_ggml\build_llama\ggml\src"
set "PATH=%ROOT%\T0P\ref_ggml\build_llama\bin;%TORCH%\lib;C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
if not exist "%HERE%obj_r11" mkdir "%HERE%obj_r11"
if not exist "%HERE%obj_r11\tmp" mkdir "%HERE%obj_r11\tmp"
if not exist "%HERE%logs_r11" mkdir "%HERE%logs_r11"
set "TEMP=%HERE%obj_r11\tmp"
set "TMP=%HERE%obj_r11\tmp"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 -vcvars_ver=14.44.35207 >nul
if errorlevel 1 (echo BLOCKED: vcvars64.bat failed & exit /b 1)
set "INC=/I"%ROOT%\T0P\common" /I"%ROOT%\T0P\kernel" /I"%GG%\include" /I"%GG%\src" /I"%GG%\src\ggml-cpu" /I"%TORCH%\include" /I"%TORCH%\include\torch\csrc\api\include""
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%ROOT%\T0P\common\common.cpp" /Fo:"%HERE%obj_r11\common.obj" > "%HERE%logs_r11\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:precise /arch:AVX2 /D__AVXVNNI__ /D_WIN32_WINNT=0x0A00 %INC% "%ROOT%\T0P\kernel\q4_kernel.cpp" /Fo:"%HERE%obj_r11\q4.obj" >> "%HERE%logs_r11\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%HERE%main_r11.cpp" /Fo:"%HERE%obj_r11\main.obj" >> "%HERE%logs_r11\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /arch:AVX2 /D__AVXVNNI__ /D_WIN32_WINNT=0x0A00 %INC% "%HERE%residency_r11.cpp" /Fo:"%HERE%obj_r11\residency.obj" >> "%HERE%logs_r11\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%HERE%provider_omega_r11.cpp" /Fo:"%HERE%obj_r11\omega.obj" >> "%HERE%logs_r11\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%HERE%provider_ggml_r11.cpp" /Fo:"%HERE%obj_r11\ggml_provider.obj" >> "%HERE%logs_r11\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%HERE%provider_ggml_repack_r11.cpp" /Fo:"%HERE%obj_r11\ggml_repack_provider.obj" >> "%HERE%logs_r11\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /Gy /O2 /std:c++20 /EHsc /MD /DNDEBUG /arch:AVX2 /DGGML_AVX2 /DGGML_FMA /DGGML_F16C /DGGML_BMI2 /D__BMI2__ /D__AVXVNNI__ /D_WIN32_WINNT=0x0A00 /I"%GG%\include" /I"%GG%\src" /I"%GG%\src\ggml-cpu" "%GG%\src\ggml-cpu\arch\x86\repack.cpp" /Fo:"%HERE%obj_r11\repack.obj" >> "%HERE%logs_r11\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%HERE%provider_torch_r11.cpp" /Fo:"%HERE%obj_r11\torch_provider.obj" >> "%HERE%logs_r11\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /arch:AVX2 /D__AVXVNNI__ /D_WIN32_WINNT=0x0A00 %INC% "%HERE%provider_aux_r11.cpp" /Fo:"%HERE%obj_r11\aux_provider.obj" >> "%HERE%logs_r11\build.log" 2>&1
if errorlevel 1 goto fail
link /INCREMENTAL:NO /OPT:REF "%HERE%obj_r11\main.obj" "%HERE%obj_r11\residency.obj" "%HERE%obj_r11\omega.obj" "%HERE%obj_r11\ggml_provider.obj" "%HERE%obj_r11\ggml_repack_provider.obj" "%HERE%obj_r11\torch_provider.obj" "%HERE%obj_r11\aux_provider.obj" "%HERE%obj_r11\common.obj" "%HERE%obj_r11\q4.obj" "%HERE%obj_r11\repack.obj" "%GB%\ggml.lib" "%GB%\ggml-cpu.lib" "%GB%\ggml-base.lib" /LIBPATH:"%TORCH%\lib" torch.lib torch_cpu.lib c10.lib bcrypt.lib psapi.lib advapi32.lib /IMPLIB:"%HERE%obj_r11\harness_r11.lib" /OUT:"%HERE%harness_r11.exe" >> "%HERE%logs_r11\build.log" 2>&1
if errorlevel 1 goto fail
echo HARNESS_R11_BUILD PASS; SMOKE is launched by official_run_r11.py --smoke-real
exit /b 0
:fail
type "%HERE%logs_r11\build.log"
exit /b 1


