@echo off
setlocal
call "%~dp0build_client.bat" %*
exit /b %ERRORLEVEL%
