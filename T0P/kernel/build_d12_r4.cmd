@echo off
setlocal
set "HERE=%~dp0"
for %%I in ("%HERE%..\..") do set "ROOT=%%~fI"
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "DATA=%ROOT%\T0P\common\data"
set "Q4_OBJECT=%~1"
if "%Q4_OBJECT%"=="" set "Q4_OBJECT=%HERE%obj\q4_kernel.obj"
if not exist "%Q4_OBJECT%" (echo KERNEL_OBJECT_MISSING=%Q4_OBJECT% & exit /b 2)
if not exist "%HERE%obj" mkdir "%HERE%obj"
if not exist "%HERE%logs" mkdir "%HERE%logs"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 -vcvars_ver=14.44.35207 >nul
if errorlevel 1 exit /b 1
cl /nologo /c /O2 /std:c++17 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 /I"%ROOT%\T0P\common" "%HERE%qa_main.cpp" /Fo:"%HERE%obj\qa_main.obj" > "%HERE%logs\build_d12_r4.log" 2>&1
if errorlevel 1 (type "%HERE%logs\build_d12_r4.log" & exit /b 1)
cl /nologo /c /O2 /std:c++17 /EHsc /MD /DNDEBUG /fp:strict /D_WIN32_WINNT=0x0A00 /I"%ROOT%\T0P\common" "%ROOT%\T0P\common\common.cpp" /Fo:"%HERE%obj\common.obj" >> "%HERE%logs\build_d12_r4.log" 2>&1
if errorlevel 1 (type "%HERE%logs\build_d12_r4.log" & exit /b 1)
cl /nologo /c /O2 /std:c++17 /EHsc /MD /DNDEBUG /fp:precise /arch:AVX2 /D__AVXVNNI__ /D_WIN32_WINNT=0x0A00 /I"%ROOT%\T0P\common" "%HERE%q4_kernel.cpp" /Fo:"%HERE%obj\q4_kernel.obj" >> "%HERE%logs\build_d12_r4.log" 2>&1
if errorlevel 1 (type "%HERE%logs\build_d12_r4.log" & exit /b 1)
link /INCREMENTAL:NO /OPT:REF "%HERE%obj\qa_main.obj" "%HERE%obj\common.obj" "%Q4_OBJECT%" bcrypt.lib psapi.lib advapi32.lib /OUT:"%HERE%qa_kernel.exe" >> "%HERE%logs\build_d12_r4.log" 2>&1
if errorlevel 1 (type "%HERE%logs\build_d12_r4.log" & exit /b 1)
echo QA_LINK_KERNEL_OBJECT=%Q4_OBJECT% > "%HERE%logs\qa_kernel_link_object_d12_r4.txt"
dumpbin /disasm "%HERE%obj\q4_kernel.obj" > "%HERE%logs\q4_kernel_disasm_d12_r4.txt"
if errorlevel 1 exit /b 1
python "%HERE%inspect_disasm.py" "%HERE%logs\q4_kernel_disasm_d12_r4.txt" "%HERE%logs\STATIC_INSTRUCTION_COUNTS_d12_r4.md" > "%HERE%logs\static_counts_d12_r4.txt" 2>&1
if errorlevel 1 (type "%HERE%logs\static_counts_d12_r4.txt" & exit /b 1)
pushd "%HERE%"
"%HERE%qa_kernel.exe" "%DATA%" > "logs\qa_kernel_d12_r4.txt" 2>&1
set "QA_RC=%ERRORLEVEL%"
python qa_kernel_gate.py "logs\qa_kernel_d12_r4.txt" "PAGE_EXPECTED_KERNEL.json" > "logs\qa_gate_d12_r4.txt" 2>&1
set "GATE_RC=%ERRORLEVEL%"
python qa_kernel_gate.py --self-test > "logs\qa_gate_self_test_d12_r4.txt" 2>&1
set "SELF_RC=%ERRORLEVEL%"
popd
type "%HERE%logs\qa_gate_self_test_d12_r4.txt"
type "%HERE%logs\qa_gate_d12_r4.txt"
if not "%QA_RC%"=="0" exit /b %QA_RC%
if not "%GATE_RC%"=="0" exit /b %GATE_RC%
if not "%SELF_RC%"=="0" exit /b %SELF_RC%
exit /b 0
