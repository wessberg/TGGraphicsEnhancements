from pathlib import Path
exec(Path('.testruns/nr/add-stage-diagnostics.py').read_text().split("edit('native/neural/src/extension.cpp'")[0])
edit('native/neural/src/extension.cpp',[
('motion[p.xy]=v; // NGX scale', '''// Unity HDRP encodes ForceNoMotion with X > 1. Its DecodeMotionVector returns zero.
 // Only the observed Unity SR texture has this encoding contract; preserve generic/RR vectors.
 if(mode && v.x>1.0) v=0;
 motion[p.xy]=v; // NGX scale'''),
('copy(p.motion.Get(),motion,ReadAs::Motion,copyMotion.Get(),0,motionRegion.x,motionRegion.y);',
 'copy(p.motion.Get(),motion,ReadAs::Motion,copyMotion.Get(),observed && observed->source==DlssSource::SuperResolution ? 1u:0u,motionRegion.x,motionRegion.y);')])
edit('tests/neural_gpu_smoke.cpp',[
('#include <tgb/bridge_c_api.h>', '#include <tgb/bridge_c_api.h>\n#include <tgb/dlss_inputs.hpp>'),
('std::puts("PASS: real NGX evaluation,', '''if(diagnostics) {
            // Actual observer route: Unity SR's no-motion marker must not become 512 pixels of motion.
            // A generic RR packet with the same numeric values must remain untouched.
            const UINT size=256;
            std::vector<float> colors(size*size*4,.25f),depths(size*size,.5f),motion(size*size*2,0);
            for(UINT y=0;y<size;++y) for(UINT x=0;x<size;++x) motion[(y*size+x)*2]=x<size/2 ? 2.f : -.01f;
            auto color=texture(size,size,DXGI_FORMAT_R32G32B32A32_FLOAT,colors.data(),size*16);
            auto depth=texture(size,size,DXGI_FORMAT_R32_FLOAT,depths.data(),size*4);
            auto mv=texture(size,size,DXGI_FORMAT_R32G32_FLOAT,motion.data(),size*8);
            for(auto source:{tgb::DlssSource::SuperResolution,tgb::DlssSource::RayReconstruction}) {
                auto folder=fs::absolute(argv[3])/(source==tgb::DlssSource::SuperResolution ? "unity-motion" : "generic-motion");
                if(diagnosticOutput(folder.c_str())) throw std::runtime_error("Motion diagnostic output rejected");
                TGNR_Frame f{sizeof(f),TGNR_ABI,3000+uint32_t(source),1,reinterpret_cast<uint64_t>(color.Get()),0,0,size,size,size,size,3,0,0,0,.1f,1000};
                auto arm=submit(&f,4); if(!arm) throw std::runtime_error("Observed camera rejected");
                diagnosticExposure(arm,exposure.Get()); render(4,arm);
                tgb::DlssInputs in; in.source=source; in.context=context.Get(); in.feature=&f;
                in.color=in.output=color.Get(); in.depth=depth.Get(); in.motion=mv.Get();
                in.colorRect=in.outputRect=in.depthRect=in.motionRect={0,0,size,size};
                in.flags=8; in.motionScaleX=in.motionScaleY=-float(size); in.valid=in.success=true;
                auto count=status.evaluated;
                tgb::publish_dlss(tgb::DlssStage::Before,in); tgb::publish_dlss(tgb::DlssStage::After,in);
                auto finish=submit(&f,5); if(!finish) throw std::runtime_error("Observed output rejected"); render(5,finish);
                read(&status); if(status.state!=TGNR_READY || status.evaluated!=count+1) throw std::runtime_error("Observed motion evaluation failed");
                diagnosticOutput(nullptr); render(3,nullptr);
            }
        }
        std::puts("PASS: real NGX evaluation,''')])
print('Unity SR motion-marker decoding and observer probe added.')
