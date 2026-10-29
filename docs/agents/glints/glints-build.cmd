@echo off
rem glints worker: configure (once), compile the shader passes side by side, then build the tracer and its GPU test
rem into %TEMP%\tgb\glints. Nothing is installed, nothing outside %TEMP%\tgb\glints* is written. NRD off (its configure
rem writes into the shared SDK folder).
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set CM=C:\Users\Frede\AppData\Local\Microsoft\WinGet\Packages\Kitware.CMake_Microsoft.Winget.Source_8wekyb3d8bbwe\cmake-4.4.3-windows-x86_64\bin\cmake.exe
set W_SOURCE=C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\glints
set SDK_SOURCE=C:\Users\Frede\Developer\TGFOA\TGFrameGeneration
set W_BUILD=C:\Users\Frede\AppData\Local\Temp\tgb\glints
if exist "%W_BUILD%\CMakeCache.txt" goto shaders
"%CM%" -S "%W_SOURCE%" -B "%W_BUILD%" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DTGFG_BUILD_NATIVE=ON -DTGPT_WITH_NRD=OFF -DFETCHCONTENT_FULLY_DISCONNECTED=ON -DMINHOOK_ROOT="%SDK_SOURCE%/build/native/_deps/minhook-src" -DSTREAMLINE_ROOT="%SDK_SOURCE%/Streamline-2.14.1" -DNGX_INCLUDE_DIR="%SDK_SOURCE%/Streamline-2.14.1/external/ngx-sdk/include" -DFIDELITYFX_ROOT="%SDK_SOURCE%/FidelityFX-SDK-2.3.0" -DXESS_ROOT="%SDK_SOURCE%/XeSS-SDK-3.0.2" -DANTILAG2_ROOT="%SDK_SOURCE%/AntiLag2-SDK-2.0.0a" -DDLSS_ROOT="%SDK_SOURCE%/DLSS-SDK-310.9.1" -DRTXDI_ROOT="%SDK_SOURCE%/RTXDI-Library-3.1" -DTG_DXC="%SDK_SOURCE%/DXC-1.9.2607/bin/x64/dxc.exe" > "%W_BUILD%-configure.log" 2>&1
if errorlevel 1 exit /b 1
:shaders
powershell -NoProfile -ExecutionPolicy Bypass -File "%W_SOURCE%\tools\Build-PtShaders.ps1" -BuildDir "%W_BUILD%" -Throttle 8 > "%W_BUILD%-shaders.log" 2>&1
if errorlevel 1 exit /b 2
"%CM%" --build "%W_BUILD%" --target TGPathTracing.Native tgpt_gpu_test > "%W_BUILD%-build.log" 2>&1
exit /b %errorlevel%
