#include "vfx_test_support.hpp"
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=2) throw std::runtime_error("output directory required");
        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> c;
        D3D_FEATURE_LEVEL feature=D3D_FEATURE_LEVEL_11_1;
        check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,&feature,1,D3D11_SDK_VERSION,&device,nullptr,&c)),"D3D11 device");
        TestHost host; device.As(&host.device); c.As(&host.context);
        check(MH_Initialize()==MH_OK,"MinHook initialized"); install(host);
        for(auto& f:state().hookFailures) std::printf("HOOK FAILURE: %s\n",f.c_str());
        check(state().hookFailures.empty(),"all hooks installed");
        check(sha256("abc",3)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA-256 independent known answer");
        auto cb=compile("RWStructuredBuffer<float4> x:register(u0); [numthreads(1,1,1)] void main(uint3 p:SV_DispatchThreadID){x[p.x]+=float4(17,0.25,0.5,1);}","cs_5_0");
        auto vb=compile("StructuredBuffer<float4> x:register(t0); float4 main(uint i:SV_VertexID):SV_Position{return float4(x[0].y,i,0,1);}","vs_5_0");
        auto pb=compile("float4 main():SV_Target{return float4(1,0,1,1);}","ps_5_0");
        ComPtr<ID3D11ComputeShader> cs; ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps;
        device->CreateComputeShader(cb->GetBufferPointer(),cb->GetBufferSize(),nullptr,&cs);
        device->CreateVertexShader(vb->GetBufferPointer(),vb->GetBufferSize(),nullptr,&vs);
        device->CreatePixelShader(pb->GetBufferPointer(),pb->GetBufferSize(),nullptr,&ps);
        check(shader_tag(cs.Get()).hash[0] && shader_tag(vs.Get()).hash[0] && shader_tag(ps.Get()).hash[0],"VS PS CS tagged at creation");
        // Our own shader is explicitly marked as a fixture candle. No production whitelist entry added.
        auto tag=shader_tag(vs.Get()); tag.candle=true; vs->SetPrivateData(shaderKey,sizeof(tag),&tag);
        Data a=data(device.Get()), b=data(device.Get());
        auto issue=[&](ID3D11DeviceContext* ctx,Data& value,bool draw) {
            ctx->CSSetShader(cs.Get(),nullptr,0); auto u=value.write.Get(); ctx->CSSetUnorderedAccessViews(0,1,&u,nullptr); ctx->Dispatch(4,1,1);
            u=nullptr; ctx->CSSetUnorderedAccessViews(0,1,&u,nullptr);
            if(draw) { auto r=value.read.Get(); ctx->VSSetShaderResources(0,1,&r); ctx->VSSetShader(vs.Get(),nullptr,0); ctx->PSSetShader(ps.Get(),nullptr,0); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); ctx->Draw(3,0); r=nullptr; ctx->VSSetShaderResources(0,1,&r); }
        };
        issue(c.Get(),a,true); check(state().draws.empty(),"disabled observer records no dispatch/draw");
        check(read_value(device.Get(),c.Get(),a.buffer.Get())==17,"unobserved shader result");
        check(configure(argv[1])==0,"capture starts"); frame(0);
        D3D11_QUERY_DESC queryDesc{D3D11_QUERY_PIPELINE_STATISTICS,0}; ComPtr<ID3D11Query> query;
        check(SUCCEEDED(device->CreateQuery(&queryDesc,&query)),"pipeline statistics query");
        c->Begin(query.Get()); issue(c.Get(),a,true); c->End(query.Get());
        D3D11_QUERY_DATA_PIPELINE_STATISTICS stats{}; const DWORD deadline=GetTickCount()+5000;
        HRESULT queryResult=S_FALSE;
        while((queryResult=c->GetData(query.Get(),&stats,sizeof(stats),0))==S_FALSE && static_cast<LONG>(GetTickCount()-deadline)<0) Sleep(1);
        check(queryResult==S_OK && stats.IAVertices==3,"observer forwards the draw's three vertices exactly once");
        trace_marker(); issue(c.Get(),b,false);
        check(state().draws.size()==4,"immediate dispatch draw trace and offscreen dispatch recorded once");
        check(read_value(device.Get(),c.Get(),a.buffer.Get())==34 && read_value(device.Get(),c.Get(),b.buffer.Get())==17,"observer preserves GPU results");
        frame(1); check(state().completedFrame==1,"frame written");
        ComPtr<ID3D11DeviceContext> deferred; device->CreateDeferredContext(0,&deferred);
        check(batch(deferred.Get(),contextKey,false)->shadow->complete,"new deferred context state known");
        D3D11_BUFFER_DESC constantDesc{}; constantDesc.ByteWidth=1024; constantDesc.Usage=D3D11_USAGE_DEFAULT; constantDesc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        ComPtr<ID3D11Buffer> constants; device->CreateBuffer(&constantDesc,nullptr,&constants);
        ComPtr<ID3D11DeviceContext1> dc1; deferred.As(&dc1); UINT firstConstant=16,numConstants=16; auto constant=constants.Get();
        dc1->VSSetConstantBuffers1(3,1,&constant,&firstConstant,&numConstants);
        issue(deferred.Get(),a,true);
        check(state().draws.empty(),"deferred recording is not immediate execution");
        ComPtr<ID3D11CommandList> list; deferred->FinishCommandList(TRUE,&list);
        c->ExecuteCommandList(list.Get(),TRUE); c->ExecuteCommandList(list.Get(),FALSE);
        check(state().draws.size()==4 && state().reusedLists==1,"reused command list records each execution once");
        check(state().draws[0]->metadata.find("\"bindingsComplete\":true")!=std::string::npos && state().draws[0]->deferred,"deferred provenance and completeness retained");
        check(state().draws[0]->metadata.find("\"resource\":{\"id\":"+std::to_string(identity(a.buffer.Get())))!=std::string::npos,"deferred output resource identity retained");
        check(state().draws[1]->metadata.find("\"firstConstant\":16,\"numConstants\":16")!=std::string::npos,"deferred constant-buffer range retained");
        check(read_value(device.Get(),c.Get(),a.buffer.Get())==68,"two deferred dispatch executions, no observer replay");
        frame(2);
        issue(deferred.Get(),b,false); deferred->FinishCommandList(FALSE,&list);
        check(batch(deferred.Get(),contextKey,false)->shadow->stages[2].shader.id==0,"FinishCommandList FALSE resets tracked state");
        c->ExecuteCommandList(list.Get(),TRUE); frame(3);
        // Indirect dispatch records the argument allocation/offset, never a guessed count.
        UINT args[4]={4,1,1,0}; D3D11_BUFFER_DESC ad{}; ad.ByteWidth=sizeof(args); ad.Usage=D3D11_USAGE_DEFAULT; ad.MiscFlags=D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
        D3D11_SUBRESOURCE_DATA init{args,0,0}; ComPtr<ID3D11Buffer> indirect; device->CreateBuffer(&ad,&init,&indirect);
        c->CSSetShader(cs.Get(),nullptr,0); auto u=a.write.Get(); c->CSSetUnorderedAccessViews(0,1,&u,nullptr); c->DispatchIndirect(indirect.Get(),0);
        ComPtr<ID3D11UnorderedAccessView> after; c->CSGetUnorderedAccessViews(0,1,&after); check(after.Get()==u,"UAV binding preserved");
        check(state().draws.size()==1 && state().draws[0]->metadata.find("\"groups\":null")!=std::string::npos,"indirect counts explicitly unknown");
        frame(4);
        // A list with only binding work is not a stale/missing dispatch. A list recorded while disabled is.
        dc1->VSSetConstantBuffers1(3,1,&constant,&firstConstant,&numConstants);
        deferred->FinishCommandList(FALSE,&list); c->ExecuteCommandList(list.Get(),TRUE);
        check(state().unknownLists==0 && state().staleLists==0,"empty command list has complete metadata");
        frame(5); configure(nullptr);
        issue(deferred.Get(),b,false); deferred->FinishCommandList(FALSE,&list);
        check(configure(argv[1])==0,"new capture epoch"); c->ExecuteCommandList(list.Get(),TRUE);
        check(state().unknownLists==1,"unobserved cached list explicitly fails coverage");
        frame(6); configure(nullptr);
        check(state().faults==0,"no observer exceptions");
        MH_DisableHook(MH_ALL_HOOKS); MH_Uninitialize(); std::puts("VFX observer GPU test passed"); return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
