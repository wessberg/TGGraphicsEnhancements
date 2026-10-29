@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
cl /nologo /O2 /EHsc /MT /std:c++17 /Inative\neural\include /Inative\bridge\include build\nr\multipass-debug\exact-probe.cpp /Fobuild\nr\multipass-debug\probe.obj /Febuild\nr\multipass-debug\exact-probe.exe /link dxgi.lib d3d11.lib user32.lib
