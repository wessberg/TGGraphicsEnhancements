from pathlib import Path
import shutil

root = Path.cwd()
out = root / 'build/nr/overdrive-check'
native = out / 'BepInEx/TGNeuralRendering/Native'
native.mkdir(parents=True, exist_ok=True)
source = (root / 'native/neural/src/extension.cpp').read_text()
needle = 'params->Set("DLSSNR.Intensity",f.strength);'
assert source.count(needle) == 1
source = source.replace('#include <limits>', '#include <limits>\n#include <cstdlib>')
source = source.replace(needle, '''const char* probe = std::getenv("TGNR_PROBE_INTENSITY");
        const float factor = probe ? std::strtof(probe, nullptr) : 1.f;
        params->Set("DLSSNR.Intensity",f.strength * factor);''')
(out / 'extension.cpp').write_text(source)
shutil.copyfile(root / 'native/neural/src/diagnostics.hpp', out / 'diagnostics.hpp')
probe = (root / 'tests/neural_gpu_smoke.cpp').read_text()
probe = probe.replace('#include <fstream>', '#include <fstream>\n#include <cstdlib>')
needle = 'std::ofstream file(root.parent_path()/(std::to_string(size)+"-"+name+".ppm"),std::ios::binary);'
assert probe.count(needle) == 1
probe = probe.replace(needle, '''const auto output = root.parent_path()/std::getenv("TGNR_PROBE_CASE");
            fs::create_directories(output);
            std::ofstream raw(output/(std::to_string(size)+"-"+name+".f32"),std::ios::binary);
            raw.write(reinterpret_cast<const char*>(image.data()), image.size()*sizeof(float));
            std::ofstream file(output/(std::to_string(size)+"-"+name+".ppm"),std::ios::binary);''')
needle = 'if(status.evaluated!=18) throw std::runtime_error("NR did not evaluate every probe frame");'
assert probe.count(needle) == 1
probe = probe.replace(needle, needle + '\n        std::puts("Probe completed 18 evaluations; inspect raw output before calling overdrive effective."); return 0;')
(out / 'probe.cpp').write_text(probe)
shutil.copyfile(root / 'build/nr/install/TGGraphicsBridge/TGGraphicsBridge.Native.dll', native / 'TGGraphicsBridge.Native.dll')
(native / 'DLSS').mkdir(exist_ok=True)
model = root / 'build/nr/probe/BepInEx/TGNeuralRendering/Native/DLSS/nvngx_dlssnr.dll'
assert model.is_file()
shutil.copyfile(model, native / 'DLSS/nvngx_dlssnr.dll')
cmd = r'''@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
cl /nologo /LD /O2 /EHsc /MT /std:c++17 /utf-8 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /Inative\neural\include /Inative\bridge\include /IDLSS-SDK-310.9.1\include build\nr\overdrive-check\extension.cpp /Fobuild\nr\overdrive-check\extension.obj /Febuild\nr\overdrive-check\BepInEx\TGNeuralRendering\Native\TGNeuralRendering.Native.dll /link /IMPLIB:build\nr\overdrive-check\nr.lib d3d11.lib d3d12.lib d3dcompiler.lib dxguid.lib version.lib bcrypt.lib dxgi.lib user32.lib advapi32.lib ole32.lib DLSS-SDK-310.9.1\lib\Windows_x86_64\x64\nvsdk_ngx_s.lib
if errorlevel 1 exit /b %errorlevel%
cl /nologo /O2 /EHsc /MT /std:c++17 /Inative\neural\include /Inative\bridge\include build\nr\overdrive-check\probe.cpp /Fobuild\nr\overdrive-check\probe.obj /Febuild\nr\overdrive-check\probe.exe /link dxgi.lib d3d11.lib user32.lib
'''
(root / '.testruns/nr/build-overdrive.cmd').write_text(cmd)
print('Prepared isolated source override. Production source/build/install untouched.')
