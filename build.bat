@echo off
setlocal
call "%~dp0host\reshade\build.bat"
exit /b %ERRORLEVEL%
