@echo off
setlocal
set "SCRIPT_DIR=%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%launch_client.ps1" %*
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Client exited with error code %ERRORLEVEL%.
    pause
)
exit /b %ERRORLEVEL%
