@echo off
setlocal
"%~1" --smoke "%~2" 1>"%~2\harness.stdout.txt" 2>"%~2\harness.stderr.txt"
set "RC=%ERRORLEVEL%"
echo %RC%>"%~2\harness.exit.txt"
exit /b 0



