from pathlib import Path
p=Path('tests/upscaling_gpu_smoke.cpp');raw=p.read_bytes();s=raw.decode().replace('\r\n','\n')
a=s.index('            if(submit(&f)!=0) { expect(false, "submit a Ray Reconstruction request"); break; }')
b=s.index('\n        }\n        context->Flush();',a)
s=s[:a]+'''            using NrQueue=void*(__cdecl*)(const TGNR_Frame*,uint32_t);
            using NrRead=int(__cdecl*)(TGNR_Status*);
            NrQueue queueNr=nullptr; NrRead readNr=nullptr;
            void(__stdcall* executeNr)(int,void*)=nullptr;
            TGNR_Frame nr{sizeof(nr),TGNR_ABI,f.sequence,1,f.color,0,0,OW,OH,RW,RH,9,0,0,0,0.1f,1000};
            // No depth/motion guesses are provided by the camera packet: NR must consume the
            // actual RR handoff. Wrong-camera mutation must fail this same evaluation check.
            if(withNr && n<frames-2) {
                auto module=GetModuleHandleW(L"TGNeuralRendering.Native.dll");
                queueNr=reinterpret_cast<NrQueue>(GetProcAddress(module,"TGNR_QueueFrame"));
                auto eventNr=reinterpret_cast<void*(__cdecl*)()>(GetProcAddress(module,"TGNR_GetRenderEvent"));
                readNr=reinterpret_cast<NrRead>(GetProcAddress(module,"TGNR_ReadStatus"));
                expect(queueNr && eventNr && readNr,"NR exports beside RR");
                if(!queueNr || !eventNr || !readNr) return 1;
                executeNr=reinterpret_cast<void(__stdcall*)(int,void*)>(eventNr());
                if(std::getenv("TGNR_TEST_WRONG_CAMERA")) nr.color=f.output;
                auto arm=queueNr(&nr,4); expect(arm!=nullptr,"NR arms the RR camera without substitute guides"); executeNr(4,arm);
            }
            if(submit(&f)!=0) { expect(false, "submit a Ray Reconstruction request"); break; }
            render(1, reinterpret_cast<void*>(uintptr_t(f.sequence)));
            if(executeNr) {
                nr.color=f.output;
                auto scene=queueNr(&nr,5); expect(scene!=nullptr,"NR accepts denoised scene"); executeNr(5,scene);
                TGNR_Status ns{sizeof(ns),TGNR_ABI}; readNr(&ns);
                expect(ns.evaluated==uint64_t(n+1) && ns.failed==0,"NR evaluated from actual RR inputs on their shared device");
                if(ns.failed) break;
                if(n==15 || n==frames-3) executeNr(3,nullptr);
            }'''+s[b:]
assert p.read_bytes()==raw
p.write_bytes(s.replace('\n','\r\n').encode())
print('RR GPU coexistence check now exercises the shared input path; wrong-camera mutation available.')
