@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
cl /nologo /O2 /EHsc /MT /std:c++17 /Inative\neural\include /Inative\bridge\include tests\neural_gpu_smoke.cpp /Fobuild\nr\multipass-debug\probe.obj /Febuild\nr\multipass-debug\probe.exe /link dxgi.lib d3d11.lib user32.lib
if errorlevel 1 exit /b %errorlevel%
python tests/neural_multipass_mutations.py build/nr/probe/BepInEx build/nr/multipass-debug/probe.exe build/nr/multipass-check
if errorlevel 1 exit /b %errorlevel%
python tests/neural_policy_mutations.py build/nr/policy-multipass
