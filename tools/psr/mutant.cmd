@echo off
rem The worker psr-2's one mutant of the reflections' pass: compiles ONLY the pass's Shader Execution Reordering build
rem (the one this GPU runs) from the copy's sources as they stand, into the build's header, with the command CMake wrote
rem for it. The caller mutates and restores the source and keeps a copy of the good header (tools\psr\mutation_check.py).
setlocal
set "TEMP=C:\Users\Frede\AppData\Local\Temp\tgb\psr-build\tmp"
set "TMP=%TEMP%"
"C:\Users\Frede\AppData\Local\Temp\tg-agents\wt\psr\DXC-1.9.2607\bin\x64\dxc.exe" -nologo -T lib_6_9 -D PT_DXR12 -D PT_SER -D PT_NO_LAMP_AROUND -HV 2021 -O3 -WX -Qstrip_debug -I C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/RTXDI-Library-3.1/Include -Fh C:/Users/Frede/AppData/Local/Temp/tgb/psr-build/generated/pathtracing/pt_specular_ser.h -Vn specularSer C:/Users/Frede/AppData/Local/Temp/tg-agents/wt/psr/native/pathtracing/shaders/pt_specular.hlsl
exit /b %errorlevel%
