@echo off
setlocal
set "HERE=%~dp0"
if not exist "%HERE%logs_r10" mkdir "%HERE%logs_r10"
python -m py_compile "%HERE%qualify_r10.py"
if errorlevel 1 exit /b 1
python "%HERE%qualify_r10.py" --self-test --self-test-output "%HERE%logs_r10\synthetic_qa_r10.txt" > "%HERE%logs_r10\self_test.stdout.txt" 2>&1
if errorlevel 1 (type "%HERE%logs_r10\self_test.stdout.txt" & exit /b 1)
type "%HERE%logs_r10\self_test.stdout.txt"
echo COLLECTOR_R10_BUILD_AND_QA PASS
exit /b 0
