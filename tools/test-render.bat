@echo off
setlocal
cd /d "%~dp0.."
call "%~dp0msvc-env.bat"
if errorlevel 1 exit /b 1
if not exist tools\build mkdir tools\build
if not exist tools\build\effect_codegen_hlsl.obj call tools\validate-shader.bat
if errorlevel 1 exit /b 1
if not defined RESHADE_SOURCE (
  echo Set RESHADE_SOURCE to the source directory of the ReShade 6.8.0 checkout. 1>&2
  exit /b 1
)
set "FX=%RESHADE_SOURCE%"
cl /nologo /EHsc /std:c++17 /O2 /MT /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I "%FX%" tools\test_render.cpp /Fotools\build\test_render.obj /Fetools\build\test_render.exe /link tools\build\effect_codegen_hlsl.obj tools\build\effect_expression.obj tools\build\effect_lexer.obj tools\build\effect_parser_exp.obj tools\build\effect_parser_stmt.obj tools\build\effect_preprocessor.obj tools\build\effect_symbol_table.obj d3d11.lib d3dcompiler.lib
if errorlevel 1 exit /b 1
tools\build\test_render.exe
exit /b %ERRORLEVEL%
