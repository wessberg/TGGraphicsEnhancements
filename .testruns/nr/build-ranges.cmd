@echo off
set "PATH=C:\Users\Frede\AppData\Local\Microsoft\WinGet\Packages\Kitware.CMake_Microsoft.Winget.Source_8wekyb3d8bbwe\cmake-4.4.3-windows-x86_64\bin;%PATH%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
cmake --build build/nr --target TGNeuralRendering.Native tgnr_gpu_smoke
if errorlevel 1 exit /b %errorlevel%
cmake --install build/nr --prefix build/nr/install --component TGNRProbe
