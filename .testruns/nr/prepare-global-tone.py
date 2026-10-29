from pathlib import Path
import shutil
import sys
import numpy as np
sys.path.insert(0, str(Path('tools/harness').resolve()))
from neural_guides import load

root=Path.cwd()
out=root/'build/nr/global-tone-check'
native=out/'BepInEx/TGNeuralRendering/Native'
native.mkdir(parents=True, exist_ok=True)
source=(root/'native/neural/src/extension.cpp').read_text()
source+='''
TGNR_EXPORT void TGNR_TestGlobalTone(float value) {
    std::lock_guard<std::recursive_mutex> guard(tgnr::mutex);
    float before=0;
    const auto result=tgnr::runtime().params->Get("DLSSNR.GlobalToneStrength",&before);
    tgnr::log("GlobalTone parameter lookup="+std::to_string(uint32_t(result))+", value="+std::to_string(before));
    if(value>=0) tgnr::runtime().params->Set("DLSSNR.GlobalToneStrength",value);
}
'''
(out/'extension.cpp').write_text(source)
shutil.copyfile(root/'native/neural/src/diagnostics.hpp',out/'diagnostics.hpp')
probe=(root/'tests/neural_gpu_smoke.cpp').read_text()
start=probe.index('        if(argc>2 && std::strcmp(argv[2],"--multipass")==0)')
probe=probe[:start]+r'''
        if(argc<4) throw std::runtime_error("Provide tone value and output label");
        const float tone=std::stof(argv[2]);
        const auto globalTone=symbol<void(__cdecl*)(float)>(nr,"TGNR_TestGlobalTone");
        globalTone(tone);
        constexpr UINT size=512,frames=32;
        std::vector<float> colors(size*size*4),depths(size*size,.5f),motion(size*size*2,0);
        std::ifstream input(root.parent_path()/"source.f32",std::ios::binary);
        input.read(reinterpret_cast<char*>(colors.data()),colors.size()*sizeof(float));
        if(!input) throw std::runtime_error("Missing real-scene input");
        auto color=texture(size,size,DXGI_FORMAT_R32G32B32A32_FLOAT,colors.data(),size*16);
        auto depth=texture(size,size,DXGI_FORMAT_R32_FLOAT,depths.data(),size*4);
        auto mv=texture(size,size,DXGI_FORMAT_R32G32_FLOAT,motion.data(),size*8);
        TGNR_Frame f{sizeof(f),TGNR_ABI,7000,1,reinterpret_cast<uint64_t>(color.Get()),reinterpret_cast<uint64_t>(depth.Get()),reinterpret_cast<uint64_t>(mv.Get()),size,size,size,size,11,1,0,0,.1f,1000};
        std::ofstream file(root.parent_path()/(std::string(argv[3])+".f32"),std::ios::binary);
        for(UINT frame=0;frame<frames;++frame) {
            f.flags=TGNR_REVERSED_DEPTH|TGNR_HDR_INPUT|(frame==0 ? TGNR_RESET:0); ++f.frame;
            context->UpdateSubresource(color.Get(),0,nullptr,colors.data(),size*16,0);
            auto guides=submit(&f,1); if(!guides) throw std::runtime_error("Global tone guides rejected"); render(1,guides);
            auto scene=submit(&f,2); if(!scene) throw std::runtime_error("Global tone scene rejected"); render(2,scene);
            read(&status); reason(detail,sizeof(detail));
            if(status.state!=TGNR_READY) throw std::runtime_error(detail);
            const auto output=pixels(color.Get(),size);
            file.write(reinterpret_cast<const char*>(output.data()),output.size()*sizeof(float));
        }
        render(3,nullptr);
        std::printf("Captured32 full-strength real-scene evaluations with GlobalTone=%g\n",tone);
        return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
'''
(out/'probe.cpp').write_text(probe)
for file in ['TGGraphicsBridge.Native.dll','DLSS/nvngx_dlssnr.dll']:
    (native/file).parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(root/'build/nr/profile-check/BepInEx/TGNeuralRendering/Native'/file,native/file)
_, scene=load(root/'artifacts/harness/nr-guide-walk-final-20261008/nr-chair-walk/01-quality-neutral/nr-stages/guide-1307-color.bin')
# Actual recorded linear HDR tavern crop. Native adapter performs its ordinary HDR conditioning.
crop=scene[240:752,880:1392,:3][::-1].copy()
rgba=np.concatenate([crop,np.ones((512,512,1),np.float32)],axis=-1)
rgba.astype('<f4').tofile(out/'source.f32')
cmd=(root/'.testruns/nr/build-profile-check.cmd').read_text().replace('profile-check','global-tone-check')
(root/'.testruns/nr/build-global-tone.cmd').write_text(cmd)
