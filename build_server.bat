@echo off
setlocal
set "SCRIPT_DIR=%~dp0"
call "%SCRIPT_DIR%build_windows.bat" -Target server %*
exit /b %ERRORLEVEL%
