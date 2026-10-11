@echo off
setlocal
set "ROOT=%~dp0.."
set "TORCH=C:\Projects\OMEGA\.venv_cuda\Lib\site-packages\torch"
set "SRC=C:\Projects\OMEGA\Ejercise\T0\src"
set "COMMON=C:\Projects\OMEGA\Ejercise\T0P\common"
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
if not exist "%ROOT%\adapter\obj" mkdir "%ROOT%\adapter\obj"
if not exist "%COMMON%\obj" mkdir "%COMMON%\obj"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 -vcvars_ver=14.44.35207 >nul
if errorlevel 1 exit /b 1
cl /nologo /c /O2 /std:c++17 /arch:AVX2 /fp:strict /EHsc /MD /DNDEBUG /D_WIN32_WINNT=0x0A00 /I"%COMMON%" /I"%TORCH%\include" /I"%TORCH%\include\torch\csrc\api\include" "%ROOT%\adapter\ref_torch.cpp" /Fo:"%ROOT%\adapter\obj\ref_torch.obj"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /c /O2 /std:c++17 /arch:AVX2 /fp:strict /EHsc /MD /DNDEBUG /D_WIN32_WINNT=0x0A00 /I"%COMMON%" "%COMMON%\common.cpp" /Fo:"%COMMON%\obj\common.obj"
if errorlevel 1 exit /b %errorlevel%
link /INCREMENTAL:NO "%ROOT%\adapter\obj\ref_torch.obj" "%COMMON%\obj\common.obj" /LIBPATH:"%TORCH%\lib" torch.lib torch_cpu.lib c10.lib Psapi.lib Bcrypt.lib Advapi32.lib /OUT:"%ROOT%\adapter\ref_torch.exe"
exit /b %errorlevel%
