@echo off
setlocal
set "ROOT=%~dp0"
call "%ROOT%..\..\tools\msvc-env.bat"
if errorlevel 1 exit /b 1
if not exist "%ROOT%build" mkdir "%ROOT%build"
cl /nologo /LD /O2 /EHsc /std:c++20 /MT /W4 /DWIN32_LEAN_AND_MEAN /DNOMINMAX ^
  /I "%ROOT%\third_party\reshade" ^
  "%ROOT%\src\addon_entry.cpp" "%ROOT%\src\compositor.cpp" "%ROOT%\src\input.cpp" ^
  /Fo"%ROOT%\build\\" /Fe"%ROOT%\build\ETBMinecraft.addon64" ^
  /link user32.lib
exit /b %ERRORLEVEL%
