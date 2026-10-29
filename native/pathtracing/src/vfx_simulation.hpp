// SPDX-License-Identifier: MIT
// Included inside tgpt::vfx, after the observer's binding helpers.
// Diagnostic evidence only. Raw-UAV compute programs are candidates, NOT identified VFX systems.
// Inputs are copied before the original dispatch, UAVs after it. No shader/draw is replayed.
namespace sim {
struct Program {
    std::vector<unsigned char> code;
    std::set<UINT> srvs, uavs, constants;
    bool rawOutput=false;
};
struct Snapshot {
    Binding binding;
    std::string role;
    ComPtr<ID3D11Buffer> staging;
    UINT bytes=0;
};
struct Packet {
    std::string hash, key;
    std::vector<Snapshot> inputs, outputs;
    UINT groups[3]{}, indirectOffset=0;
    bool indirect=false, deferred=false, bindingsComplete=true;
    uint64_t predicate=0;
    uint64_t serial=0, executions=0;
    int submittedAfter=-1;
};
struct State {
    std::mutex mutex;
    std::atomic<bool> enabled{false};
    ID3D11Device* device=nullptr;
    bool armed=false;
    std::map<std::string,Program> programs;
    std::map<std::string,std::shared_ptr<Packet>> latest;
    std::vector<std::shared_ptr<Packet>> current;
    std::atomic<uint64_t> serial{0}, faults{0}, missingPrograms{0};
    std::atomic<int> boundary{-1};
    std::atomic<bool> sampling{false};
};
inline State& data() { static auto* s=new State; return *s; }
inline void enable(ID3D11Device* device,bool on) {
    auto& s=data(); s.enabled=false;
    std::lock_guard<std::mutex> lock(s.mutex);
    s.latest.clear(); s.current.clear(); s.programs.clear(); s.device=device; s.armed=on;
    s.serial=0; s.faults=0; s.missingPrograms=0; s.boundary=-1; s.sampling=false; s.enabled=on;
}
inline void created(const void* code,size_t size,const std::string& hash,const std::string& stage) {
    if(!data().enabled) return;
    if(stage!="cs") {
        Program p; p.code.assign(static_cast<const unsigned char*>(code),static_cast<const unsigned char*>(code)+size);
        std::lock_guard<std::mutex> lock(data().mutex); data().programs.emplace(hash,std::move(p)); return;
    }
    // Use the system DXBC decoder only for declarations. Shader code stays in runtime evidence,
    // never in a generated source/header. No vendor DLL is disassembled.
    using Disassemble=HRESULT(WINAPI*)(LPCVOID,SIZE_T,UINT,LPCSTR,ID3DBlob**);
    static const auto decode=[] {
        auto module=LoadLibraryExW(L"d3dcompiler_47.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        return module ? reinterpret_cast<Disassemble>(GetProcAddress(module,"D3DDisassemble")):nullptr;
    }();
    ComPtr<ID3DBlob> text;
    if(!decode || FAILED(decode(code,size,0,nullptr,&text))) throw std::runtime_error("Compute declarations unavailable");
    Program p;
    std::istringstream lines(std::string(static_cast<const char*>(text->GetBufferPointer()),text->GetBufferSize()));
    std::string line;
    while(std::getline(lines,line)) {
        for(char& ch:line) if(ch>='A' && ch<='Z') ch=char(ch-'A'+'a');
        const auto begin=line.find_first_not_of(" \t");
        if(begin==std::string::npos || line.compare(begin,4,"dcl_")!=0) continue;
        const auto space=line.find(' ',begin); if(space==std::string::npos) continue;
        const auto declaration=line.substr(begin,space-begin);
        auto slot=[&](const char* prefix) -> UINT {
            auto at=line.rfind(std::string(" ")+prefix); if(at==std::string::npos) throw std::runtime_error("Unparsed compute declaration");
            return static_cast<UINT>(std::stoul(line.substr(at+1+std::strlen(prefix))));
        };
        if(declaration=="dcl_constantbuffer") p.constants.insert(slot("cb"));
        else if(declaration.find("dcl_resource")==0) p.srvs.insert(slot("t"));
        else if(declaration.find("dcl_uav")==0) {
            p.uavs.insert(slot("u")); if(declaration=="dcl_uav_raw") p.rawOutput=true;
        }
    }
    p.code.assign(static_cast<const unsigned char*>(code),static_cast<const unsigned char*>(code)+size);
    std::lock_guard<std::mutex> lock(data().mutex); data().programs.emplace(hash,std::move(p));
}
inline Snapshot snapshot(ID3D11DeviceContext* c,const Binding& binding,const char* role,bool copy) {
    Snapshot s; s.binding=binding; s.role=role;
    if(!binding.simulationBuffer) return s; // Texture binding: recorded in metadata, never copied.
    D3D11_BUFFER_DESC desc{}; binding.simulationBuffer->GetDesc(&desc); s.bytes=desc.ByteWidth;
    desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=desc.MiscFlags=desc.StructureByteStride=0;
    desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Device> device; c->GetDevice(&device);
    if(FAILED(device->CreateBuffer(&desc,nullptr,&s.staging))) throw std::runtime_error("Simulation readback allocation failed");
    if(copy) {
        c->CopyResource(s.staging.Get(),binding.simulationBuffer.Get());
        s.binding.simulationBuffer.Reset(); // The queued copy owns its inputs; do not pin game allocations afterward.
    }
    return s;
}
inline std::shared_ptr<Packet> begin(ID3D11DeviceContext* c,const Stage* shadow,bool complete,
                                      UINT x,UINT y,UINT z,ID3D11Buffer* args=nullptr,UINT offset=0) noexcept {
    try {
        if(!data().enabled || executingList) return {};
        ComPtr<ID3D11Device> device; c->GetDevice(&device); if(device.Get()!=data().device) return {};
        ShaderTag tag;
        if(shadow) tag=shadow->shader;
        else { ComPtr<ID3D11ComputeShader> shader; c->CSGetShader(&shader,nullptr,nullptr); tag=shader_tag(shader.Get()); }
        if(!tag.hash[0]) { ++data().missingPrograms; return {}; }
        Program p;
        { std::lock_guard<std::mutex> lock(data().mutex);
          auto it=data().programs.find(tag.hash); if(it==data().programs.end()) return {}; if(!it->second.rawOutput) return {}; p=it->second; }
        auto result=std::make_shared<Packet>(); result->hash=tag.hash; result->key=tag.hash;
        result->groups[0]=x; result->groups[1]=y; result->groups[2]=z;
        result->indirect=args!=nullptr; result->indirectOffset=offset;
        result->deferred=shadow!=nullptr; result->bindingsComplete=complete;
        if(!shadow) { ComPtr<ID3D11Predicate> predicate; BOOL value=FALSE; c->GetPredication(&predicate,&value); result->predicate=identity(predicate.Get()); }
        auto add=[&](const Binding& b,const char* role,bool output) {
            if(!b.resource) return;
            // Startup keeps the last full allocation snapshot for each writer. Transient input/CB views
            // must not turn every frame into a new retained task. Sampled dispatches are all kept in current.
            if(output) result->key+='|'+std::to_string(b.resource);
            auto snap=snapshot(c,b,role,!output);
            (output ? result->outputs:result->inputs).push_back(std::move(snap));
        };
        ComPtr<ID3D11DeviceContext1> c1; if(!shadow) c->QueryInterface(IID_PPV_ARGS(&c1));
        for(UINT slot:p.constants) {
            Binding b;
            if(shadow) { auto it=shadow->constants.find(slot); if(it!=shadow->constants.end()) b=it->second; }
            else { ComPtr<ID3D11Buffer> v; UINT first=0,count=4096;
                if(c1) c1->CSGetConstantBuffers1(slot,1,&v,&first,&count); else c->CSGetConstantBuffers(slot,1,&v);
                b=buffer_binding(v.Get(),slot,first,count); }
            add(b,"constant",false);
        }
        for(UINT slot:p.srvs) {
            Binding b;
            if(shadow) { auto it=shadow->srvs.find(slot); if(it!=shadow->srvs.end()) b=it->second; }
            else { ComPtr<ID3D11ShaderResourceView> v; c->CSGetShaderResources(slot,1,&v); b=view_binding(v.Get(),slot); }
            add(b,"srv",false);
        }
        for(UINT slot:p.uavs) {
            Binding b;
            if(shadow) { auto it=shadow->uavs.find(slot); if(it!=shadow->uavs.end()) b=it->second; }
            else { ComPtr<ID3D11UnorderedAccessView> v; c->CSGetUnorderedAccessViews(slot,1,&v); b=view_binding(v.Get(),slot); }
            add(b,"uav",true);
        }
        if(args) add(buffer_binding(args,0),"indirect",false);
        return result;
    } catch(...) { ++data().faults; return {}; }
}
inline void submit(const std::shared_ptr<Packet>& packet) {
    auto& s=data(); std::lock_guard<std::mutex> lock(s.mutex);
    if(!s.enabled) return;
    // Snapshot buffers in a reused command list are overwritten on its next execution. Do not
    // claim per-execution history: retain the last execution and explicitly reject reuse in analysis.
    packet->serial=++s.serial; ++packet->executions; packet->submittedAfter=s.boundary;
    s.latest[packet->key]=packet; if(s.sampling) s.current.push_back(packet);
}
inline void end(ID3D11DeviceContext* c,const std::shared_ptr<Packet>& packet) noexcept {
    if(!packet) return;
    try {
        for(auto& b:packet->outputs) if(b.staging) {
            c->CopyResource(b.staging.Get(),b.binding.simulationBuffer.Get()); b.binding.simulationBuffer.Reset();
        }
        if(c->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED) batch(c,contextKey,true)->simulations.push_back(packet);
        else submit(packet);
    } catch(...) { ++data().faults; }
}
inline std::shared_ptr<Packet> begin_context(ID3D11DeviceContext* c,UINT x,UINT y,UINT z,ID3D11Buffer* args=nullptr,UINT offset=0) noexcept {
    if(!data().enabled) return {};
    try {
        ComPtr<ID3D11Device> device; c->GetDevice(&device); if(device.Get()!=data().device) return {};
        auto pending=c->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED ? batch(c,contextKey,true):ComPtr<Batch>();
        auto packet=begin(c,pending ? &pending->shadow->stages[2]:nullptr,!pending || pending->shadow->complete,x,y,z,args,offset);
        if(packet && pending) packet->predicate=pending->shadow->predicate;
        return packet;
    } catch(...) { ++data().faults; return {}; }
}
inline void write(ID3D11DeviceContext* c,const std::filesystem::path& directory,int frame) {
    auto& s=data(); if(!s.enabled) return;
    std::lock_guard<std::mutex> lock(s.mutex);
    std::filesystem::create_directories(directory/L"simulation");
    std::filesystem::create_directories(directory/L"simulation"/L"programs");
    std::ofstream out(directory/("frame-"+std::to_string(frame)+"-simulation.json"));
    out.exceptions(std::ios::badbit|std::ios::failbit);
    out<<"{\"schema\":1,\"frame\":"<<frame<<",\"previousBoundary\":"<<s.boundary
       <<",\"diagnosticOnly\":true,\"particleCaptureComplete\":false,\"faults\":"<<s.faults
       <<",\"missingPrograms\":"<<s.missingPrograms<<",\"submittedThisSample\":"<<s.current.size()<<",\"packets\":[";
    std::map<uint64_t,std::shared_ptr<Packet>> packets;
    for(auto& pair:s.latest) packets[pair.second->serial]=pair.second;
    for(auto& p:s.current) packets[p->serial]=p;
    bool comma=false;
    for(auto& pair:packets) {
        const auto& p=*pair.second; if(comma) out<<','; comma=true;
        out<<"{\"serial\":"<<p.serial<<",\"submittedAfter\":"<<p.submittedAfter<<",\"executions\":"<<p.executions
           <<",\"deferred\":"<<(p.deferred ? "true":"false")<<",\"bindingsComplete\":"<<(p.bindingsComplete ? "true":"false")
           <<",\"predicate\":"<<p.predicate<<",\"cs\":"<<quote(p.hash)<<",\"groups\":["<<p.groups[0]<<','<<p.groups[1]<<','<<p.groups[2]
           <<"],\"indirect\":"<<(p.indirect ? "true":"false")<<",\"indirectOffset\":"<<p.indirectOffset<<",\"buffers\":[";
        bool bc=false; size_t index=0;
        auto dump=[&](const Snapshot& b) {
            if(bc) out<<','; bc=true;
            out<<"{\"role\":"<<quote(b.role)<<','<<b.binding.json<<",\"bytes\":"<<b.bytes;
            if(b.staging) {
                const auto name="simulation/f"+std::to_string(frame)+"-p"+std::to_string(p.serial)+"-b"+std::to_string(index)+".bin";
                D3D11_MAPPED_SUBRESOURCE mapped{};
                if(FAILED(c->Map(b.staging.Get(),0,D3D11_MAP_READ,0,&mapped))) throw std::runtime_error("Simulation snapshot map failed");
                try {
                    std::ofstream bin((directory/name).make_preferred(),std::ios::binary); bin.exceptions(std::ios::badbit|std::ios::failbit);
                    bin.write(static_cast<const char*>(mapped.pData),b.bytes); bin.close();
                    out<<",\"file\":"<<quote(name)<<",\"sha256\":"<<quote(sha256(mapped.pData,b.bytes));
                } catch(...) { c->Unmap(b.staging.Get(),0); throw; }
                c->Unmap(b.staging.Get(),0);
            }
            out<<'}'; ++index;
        };
        for(auto& b:p.inputs) dump(b); for(auto& b:p.outputs) dump(b);
        out<<"]}";
    }
    out<<"]}\n"; out.close();
    for(auto& pair:s.programs) {
        const auto path=directory/L"simulation"/L"programs"/(pair.first+".dxbc");
        if(std::filesystem::exists(path)) continue;
        std::ofstream bin(path,std::ios::binary); bin.exceptions(std::ios::badbit|std::ios::failbit);
        bin.write(reinterpret_cast<const char*>(pair.second.code.data()),pair.second.code.size());
    }
    s.current.clear(); s.boundary=frame;
}
} // namespace sim
