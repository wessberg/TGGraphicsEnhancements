from pathlib import Path
import shutil

root = Path.cwd()
out = root / 'build/nr/profile-check'
native = out / 'BepInEx/TGNeuralRendering/Native'
native.mkdir(parents=True, exist_ok=True)
source = (root/'native/neural/src/extension.cpp').read_text()
source += '''
TGNR_EXPORT void TGNR_TestPreset(uint32_t preset) {
    std::lock_guard<std::recursive_mutex> guard(tgnr::mutex);
    tgnr::runtime().params->Set("DLSSNR.Hint.Render.Preset",preset);
}
'''
(out/'extension.cpp').write_text(source)
shutil.copyfile(root/'native/neural/src/diagnostics.hpp', out/'diagnostics.hpp')
probe = (root/'tests/neural_gpu_smoke.cpp').read_text()
start = probe.index('        if(argc>2 && std::strcmp(argv[2],"--multipass")==0)')
probe = probe[:start] + r'''
        const auto preset=symbol<void(__cdecl*)(uint32_t)>(nr,"TGNR_TestPreset");
        constexpr UINT size=256;
        constexpr int frames=24;
        std::vector<float> colors(size*size*4), depths(size*size,.5f), motion(size*size*2,0);
        for(UINT y=0;y<size;++y) for(UINT x=0;x<size;++x) {
            auto i=(y*size+x)*4;
            colors[i]=.05f+.7f*x/size; colors[i+1]=.1f+.6f*y/size;
            colors[i+2]=.15f+.2f*((x/16+y/16)%2); colors[i+3]=.37f;
        }
        auto color=texture(size,size,DXGI_FORMAT_R32G32B32A32_FLOAT,colors.data(),size*16);
        auto depth=texture(size,size,DXGI_FORMAT_R32_FLOAT,depths.data(),size*4);
        auto mv=texture(size,size,DXGI_FORMAT_R32G32_FLOAT,motion.data(),size*8);
        TGNR_Frame f{sizeof(f),TGNR_ABI,7000,1,reinterpret_cast<uint64_t>(color.Get()),reinterpret_cast<uint64_t>(depth.Get()),reinterpret_cast<uint64_t>(mv.Get()),size,size,size,size,3,1,0,0,.1f,1000};
        auto capture=[&](const std::string& name,bool fresh,uint32_t style,uint32_t hint) {
            if(fresh) render(3,nullptr);
            f.model=style; preset(hint);
            std::ofstream file(root.parent_path()/(name+".f32"),std::ios::binary);
            for(int frame=0;frame<frames;++frame) {
                f.flags=TGNR_REVERSED_DEPTH|(frame==0 ? TGNR_RESET:0); ++f.frame;
                context->UpdateSubresource(color.Get(),0,nullptr,colors.data(),size*16,0);
                auto guides=submit(&f,1); if(!guides) throw std::runtime_error("Profile guides rejected"); render(1,guides);
                auto scene=submit(&f,2); if(!scene) throw std::runtime_error("Profile scene rejected"); render(2,scene);
                read(&status); reason(detail,sizeof(detail));
                if(status.state!=TGNR_READY) throw std::runtime_error(detail);
                const auto output=pixels(color.Get(),size);
                file.write(reinterpret_cast<const char*>(output.data()),output.size()*sizeof(float));
            }
            std::printf("Captured %s style=%u preset=%u fresh=%u\n",name.c_str(),style,hint,unsigned(fresh));
        };
        capture("fresh-default",true,0,0);
        capture("reset-default",false,0,0);
        capture("live-style1",false,1,0);
        capture("fresh-style1",true,1,0);
        capture("live-style2",false,2,0);
        capture("fresh-style2",true,2,0);
        capture("fresh-default-repeat",true,0,0);
        for(uint32_t hint=1;hint<=3;++hint) capture("fresh-preset"+std::to_string(hint),true,0,hint);
        render(3,nullptr);
        std::puts("Completed profile probe. Compare exact outputs before inferring preset/style behavior.");
        return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
'''
(out/'probe.cpp').write_text(probe)
shutil.copyfile(root/'build/nr/install/TGGraphicsBridge/TGGraphicsBridge.Native.dll', native/'TGGraphicsBridge.Native.dll')
(native/'DLSS').mkdir(exist_ok=True)
shutil.copyfile(root/'build/nr/probe/BepInEx/TGNeuralRendering/Native/DLSS/nvngx_dlssnr.dll',native/'DLSS/nvngx_dlssnr.dll')
cmd=(root/'.testruns/nr/build-overdrive.cmd').read_text().replace('overdrive-check','profile-check')
(root/'.testruns/nr/build-profile-check.cmd').write_text(cmd)
print('Prepared independent profile probe with current production NR source. No game files changed.')
