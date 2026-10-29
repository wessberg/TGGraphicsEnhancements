exec(Path('.testruns/nr/add-stage-diagnostics.py').read_text().split("edit('native/neural/src/extension.cpp'")[0]) if False else None
from pathlib import Path
s=Path('.testruns/nr/add-stage-diagnostics.py').read_text().split("edit('native/neural/src/extension.cpp'")[0]
exec(s)
edit('tests/neural_gpu_smoke.cpp',[
('for(uint32_t size:{256u,320u}) {', '''const bool diagnostics=argc>3 && std::strcmp(argv[2],"--diagnostics")==0;
        auto diagnosticOutput=symbol<int(__cdecl*)(const wchar_t*)>(nr,"TGNR_SetDiagnostics");
        auto diagnosticExposure=symbol<int(__cdecl*)(void*,ID3D11Resource*)>(nr,"TGNR_QueueDiagnosticExposure");
        if(diagnostics && diagnosticOutput(fs::absolute(argv[3]).c_str())) throw std::runtime_error("Diagnostic output rejected");
        const float exposureData[]={0.125f,3.0f};
        auto exposure=texture(1,1,DXGI_FORMAT_R32G32_FLOAT,exposureData,8);
        for(uint32_t size:{256u,320u}) {'''),
('auto scene=submit(&f,2); if(!scene) throw std::runtime_error("Color rejected"); render(2,scene);', '''auto scene=submit(&f,2); if(!scene) throw std::runtime_error("Color rejected");
                if(diagnostics && diagnosticExposure(scene,exposure.Get())) throw std::runtime_error("Diagnostic exposure rejected");
                render(2,scene);'''),
('if(status.evaluated!=18) throw std::runtime_error("NR did not evaluate every probe frame");', '''if(status.evaluated!=18) throw std::runtime_error("NR did not evaluate every probe frame");
        if(diagnostics) diagnosticOutput(nullptr);''')])
