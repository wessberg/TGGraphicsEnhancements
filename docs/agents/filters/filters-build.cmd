@echo off
rem Configure (once) and build the filters copy WITH NRD (the one job allowed to), then stage a runtime folder.
rem Usage: filters-build.cmd            (build + stage into %TEMP%\tgb\filters-rt)
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set SP=C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad
set WT=%SP%\wt\filters
set WTF=C:/Users/Frede/AppData/Local/Temp/claude/C--Users-Frede-Developer-TGFOA-TGFrameGeneration/3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8/scratchpad/wt/filters
set MAIN=C:\Users\Frede\Developer\TGFOA\TGFrameGeneration
set B=%TEMP%\tgb\filters-build
set DEPS=%TEMP:\=/%/tgb/surfaces-deps
set RT=%TEMP%\tgb\filters-rt
set PATH=%SP%\pytools\bin;%PATH%
if not exist "%B%\CMakeCache.txt" (
  cmake -S "%WT%" -B "%B%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON ^
    -DCMAKE_MAKE_PROGRAM=%SP:\=/%/pytools/bin/ninja.exe ^
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON ^
    -DFETCHCONTENT_SOURCE_DIR_DXC=%DEPS%/dxc-src -DFETCHCONTENT_SOURCE_DIR_MATHLIB=%DEPS%/mathlib-src ^
    -DFETCHCONTENT_SOURCE_DIR_MINHOOK=%DEPS%/minhook-src -DFETCHCONTENT_SOURCE_DIR_SHADERMAKE=%DEPS%/shadermake-src ^
    -DMINHOOK_ROOT=%DEPS%/minhook-src -DANTILAG2_ROOT=%WTF%/AntiLag2-SDK-2.0.0a ^
    -DDLSS_ROOT=%WTF%/DLSS-SDK-310.9.1 -DFIDELITYFX_ROOT=%WTF%/FidelityFX-SDK-2.3.0 -DSTREAMLINE_ROOT=%WTF%/Streamline-2.14.1 ^
    -DXESS_ROOT=%WTF%/XeSS-SDK-3.0.2 -DNGX_INCLUDE_DIR=%WTF%/Streamline-2.14.1/external/ngx-sdk/include ^
    -DTG_DXC=%WTF%/DXC-1.9.2607/bin/x64/dxc.exe -DTGPT_WITH_NRD=ON -DTGFG_BUILD_NATIVE=ON ^
    -DNRD_NORMAL_ENCODING=2 -DNRD_ROUGHNESS_ENCODING=1 ^
    -DTG_TEST_NATIVE_DIR=%RT:\=/% > "%TEMP%\tgb\filters-configure.log" 2>&1
  if errorlevel 1 ( echo CONFIGURE FAILED & exit /b 1 )
)
set TARGETS=TGPathTracing.Native tgpt_gpu_test
if not "%1"=="" set TARGETS=%*
cmake --build "%B%" --target %TARGETS% > "%TEMP%\tgb\filters-buildlog.txt" 2>&1
if not %errorlevel%==0 ( echo BUILD FAILED & findstr /i /c:"error" "%TEMP%\tgb\filters-buildlog.txt" & exit /b 1 )
if not exist "%RT%\TGGraphicsBridge" (
  mkdir "%RT%" 2>nul
  xcopy /e /i /q /y "%MAIN%\artifacts\Native\TGGraphicsBridge" "%RT%\TGGraphicsBridge" >nul
  xcopy /e /i /q /y "%MAIN%\artifacts\Native\TGPathTracing" "%RT%\TGPathTracing" >nul
  if exist "%RT%\TGPathTracing\ShaderCache" rmdir /s /q "%RT%\TGPathTracing\ShaderCache"
)
copy /y "%B%\TGPathTracing.Native.dll" "%RT%\TGPathTracing\TGPathTracing.Native.dll" >nul
if errorlevel 1 ( echo STAGE FAILED & exit /b 1 )
if exist "%B%\tgpt_gpu_test.exe" copy /y "%B%\tgpt_gpu_test.exe" "%RT%\tgpt_gpu_test.exe" >nul
echo BUILD OK
