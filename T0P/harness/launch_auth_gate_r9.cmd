@echo off
setlocal
"%~1" --official 1>"%~2\auth_gate.stdout.txt" 2>"%~2\auth_gate.stderr.txt"
set "RC=%ERRORLEVEL%"
echo %RC%>"%~2\auth_gate.exit.txt"
exit /b 0
