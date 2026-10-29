@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
cl /nologo /LD /O2 /EHsc /MT /std:c++17 /utf-8 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /Inative\neural\include /Inative\bridge\include /IDLSS-SDK-310.9.1\include build\nr\multipass-debug\extension.cpp /Fobuild\nr\multipass-debug\extension.obj /Febuild\nr\multipass-debug\TGNeuralRendering.Native.dll /link /IMPLIB:build\nr\multipass-debug\nr.lib d3d11.lib d3d12.lib d3dcompiler.lib dxguid.lib version.lib bcrypt.lib dxgi.lib user32.lib advapi32.lib ole32.lib DLSS-SDK-310.9.1\lib\Windows_x86_64\x64\nvsdk_ngx_s.lib
if errorlevel 1 exit /b %errorlevel%
