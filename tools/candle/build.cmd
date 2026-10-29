@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set "CM=C:\Users\Frede\AppData\Local\Microsoft\WinGet\Packages\Kitware.CMake_Microsoft.Winget.Source_8wekyb3d8bbwe\cmake-4.4.3-windows-x86_64\bin\cmake.exe"
set "MAIN=C:/Users/Frede/Developer/TGFOA/TGFrameGeneration"
"%CM%" -S . -B C:/Users/Frede/AppData/Local/Temp/tgb/candle-build -G Ninja -DCMAKE_MAKE_PROGRAM=C:/Users/Frede/AppData/Local/Temp/claude/C--Users-Frede-Developer-TGFOA-TGFrameGeneration/3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8/scratchpad/pytools/bin/ninja.exe -DCMAKE_BUILD_TYPE=Release -DTGFG_BUILD_NATIVE=ON -DBUILD_TESTING=ON -DFETCHCONTENT_FULLY_DISCONNECTED=ON -DTGPT_WITH_NRD=OFF -DSTREAMLINE_ROOT=%MAIN%/Streamline-2.14.1 -DNGX_INCLUDE_DIR=%MAIN%/Streamline-2.14.1/external/ngx-sdk/include -DFIDELITYFX_ROOT=%MAIN%/FidelityFX-SDK-2.3.0 -DXESS_ROOT=%MAIN%/XeSS-SDK-3.0.2 -DANTILAG2_ROOT=%MAIN%/AntiLag2-SDK-2.0.0a -DDLSS_ROOT=%MAIN%/DLSS-SDK-310.9.1 -DMINHOOK_ROOT=%MAIN%/build/native/_deps/minhook-src -DRTXDI_ROOT=%MAIN%/RTXDI-Library-3.1 -DTG_DXC=%MAIN%/DXC-1.9.2607/bin/x64/dxc.exe -DTG_TEST_NATIVE_DIR=C:/Users/Frede/AppData/Local/Temp/tgb/candle-build
if errorlevel 1 exit /b 1
"%CM%" --build C:/Users/Frede/AppData/Local/Temp/tgb/candle-build --target TGPathTracing.Native tgpt_gpu_test -j 6
