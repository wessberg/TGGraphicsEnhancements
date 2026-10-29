from pathlib import Path
import shutil

root=Path.cwd()
folder=root/'build/nr/single-regression'
folder.mkdir(exist_ok=True)
source=(root/'tests/neural_gpu_smoke.cpp').read_text()
source=source[:source.index('        if(argc>2 && std::strcmp(argv[2],"--multipass")==0)')]
source=source.replace('TGNR_Status status{sizeof(status),TGNR_ABI};', '''const auto abi=symbol<uint32_t(__cdecl*)()>(nr,"TGNR_GetAbiVersion")();
        if(abi!=0x00010200 && abi!=0x00010300) throw std::runtime_error("Unsupported comparison ABI");
        TGNR_Status status{abi==0x00010200 ? 72u:80u,abi};''')
source+=r'''
        constexpr UINT size=256;
        std::vector<float> colors(size*size*4), depths(size*size,.5f), motion(size*size*2,0);
        auto color=texture(size,size,DXGI_FORMAT_R32G32B32A32_FLOAT,colors.data(),size*16);
        auto depth=texture(size,size,DXGI_FORMAT_R32_FLOAT,depths.data(),size*4);
        auto mv=texture(size,size,DXGI_FORMAT_R32G32_FLOAT,motion.data(),size*8);
        TGNR_Frame f{abi==0x00010200 ? 100u:104u,abi,0,1,reinterpret_cast<uint64_t>(color.Get()),reinterpret_cast<uint64_t>(depth.Get()),reinterpret_cast<uint64_t>(mv.Get()),size,size,size,size,9,1,0,0,.1f,1000};
        std::ofstream dump(root.parent_path()/"sequence.f32",std::ios::binary);
        int previous=0;
        for(uint32_t frame=0;frame<120;++frame) {
            const int offset=frame<40 || frame>=80 ? 0:int(24*std::sin((frame-40)*6.28318530718/40));
            for(UINT y=0;y<size;++y) for(UINT x=0;x<size;++x) {
                const UINT shifted=UINT(int(x)+offset+int(size))%size; const auto i=(y*size+x)*4;
                colors[i]=.02f+2.f*shifted/size; colors[i+1]=.03f+1.5f*y/size;
                colors[i+2]=.1f+.6f*((shifted/16+y/16)%2); colors[i+3]=.37f;
                motion[(y*size+x)*2]=float(offset-previous)/size;
            }
            previous=offset;
            context->UpdateSubresource(color.Get(),0,nullptr,colors.data(),size*16,0);
            context->UpdateSubresource(mv.Get(),0,nullptr,motion.data(),size*8,0);
            f.frame=frame+1; f.flags=9|(frame==0?2:0);
            auto guides=submit(&f,1); if(!guides) throw std::runtime_error("Guides rejected"); render(1,guides);
            auto scene=submit(&f,2); if(!scene) throw std::runtime_error("Scene rejected"); render(2,scene);
            read(&status);
            if(status.state!=TGNR_READY || status.evaluated!=frame+1 || status.resets!=1 || status.failed || status.skipped)
                throw std::runtime_error("Invalid regression sequence state");
            const auto out=pixels(color.Get(),size); dump.write(reinterpret_cast<const char*>(out.data()),out.size()*4);
        }
        std::printf("PASS ABI=%08X: 120 HDR frames: held / moving / held, full A, single reset, no skips/failures.\n",abi);
        render(3,nullptr); return 0;
    } catch(const std::exception& e) { std::printf("FAIL: %s\n",e.what()); return 1; }
}
'''
(folder/'probe.cpp').write_text(source)
game=Path('C:/Program Files (x86)/Steam/steamapps/common/Tainted Grail FoA')
old=game/'BepInEx/TGNeuralRendering/Backups/20261008-092257-779-tuning/TGNeuralRendering.Native.dll'
for name, native in [('old',old),('new',root/'build/nr/install/TGNeuralRendering/TGNeuralRendering.Native.dll')]:
    target=folder/name/'BepInEx/TGNeuralRendering/Native'
    (target/'DLSS').mkdir(parents=True,exist_ok=True)
    shutil.copyfile(native,target/'TGNeuralRendering.Native.dll')
    shutil.copyfile(game/'BepInEx/TGNeuralRendering/Native/TGGraphicsBridge.Native.dll',target/'TGGraphicsBridge.Native.dll')
    shutil.copyfile(game/'BepInEx/TGNeuralRendering/Native/DLSS/nvngx_dlssnr.dll',target/'DLSS/nvngx_dlssnr.dll')
cmd=(root/'.testruns/nr/build-overdrive.cmd').read_text()
cmd=cmd[:cmd.index('cl /nologo /LD')]+cmd[cmd.index('cl /nologo /O2 /EHsc /MT'):]
(root/'.testruns/nr/build-single-regression.cmd').write_text(cmd.replace('overdrive-check','single-regression'))
