@echo off
setlocal
set "TEMP=C:\Users\Frede\AppData\Local\Temp\tgb\psr-build\tmp"
set "TMP=%TEMP%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
if "%~1"=="fixture" goto fixture
"C:\Users\Frede\AppData\Local\Microsoft\WinGet\Packages\Kitware.CMake_Microsoft.Winget.Source_8wekyb3d8bbwe\cmake-4.4.3-windows-x86_64\bin\cmake.exe" --build C:/Users/Frede/AppData/Local/Temp/tgb/psr-build --target TGPathTracing.Native tgpt_gpu_test
exit /b %errorlevel%
:fixture
"C:\Users\Frede\AppData\Local\Microsoft\WinGet\Packages\Kitware.CMake_Microsoft.Winget.Source_8wekyb3d8bbwe\cmake-4.4.3-windows-x86_64\bin\cmake.exe" --build C:/Users/Frede/AppData/Local/Temp/tgb/psr-build --target tgpt_gpu_test
exit /b %errorlevel%
