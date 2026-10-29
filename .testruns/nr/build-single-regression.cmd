@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
cl /nologo /O2 /EHsc /MT /std:c++17 /Inative\neural\include /Inative\bridge\include build\nr\single-regression\probe.cpp /Fobuild\nr\single-regression\probe.obj /Febuild\nr\single-regression\probe.exe /link dxgi.lib d3d11.lib user32.lib
