@echo off
rem The worker's shader build: the tracer's passes side by side, the compilers' temporary files inside the worker's own
rem build folder, the whole PC's limits read from the real %TEMP% (tools\Build-PtShaders.ps1 -MachineTemp). Never -First.
setlocal
set "TEMP=C:\Users\Frede\AppData\Local\Temp\tgb\psr-build\tmp"
set "TMP=%TEMP%"
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Frede\AppData\Local\Temp\tg-agents\wt\psr\tools\Build-PtShaders.ps1" -BuildDir C:/Users/Frede/AppData/Local/Temp/tgb/psr-build -MachineTemp C:/Users/Frede/AppData/Local/Temp
exit /b %errorlevel%
