@echo off
setlocal
set "HERE=%~dp0"
for %%I in ("%HERE%..\..") do set "ROOT=%%~fI"
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "GG=%ROOT%\T0P\ref_ggml\llama.cpp-0.6.0\ggml"
set "GG_BUILD=%ROOT%\T0P\ref_ggml\build_llama\ggml\src"
set "TORCH=C:\Projects\OMEGA\.venv_cuda\Lib\site-packages\torch"
set "OBJ=%HERE%logs_r17\obj_r17"
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
if not exist "%OBJ%" mkdir "%OBJ%"
if not exist "%OBJ%\tmp" mkdir "%OBJ%\tmp"
set "TEMP=%OBJ%\tmp"
set "TMP=%OBJ%\tmp"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 -vcvars_ver=14.44.35207 >nul
if errorlevel 1 (echo HARNESS_R17_BUILD REJECT vcvars64_failed & exit /b 1)
set INC=/I%ROOT%\T0P\common /I%ROOT%\T0P\kernel /I%ROOT%\T0P\evict /I%HERE% /I%GG%\include /I%GG%\src /I%GG%\src\ggml-cpu /I%ROOT%\T0P\ref_ggml\llama.cpp-0.6.0\vendor /I%TORCH%\include /I%TORCH%\include\torch\csrc\api\include
set "LOG=%OBJ%\build_r17.log"
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%ROOT%\T0P\common\common.cpp" /Fo:"%OBJ%\common_r17.obj" > "%LOG%" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:precise /arch:AVX2 /D__AVXVNNI__ /D_WIN32_WINNT=0x0A00 %INC% "%ROOT%\T0P\kernel\q4_kernel.cpp" /Fo:"%OBJ%\q4_kernel_r17.obj" >> "%LOG%" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:precise /arch:AVX2 /D__AVXVNNI__ /Dmain=q4_standalone_qa_main_r17 /D_WIN32_WINNT=0x0A00 %INC% "%ROOT%\T0P\kernel\qa_main.cpp" /Fo:"%OBJ%\qa_main_r17.obj" >> "%LOG%" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%HERE%topology_r17.cpp" /Fo:"%OBJ%\topology_r17.obj" >> "%LOG%" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /arch:AVX2 /D__AVXVNNI__ /D_WIN32_WINNT=0x0A00 %INC% "%HERE%residency_r17.cpp" /Fo:"%OBJ%\residency_r17.obj" >> "%LOG%" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%HERE%main_r17.cpp" /Fo:"%OBJ%\main_r17.obj" >> "%LOG%" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /arch:AVX2 /D__AVXVNNI__ /D_WIN32_WINNT=0x0A00 %INC% "%ROOT%\T0P\evict\evict.cpp" /Fo:"%OBJ%\evict_r17.obj" >> "%LOG%" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%HERE%provider_omega_r17.cpp" /Fo:"%OBJ%\provider_omega_r17.obj" >> "%LOG%" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /DGGML_AVX2 /DGGML_FMA /DGGML_F16C /DGGML_BMI2 /D__BMI2__ /D__AVXVNNI__ /arch:AVX2 /D_WIN32_WINNT=0x0A00 %INC% "%HERE%provider_ggml_r17.cpp" /Fo:"%OBJ%\provider_ggml_r17.obj" >> "%LOG%" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /DGGML_AVX2 /DGGML_FMA /DGGML_F16C /DGGML_BMI2 /D__BMI2__ /D__AVXVNNI__ /arch:AVX2 /D_WIN32_WINNT=0x0A00 %INC% "%HERE%provider_ggml_repack_r17.cpp" /Fo:"%OBJ%\provider_ggml_repack_r17.obj" >> "%LOG%" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /DGGML_AVX2 /DGGML_FMA /DGGML_F16C /DGGML_BMI2 /D__BMI2__ /D__AVXVNNI__ /arch:AVX2 /D_WIN32_WINNT=0x0A00 %INC% "%GG%\src\ggml-cpu\arch\x86\repack.cpp" /Fo:"%OBJ%\ggml_repack_r17.obj" >> "%LOG%" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 %INC% "%HERE%provider_torch_r17.cpp" /Fo:"%OBJ%\provider_torch_r17.obj" >> "%LOG%" 2>&1
if errorlevel 1 goto fail
cl /nologo /c /O2 /std:c++20 /EHsc /MD /DNDEBUG /fp:strict /arch:AVX2 /D__AVXVNNI__ /D_WIN32_WINNT=0x0A00 %INC% "%HERE%provider_aux_r17.cpp" /Fo:"%OBJ%\provider_aux_r17.obj" >> "%LOG%" 2>&1
if errorlevel 1 goto fail
link /INCREMENTAL:NO /OPT:REF /MAP:"%OBJ%\harness_r17.map" "%OBJ%\main_r17.obj" "%OBJ%\residency_r17.obj" "%OBJ%\qa_main_r17.obj" "%OBJ%\topology_r17.obj" "%OBJ%\common_r17.obj" "%OBJ%\q4_kernel_r17.obj" "%OBJ%\evict_r17.obj" "%OBJ%\provider_omega_r17.obj" "%OBJ%\provider_ggml_r17.obj" "%OBJ%\provider_ggml_repack_r17.obj" "%OBJ%\ggml_repack_r17.obj" "%OBJ%\provider_torch_r17.obj" "%OBJ%\provider_aux_r17.obj" "%GG_BUILD%\ggml.lib" "%GG_BUILD%\ggml-cpu.lib" "%GG_BUILD%\ggml-base.lib" /LIBPATH:"%TORCH%\lib" torch.lib torch_cpu.lib c10.lib bcrypt.lib advapi32.lib psapi.lib /OUT:"%OBJ%\harness_r17.exe" >> "%LOG%" 2>&1
if errorlevel 1 goto fail
echo HARNESS_R17_BUILD PASS linked=kernel+standalone_D12_QA_same_object+common+topology+residency+eviction+OMEGA+ggml-flat+ggml-repack+Torch+BW_DRAM+P_instr output=%OBJ%\harness_r17.exe
exit /b 0
:fail
type "%LOG%"
echo HARNESS_R17_BUILD REJECT compile_or_link_error
exit /b 1
