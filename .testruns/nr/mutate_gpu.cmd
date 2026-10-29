@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
python tests/neural_gpu_mutations.py build/nr/probe/BepInEx build/nr/tgnr_gpu_smoke.exe build/nr/gpu-mutations
