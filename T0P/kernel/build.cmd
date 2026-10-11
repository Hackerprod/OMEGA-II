@echo off
setlocal
set "ROOT=%~dp0..\.."
set "HERE=%~dp0"
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "DATA=%ROOT%\T0P\common\data"
if not exist "%HERE%obj" mkdir "%HERE%obj"
if not exist "%HERE%logs" mkdir "%HERE%logs"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 -vcvars_ver=14.44.35207 >nul
if errorlevel 1 exit /b 1

cl /nologo /c /O2 /std:c++17 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 /I"%ROOT%\T0P\common" "%HERE%qa_main.cpp" /Fo:"%HERE%obj\qa_main.obj" > "%HERE%logs\build.log" 2>&1
if errorlevel 1 (type "%HERE%logs\build.log" & exit /b 1)
cl /nologo /c /O2 /std:c++17 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 /I"%ROOT%\T0P\common" "%ROOT%\T0P\common\common.cpp" /Fo:"%HERE%obj\common.obj" >> "%HERE%logs\build.log" 2>&1
if errorlevel 1 (type "%HERE%logs\build.log" & exit /b 1)
cl /nologo /c /O2 /std:c++17 /EHsc /MD /DNDEBUG /fp:precise /arch:AVX2 /D__AVXVNNI__ /D_WIN32_WINNT=0x0A00 /I"%ROOT%\T0P\common" "%HERE%q4_kernel.cpp" /Fo:"%HERE%obj\q4_kernel.obj" >> "%HERE%logs\build.log" 2>&1
if errorlevel 1 (type "%HERE%logs\build.log" & exit /b 1)
link /INCREMENTAL:NO /OPT:REF "%HERE%obj\qa_main.obj" "%HERE%obj\common.obj" "%HERE%obj\q4_kernel.obj" bcrypt.lib psapi.lib advapi32.lib /OUT:"%HERE%qa_kernel.exe" >> "%HERE%logs\build.log" 2>&1
if errorlevel 1 (type "%HERE%logs\build.log" & exit /b 1)

dumpbin /disasm "%HERE%obj\q4_kernel.obj" > "%HERE%logs\q4_kernel_disasm.txt"
if errorlevel 1 exit /b 1
python "%HERE%inspect_disasm.py" "%HERE%logs\q4_kernel_disasm.txt" "%HERE%logs\STATIC_INSTRUCTION_COUNTS.md" > "%HERE%logs\static_counts.txt" 2>&1
if errorlevel 1 (type "%HERE%logs\static_counts.txt" & exit /b 1)
pushd "%HERE%"
"%HERE%qa_kernel.exe" "%DATA%" > "logs\qa_kernel.txt" 2>&1
set "QA_RC=%ERRORLEVEL%"
python qa_kernel_gate.py "logs\qa_kernel.txt" "PAGE_EXPECTED_KERNEL.json" > "logs\qa_gate.txt" 2>&1
set "GATE_RC=%ERRORLEVEL%"
popd
type "%HERE%logs\qa_kernel.txt"
type "%HERE%logs\qa_gate.txt"
if not "%QA_RC%"=="0" exit /b %QA_RC%
if not "%GATE_RC%"=="0" exit /b %GATE_RC%
exit /b 0
