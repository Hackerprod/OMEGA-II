@echo off
setlocal
"%~1" --geometry-smoke "%~2" "%~3" "%~4" 1>"%~2\geometry.stdout.txt" 2>"%~2\geometry.stderr.txt"
set "RC=%ERRORLEVEL%"
echo %RC%>"%~2\geometry.exit.txt"
exit /b 0
