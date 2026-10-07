@echo off
setlocal
cd /d "%~dp0.."
call "%~dp0msvc-env.bat"
if errorlevel 1 exit /b 1
if not exist tools\build mkdir tools\build
if not defined RESHADE_SOURCE (
  echo Set RESHADE_SOURCE to the source directory of the ReShade 6.8.0 checkout. 1>&2
  exit /b 1
)
set "FX=%RESHADE_SOURCE%"
cl /nologo /EHsc /std:c++17 /O2 /MT /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I "%FX%" tools\validate_shader.cpp "%FX%\effect_codegen_hlsl.cpp" "%FX%\effect_expression.cpp" "%FX%\effect_lexer.cpp" "%FX%\effect_parser_exp.cpp" "%FX%\effect_parser_stmt.cpp" "%FX%\effect_preprocessor.cpp" "%FX%\effect_symbol_table.cpp" /Fotools\build\ /Fetools\build\validate_shader.exe /link d3dcompiler.lib
if errorlevel 1 exit /b 1
tools\build\validate_shader.exe host\reshade\shaders\ETBMinecraft.fx
exit /b %ERRORLEVEL%
