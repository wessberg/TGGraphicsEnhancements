from pathlib import Path
import shutil

root=Path.cwd()
base=root/'build/nr/profile-check'
out=root/'build/nr/style-c-check'
native=out/'BepInEx/TGNeuralRendering/Native'
native.mkdir(parents=True,exist_ok=True)
shutil.copyfile(root/'native/neural/src/extension.cpp',out/'extension.cpp')
shutil.copyfile(root/'native/neural/src/diagnostics.hpp',out/'diagnostics.hpp')
policy=(root/'native/neural/include/tgnr_policy.hpp').read_text()
assert policy.count('f.model<=2')==1
(out/'tgnr_policy.hpp').write_text(policy.replace('f.model<=2','f.model<=3'))
probe=(base/'probe.cpp').read_text()
probe=probe.replace('        const auto preset=symbol<void(__cdecl*)(uint32_t)>(nr,"TGNR_TestPreset");','')
probe=probe.replace('f.model=style; preset(hint);','f.model=style;')
start=probe.index('        capture("fresh-default",true,0,0);')
end=probe.index('        render(3,nullptr);', start)
probe=probe[:start]+'''        capture("default",true,0,0);
        capture("style-a",true,1,0);
        capture("style-b",true,2,0);
        capture("style-c",true,3,0);
        capture("default-repeat",true,0,0);
'''+probe[end:]
(out/'probe.cpp').write_text(probe)
for file in ['TGGraphicsBridge.Native.dll','DLSS/nvngx_dlssnr.dll']:
    (native/file).parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(base/'BepInEx/TGNeuralRendering/Native'/file,native/file)
cmd=(root/'.testruns/nr/build-profile-check.cmd').read_text().replace('profile-check','style-c-check')
(root/'.testruns/nr/build-style-c.cmd').write_text(cmd)
