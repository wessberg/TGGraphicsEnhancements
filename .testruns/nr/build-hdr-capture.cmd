@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
cl /nologo /LD /EHsc /std:c++17 /O2 /W4 /I build\nr\hdr-capture-sdk\include tools\harness\hdr_capture.cpp /Fo:build\nr\hdr_capture.obj /Fe:build\nr\TGNR.HdrCapture.addon64 /link user32.lib
exit /b %errorlevel%
