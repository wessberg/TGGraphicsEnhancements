@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
cl /nologo /LD /O2 /EHsc /MT /std:c++17 /utf-8 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /Inative\neural\include /Inative\bridge\include /IDLSS-SDK-310.9.1\include build\nr\overdrive-check\extension.cpp /Fobuild\nr\overdrive-check\extension.obj /Febuild\nr\overdrive-check\BepInEx\TGNeuralRendering\Native\TGNeuralRendering.Native.dll /link /IMPLIB:build\nr\overdrive-check\nr.lib d3d11.lib d3d12.lib d3dcompiler.lib dxguid.lib version.lib bcrypt.lib dxgi.lib user32.lib advapi32.lib ole32.lib DLSS-SDK-310.9.1\lib\Windows_x86_64\x64\nvsdk_ngx_s.lib
if errorlevel 1 exit /b %errorlevel%
cl /nologo /O2 /EHsc /MT /std:c++17 /Inative\neural\include /Inative\bridge\include build\nr\overdrive-check\probe.cpp /Fobuild\nr\overdrive-check\probe.obj /Febuild\nr\overdrive-check\probe.exe /link dxgi.lib d3d11.lib user32.lib
