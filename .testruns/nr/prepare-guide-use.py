from pathlib import Path
import shutil
import sys
import numpy as np
sys.path.insert(0, str(Path('tools/harness').resolve()))
from neural_guides import load

root = Path.cwd()
out = root/'build/nr/guide-use-check'
native = out/'BepInEx/TGNeuralRendering/Native'
native.mkdir(parents=True, exist_ok=True)
source = (root/'native/neural/src/extension.cpp').read_text()
source = source.replace('namespace tgnr {', 'namespace tgnr {\nuint32_t guideUseMode=0;', 1)
assert 'uint32_t guideUseMode=0;' in source
old = 'params->Set("DLSSNR.Depth",depth.d12.Get()); params->Set("DLSSNR.MVec",motion.d12.Get());'
new = 'params->Set("DLSSNR.Depth",guideUseMode==2 ? static_cast<ID3D12Resource*>(nullptr):depth.d12.Get()); params->Set("DLSSNR.MVec",(guideUseMode==1 || guideUseMode==5) ? static_cast<ID3D12Resource*>(nullptr):motion.d12.Get());'
assert source.count(old) == 1
source = source.replace(old, new)
old = 'params->Set("DLSSNR.Reset",reset ? 1u:0u);'
assert source.count(old) == 1
source = source.replace(old, 'params->Set("DLSSNR.Reset",(reset || guideUseMode==3 || guideUseMode==6) ? 1u:0u);')
old = 'params->Set("DLSSNR.LocalStructureStrength",f.localStructure);'
assert source.count(old) == 1
source = source.replace(old, 'params->Set("DLSSNR.LocalStructureStrength",guideUseMode>=4 ? 0.f:f.localStructure);')
old = 'params->Set("DLSSNR.SkinStructureStrength",f.skinStructure);'
assert source.count(old) == 1
source = source.replace(old, 'params->Set("DLSSNR.SkinStructureStrength",guideUseMode>=4 ? 0.f:f.skinStructure);')
source += '\nTGNR_EXPORT void TGNR_TestGuideUse(uint32_t mode) { tgnr::guideUseMode=mode; }\n'
(out/'extension.cpp').write_text(source)
shutil.copyfile(root/'native/neural/src/diagnostics.hpp', out/'diagnostics.hpp')
probe = (root/'build/nr/global-tone-check/probe.cpp').read_text()
start = probe.index('        if(argc<4)')
probe = probe[:start] + r'''
        if(argc<4) throw std::runtime_error("Provide guide mode and output label");
        const auto mode=std::stoul(argv[2]);
        symbol<void(__cdecl*)(uint32_t)>(nr,"TGNR_TestGuideUse")(mode);
        constexpr UINT size=512,sourceWidth=640,frames=40;
        std::vector<float> sourceColor(sourceWidth*size*4),sourceDepth(sourceWidth*size);
        auto load=[&](const char* name,std::vector<float>& values) {
            std::ifstream file(root.parent_path()/name,std::ios::binary);
            file.read(reinterpret_cast<char*>(values.data()),values.size()*sizeof(float));
            if(!file) throw std::runtime_error("Missing recorded scene data");
        };
        load("color.f32",sourceColor); load("depth.f32",sourceDepth);
        std::vector<float> colors(size*size*4),depths(size*size),motion(size*size*2);
        auto color=texture(size,size,DXGI_FORMAT_R32G32B32A32_FLOAT,nullptr,0);
        auto depth=texture(size,size,DXGI_FORMAT_R32_FLOAT,nullptr,0);
        auto mv=texture(size,size,DXGI_FORMAT_R32G32_FLOAT,nullptr,0);
        TGNR_Frame f{sizeof(f),TGNR_ABI,7000,1,reinterpret_cast<uint64_t>(color.Get()),reinterpret_cast<uint64_t>(depth.Get()),reinterpret_cast<uint64_t>(mv.Get()),size,size,size,size,11,1,0,0,.01f,5000};
        std::ofstream file(root.parent_path()/(std::string(argv[3])+".f32"),std::ios::binary);
        UINT previousOffset=0;
        for(UINT frame=0;frame<frames;++frame) {
            // Eight static frames, 24 frames panning a recorded image by two pixels, eight held.
            // This tests guide consumption, not 3D camera motion or subjective game stability.
            const UINT offset=frame<8 ? 0 : 2*std::min(frame-7,24u);
            for(UINT y=0;y<size;++y) {
                std::memcpy(colors.data()+y*size*4,sourceColor.data()+(y*sourceWidth+offset)*4,size*16);
                std::memcpy(depths.data()+y*size,sourceDepth.data()+y*sourceWidth+offset,size*4);
            }
            for(UINT i=0;i<size*size;++i) { motion[i*2]=-float(offset-previousOffset)/size; motion[i*2+1]=0; }
            previousOffset=offset;
            context->UpdateSubresource(color.Get(),0,nullptr,colors.data(),size*16,0);
            context->UpdateSubresource(depth.Get(),0,nullptr,depths.data(),size*4,0);
            context->UpdateSubresource(mv.Get(),0,nullptr,motion.data(),size*8,0);
            f.flags=TGNR_REVERSED_DEPTH|TGNR_HDR_INPUT|(frame==0 ? TGNR_RESET:0); ++f.frame;
            auto guides=submit(&f,1); if(!guides) throw std::runtime_error("Guide packet rejected"); render(1,guides);
            auto scene=submit(&f,2); if(!scene) throw std::runtime_error("Scene packet rejected"); render(2,scene);
            read(&status); reason(detail,sizeof(detail));
            if(status.state!=TGNR_READY) throw std::runtime_error(detail);
            const auto output=pixels(color.Get(),size);
            file.write(reinterpret_cast<const char*>(output.data()),output.size()*sizeof(float));
        }
        render(3,nullptr);
        std::printf("Captured40 frames, guide mode=%u, %s\n",unsigned(mode),argv[3]);
        return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
'''
# D3D11 forbids a non-null initial data descriptor whose data pointer is null.
probe = probe.replace('CreateTexture2D(&d,&initial,&result)', 'CreateTexture2D(&d,data ? &initial:nullptr,&result)')
(out/'probe.cpp').write_text(probe)
for name in ['TGGraphicsBridge.Native.dll', 'DLSS/nvngx_dlssnr.dll']:
    (native/name).parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(root/'build/nr/global-tone-check/BepInEx/TGNeuralRendering/Native'/name, native/name)
folder = root/'artifacts/harness/nr-guide-walk-final-20261008/nr-chair-walk/01-quality-neutral/nr-stages'
_, color = load(folder/'guide-1307-color.bin')
_, depth = load(folder/'guide-1307-depth.bin')
color = color[240:752,816:1456,:3][::-1].copy()
rgba = np.concatenate([color,np.ones((512,640,1),np.float32)],axis=-1)
rgba.astype('<f4').tofile(out/'color.f32')
depth[240:752,816:1456][::-1].astype('<f4').tofile(out/'depth.f32')
cmd = (root/'.testruns/nr/build-global-tone.cmd').read_text().replace('global-tone-check','guide-use-check')
(root/'.testruns/nr/build-guide-use.cmd').write_text(cmd)
