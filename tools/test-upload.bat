@echo off
setlocal
cd /d "%~dp0.."
call "%~dp0msvc-env.bat"
if errorlevel 1 exit /b 1
if not exist tools\build mkdir tools\build
cl /nologo /EHsc /std:c++20 /O2 /MT tests\test_upload.cpp /Fotools\build\test_upload.obj /Fetools\build\test_upload.exe
if errorlevel 1 exit /b 1
tools\build\test_upload.exe
exit /b %ERRORLEVEL%
