@echo off
if defined VCVARS64 call "%VCVARS64%" >nul
if errorlevel 1 exit /b 1
where cl.exe >nul 2>nul
if errorlevel 1 (
  echo Run from an x64 Native Tools Command Prompt, or set VCVARS64 to vcvars64.bat. 1>&2
  exit /b 1
)
exit /b 0
