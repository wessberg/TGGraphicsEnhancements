@echo off
set "PATH=C:\Users\Frede\AppData\Local\Microsoft\WinGet\Packages\Kitware.CMake_Microsoft.Winget.Source_8wekyb3d8bbwe\cmake-4.4.3-windows-x86_64\bin;%PATH%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
cmake -S . -B build/nr -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DTGFG_BUILD_NATIVE=ON -DBUILD_TESTING=ON -DTGPT_WITH_NRD=OFF -DMINHOOK_ROOT="%CD%/build/native/_deps/minhook-src" -DSTREAMLINE_ROOT="%CD%/Streamline-2.14.1" -DNGX_INCLUDE_DIR="%CD%/Streamline-2.14.1/external/ngx-sdk/include" -DFIDELITYFX_ROOT="%CD%/FidelityFX-SDK-2.3.0" -DXESS_ROOT="%CD%/XeSS-SDK-3.0.2" -DANTILAG2_ROOT="%CD%/AntiLag2-SDK-2.0.0a" -DDLSS_ROOT="%CD%/DLSS-SDK-310.9.1" -DTG_TEST_NATIVE_DIR="%CD%/build/nr/install"
if errorlevel 1 exit /b %errorlevel%
cmake --build build/nr --target TGNeuralRendering.Native TGGraphicsBridge.Native TGFG.Native TGBetterUpscaling.Native tgnr_policy_tests tgnr_gpu_smoke tgup_gpu_smoke
if errorlevel 1 exit /b %errorlevel%
cmake --install build/nr --prefix build/nr/install --component TGNRProbe
