@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /nologo /EHsc /O2 /std:c++17 /W3 probe.cpp /Fe:share-probe.exe > build.log 2>&1
echo exit %ERRORLEVEL%
findstr /i /c:"error" /c:"warning" build.log
