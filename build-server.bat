@echo off
setlocal
call "%~dp0build_server.bat" %*
exit /b %ERRORLEVEL%
