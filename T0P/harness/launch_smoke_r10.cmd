@echo off
setlocal EnableDelayedExpansion
set "MODE=%~5"
if "%MODE%"=="" set "MODE=--smoke"
"%~1" %MODE% "%~2" "%~3" "%~4" 1>"%~2\harness.stdout.txt" 2>"%~2\harness.stderr.txt"
set "RC=!ERRORLEVEL!"
>"%~2\harness.exit.txt" echo(!RC!
exit /b 0



