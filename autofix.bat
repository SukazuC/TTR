@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0autofix.ps1" %*
set EXIT_CODE=%ERRORLEVEL%
echo.
if %EXIT_CODE% EQU 0 (
    echo [autofix] Completed successfully.
) else (
    echo [autofix] Terminated with error code %EXIT_CODE%.
)
echo.
pause
endlocal
