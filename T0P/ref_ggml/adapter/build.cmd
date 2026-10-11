@echo off
setlocal
set "ROOT=%~dp0..\..\.."
set "HERE=%~dp0"
set "GG=%~dp0..\llama.cpp-0.6.0\ggml"
set "BUILD=%~dp0..\build_llama"
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
if not exist "%HERE%obj" mkdir "%HERE%obj"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 -vcvars_ver=14.44.35207 >nul
if errorlevel 1 exit /b 1
cl /nologo /c /O2 /std:c++17 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 /I"%ROOT%\T0P\common" /I"%GG%\include" /I"%GG%\src" /I"%GG%\src\ggml-cpu" "%HERE%main.cpp" /Fo:"%HERE%obj\main.obj"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /c /O2 /std:c++17 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 /I"%ROOT%\T0P\common" "%ROOT%\T0P\common\common.cpp" /Fo:"%HERE%obj\common.obj"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /c /Gy /O2 /std:c++17 /EHsc /MD /DNDEBUG /D_WIN32_WINNT=0x0A00 /arch:AVX2 /DGGML_AVX2 /DGGML_FMA /DGGML_F16C /DGGML_BMI2 /D__BMI2__ /D__AVXVNNI__ /I"%GG%\include" /I"%GG%\src" /I"%GG%\src\ggml-cpu" "%GG%\src\ggml-cpu\arch\x86\repack.cpp" /Fo:"%HERE%obj\repack_x86.obj"
if errorlevel 1 exit /b %errorlevel%
link /INCREMENTAL:NO /OPT:REF "%HERE%obj\main.obj" "%HERE%obj\common.obj" "%HERE%obj\repack_x86.obj" "%BUILD%\ggml\src\ggml.lib" "%BUILD%\ggml\src\ggml-cpu.lib" "%BUILD%\ggml\src\ggml-base.lib" bcrypt.lib psapi.lib advapi32.lib /OUT:"%HERE%adapter.exe"
exit /b %errorlevel%
