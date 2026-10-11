@echo off
setlocal
set "COMMON_DIR=%~dp0"
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
if not exist "%COMMON_DIR%obj" mkdir "%COMMON_DIR%obj"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 -vcvars_ver=14.44.35207 >nul
if errorlevel 1 exit /b 1
cl /nologo /c /O2 /std:c++17 /EHsc /MD /DNDEBUG /D_WIN32_WINNT=0x0A00 /I"%COMMON_DIR%." "%COMMON_DIR%common.cpp" /Fo:"%COMMON_DIR%obj\common.obj"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /c /O2 /std:c++17 /EHsc /MD /DNDEBUG /D_WIN32_WINNT=0x0A00 /I"%COMMON_DIR%." "%COMMON_DIR%generate_main.cpp" /Fo:"%COMMON_DIR%obj\generate_main.obj"
if errorlevel 1 exit /b %errorlevel%
link /INCREMENTAL:NO "%COMMON_DIR%obj\common.obj" "%COMMON_DIR%obj\generate_main.obj" /OUT:"%COMMON_DIR%generate_cells.exe"
exit /b %errorlevel%
