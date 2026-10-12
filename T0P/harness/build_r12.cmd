@echo off
setlocal
set "HERE=%~dp0"
for %%I in ("%HERE%..\..") do set "ROOT=%%~fI"
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "TORCH=C:\Projects\OMEGA\.venv_cuda\Lib\site-packages\torch"
set "GG=%ROOT%\T0P\ref_ggml\llama.cpp-0.6.0\ggml"
set "GB=%ROOT%\T0P\ref_ggml\build_llama\ggml\src"
set "PATH=%ROOT%\T0P\ref_ggml\build_llama\bin;%TORCH%\lib;C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
if not exist "%HERE%logs_r12\obj_r12" mkdir "%HERE%logs_r12\obj_r12"
if not exist "%HERE%logs_r12\obj_r12\tmp" mkdir "%HERE%logs_r12\obj_r12\tmp"
if not exist "%HERE%logs_r12" mkdir "%HERE%logs_r12"
set "TEMP=%HERE%logs_r12\obj_r12\tmp"
set "TMP=%HERE%logs_r12\obj_r12\tmp"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 -vcvars_ver=14.44.35207 >nul
if errorlevel 1 (echo BLOCKED: vcvars64.bat failed & exit /b 1)
set "INC=/I"%ROOT%\T0P\common" /I"%ROOT%\T0P\kernel" /I"%GG%\include" /I"%GG%\src" /I"%GG%\src\ggml-cpu" /I"%TORCH%\include" /I"%TORCH%\include\torch\csrc\api\include""
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%ROOT%\T0P\common\common.cpp" /Fo:"%HERE%logs_r12\obj_r12\common.obj" > "%HERE%logs_r12\obj_r12\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:precise /arch:AVX2 /D__AVXVNNI__ /D_WIN32_WINNT=0x0A00 %INC% "%ROOT%\T0P\kernel\q4_kernel.cpp" /Fo:"%HERE%logs_r12\obj_r12\q4.obj" >> "%HERE%logs_r12\obj_r12\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%HERE%main_r12.cpp" /Fo:"%HERE%logs_r12\obj_r12\main.obj" >> "%HERE%logs_r12\obj_r12\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /arch:AVX2 /D__AVXVNNI__ /D_WIN32_WINNT=0x0A00 %INC% "%HERE%residency_r12.cpp" /Fo:"%HERE%logs_r12\obj_r12\residency.obj" >> "%HERE%logs_r12\obj_r12\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%HERE%provider_omega_r12.cpp" /Fo:"%HERE%logs_r12\obj_r12\omega.obj" >> "%HERE%logs_r12\obj_r12\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%HERE%provider_ggml_r12.cpp" /Fo:"%HERE%logs_r12\obj_r12\ggml_provider.obj" >> "%HERE%logs_r12\obj_r12\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%HERE%provider_ggml_repack_r12.cpp" /Fo:"%HERE%logs_r12\obj_r12\ggml_repack_provider.obj" >> "%HERE%logs_r12\obj_r12\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /Gy /O2 /std:c++20 /EHsc /MD /DNDEBUG /arch:AVX2 /DGGML_AVX2 /DGGML_FMA /DGGML_F16C /DGGML_BMI2 /D__BMI2__ /D__AVXVNNI__ /D_WIN32_WINNT=0x0A00 /I"%GG%\include" /I"%GG%\src" /I"%GG%\src\ggml-cpu" "%GG%\src\ggml-cpu\arch\x86\repack.cpp" /Fo:"%HERE%logs_r12\obj_r12\repack.obj" >> "%HERE%logs_r12\obj_r12\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%HERE%provider_torch_r12.cpp" /Fo:"%HERE%logs_r12\obj_r12\torch_provider.obj" >> "%HERE%logs_r12\obj_r12\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /arch:AVX2 /D__AVXVNNI__ /D_WIN32_WINNT=0x0A00 %INC% "%HERE%provider_aux_r12.cpp" /Fo:"%HERE%logs_r12\obj_r12\aux_provider.obj" >> "%HERE%logs_r12\obj_r12\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%HERE%topology_r12.cpp" /Fo:"%HERE%logs_r12\obj_r12\topology.obj" >> "%HERE%logs_r12\obj_r12\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%HERE%large_pages_r12.cpp" /Fo:"%HERE%logs_r12\obj_r12\large_pages.obj" >> "%HERE%logs_r12\obj_r12\build.log" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%HERE%smoke_r12.cpp" /Fo:"%HERE%logs_r12\obj_r12\smoke.obj" >> "%HERE%logs_r12\obj_r12\build.log" 2>&1
if errorlevel 1 goto fail
link /INCREMENTAL:NO /OPT:REF "%HERE%logs_r12\obj_r12\main.obj" "%HERE%logs_r12\obj_r12\smoke.obj" "%HERE%logs_r12\obj_r12\large_pages.obj" "%HERE%logs_r12\obj_r12\topology.obj" "%HERE%logs_r12\obj_r12\residency.obj" "%HERE%logs_r12\obj_r12\omega.obj" "%HERE%logs_r12\obj_r12\ggml_provider.obj" "%HERE%logs_r12\obj_r12\ggml_repack_provider.obj" "%HERE%logs_r12\obj_r12\torch_provider.obj" "%HERE%logs_r12\obj_r12\aux_provider.obj" "%HERE%logs_r12\obj_r12\common.obj" "%HERE%logs_r12\obj_r12\q4.obj" "%HERE%logs_r12\obj_r12\repack.obj" "%GB%\ggml.lib" "%GB%\ggml-cpu.lib" "%GB%\ggml-base.lib" /LIBPATH:"%TORCH%\lib" torch.lib torch_cpu.lib c10.lib bcrypt.lib psapi.lib advapi32.lib /IMPLIB:"%HERE%logs_r12\obj_r12\harness_r12.lib" /MAP:"%HERE%logs_r12\obj_r12\harness_r12.map" /OUT:"%HERE%logs_r12\obj_r12\harness_r12.exe" >> "%HERE%logs_r12\obj_r12\build.log" 2>&1
if errorlevel 1 goto fail
echo HARNESS_R12_BUILD PASS; SMOKE uses new D13 attempt directories and never qualifies from its metrics
exit /b 0
:fail
type "%HERE%logs_r12\obj_r12\build.log"
exit /b 1


