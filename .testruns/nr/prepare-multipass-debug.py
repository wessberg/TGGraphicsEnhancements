from pathlib import Path
root = Path.cwd()
folder = root / 'build/nr/multipass-debug'
folder.mkdir(exist_ok=True)
source = (root / 'native/neural/src/extension.cpp').read_text()
helper = r'''
    void dump(ID3D11Texture2D* texture, uint32_t frame,uint32_t stage,const char* label) {
        D3D11_TEXTURE2D_DESC d{}; texture->GetDesc(&d); d.BindFlags=d.MiscFlags=0;
        d.CPUAccessFlags=D3D11_CPU_ACCESS_READ; d.Usage=D3D11_USAGE_STAGING;
        ComPtr<ID3D11Texture2D> staging; check(host->game_device()->CreateTexture2D(&d,nullptr,&staging),"Dump create");
        auto ctx=host->game_context(); ctx->CopyResource(staging.Get(),texture); D3D11_MAPPED_SUBRESOURCE m{};
        check(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&m),"Dump map");
        std::ofstream out("build/nr/multipass-debug/"+std::to_string(frame)+"-"+std::to_string(stage)+"-"+label+".bin",std::ios::binary);
        for(uint32_t y=0;y<d.Height;++y) out.write(static_cast<const char*>(m.pData)+y*m.RowPitch,d.Width*16);
        ctx->Unmap(staging.Get(),0);
    }
'''
source = source.replace('    void evaluate(const Packet& p) {', helper+'    void evaluate(const Packet& p) {')
source = source.replace('            if(i+1<f.passes) { submit_and_wait(); begin_list(); }', '''
            submit_and_wait();
            dump((i==0?color:(i%2==0?intermediate:result)).d11.Get(),f.frame,i,"in");
            dump(passOutput->d11.Get(),f.frame,i,"out");
            begin_list();''')
(folder/'extension.cpp').write_text(source)
(folder/'diagnostics.hpp').write_bytes((root/'native/neural/src/diagnostics.hpp').read_bytes())
build = (root/'.testruns/nr/build-overdrive.cmd').read_text()
build = build[:build.index('cl /nologo /O2 /EHsc /MT')]
build = build.replace('overdrive-check','multipass-debug').replace('build\\nr\\multipass-debug\\BepInEx\\TGNeuralRendering\\Native\\TGNeuralRendering.Native.dll','build\\nr\\multipass-debug\\TGNeuralRendering.Native.dll')
(root/'.testruns/nr/build-multipass-debug.cmd').write_text(build)
