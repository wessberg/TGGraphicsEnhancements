// SPDX-License-Identifier: MIT
// Opt-in D3D11 evidence for PT-71. Included by extension.cpp (no bridge/build-system change).
// Default observer is metadata-only. The separately gated simulation diagnostic reads buffers, never textures.
// Neither mode replays a draw/dispatch or changes a pipeline binding.
// All sizeable storage is on the heap. Deferred bindings are observed at their setters.
#pragma once
#include <tgb/bridge_api.hpp>
#include <wrl/client.h>
#include <bcrypt.h>
#include <d3dcompiler.h>
#include <atomic>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <stdexcept>
#include <vector>
#include <set>
#include <type_traits>
#include "vfx_programs.hpp"

namespace tgpt::vfx {
using Microsoft::WRL::ComPtr;
inline const GUID objectKey={0x857d3401,0xa224,0x4539,{0x8f,0xc3,0x41,0x09,0xf1,0x32,0xc5,0x42}};
inline const GUID shaderKey={0x857d3402,0xa224,0x4539,{0x8f,0xc3,0x41,0x09,0xf1,0x32,0xc5,0x42}};
inline const GUID listKey={0x857d3403,0xa224,0x4539,{0x8f,0xc3,0x41,0x09,0xf1,0x32,0xc5,0x42}};
inline const GUID contextKey={0x857d3404,0xa224,0x4539,{0x8f,0xc3,0x41,0x09,0xf1,0x32,0xc5,0x42}};
struct ShaderTag { uint64_t id=0; char hash[65]{}; bool candle=false; };
namespace sim { struct Packet; bool enabled(); void created(const void*,size_t,const std::string&,const std::string&); }
struct Binding { uint64_t resource=0; std::string json; ComPtr<ID3D11Buffer> simulationBuffer; };
struct Stage { ShaderTag shader; std::map<UINT,Binding> srvs, constants, uavs; };
struct Shadow {
    std::array<Stage,3> stages; // VS, PS, CS. Heap-owned by the context's Batch.
    std::map<UINT,Binding> vertices, omUavs;
    Binding indices;
    bool complete=false;
    uint64_t predicate=0;
};
struct Draw {
    uint64_t context=0, sequence=0, list=0, epoch=0; bool deferred=false;
    std::string metadata;

};
struct Batch final : IUnknown {
    std::atomic<ULONG> refs{1};
    std::vector<std::shared_ptr<Draw>> draws;
    std::vector<std::shared_ptr<sim::Packet>> simulations;
    uint64_t calls=0, dispatches=0, executions=0, epoch=0;
    bool unobserved=false;
    std::unique_ptr<Shadow> shadow=std::make_unique<Shadow>();
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if(!out) return E_POINTER; *out=nullptr;
        if(iid!=__uuidof(IUnknown)) return E_NOINTERFACE;
        *out=static_cast<IUnknown*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n=--refs; if(!n) delete this; return n; }
};
struct Program { std::string stage, hash, variants; size_t bytes=0; uint64_t made=0; };
struct State {
    std::atomic<tgb::Host*> host{nullptr};
    std::atomic<ID3D11Device*> device{nullptr}; // borrowed, compared only; device generation is separately recorded
    std::atomic<uint32_t> generation{0};
    std::mutex mutex, identityMutex;
    std::recursive_mutex installMutex;
    std::atomic<bool> active{false};
    std::atomic<uint64_t> nextId{1}, epoch{0}, faults{0}, boundaryDrops{0};
    std::atomic<int> completedFrame{-1};
    std::map<std::string,Program> programs;
    std::vector<std::string> hookFailures;
    std::vector<std::shared_ptr<Draw>> draws;
    std::filesystem::path directory;
    uint64_t calls=0, dispatches=0, sequence=0, executes=0, unknownLists=0, reusedLists=0, staleLists=0;
    int previousFrame=-1;
    std::atomic<bool> installed{false};
};
inline thread_local bool executingList=false;
inline State& state() { static auto* s=new State; return *s; }
inline std::string quote(const std::string& value) {
    std::ostringstream o; o<<'"';
    for(unsigned char c:value) {
        if(c=='"' || c=='\\') o<<'\\'<<char(c);
        else if(c<32) o<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<unsigned(c)<<std::dec;
        else o<<char(c);
    }
    o<<'"'; return o.str();
}
inline std::string sha256(const void* data, size_t bytes) {
    using Hash=NTSTATUS(WINAPI*)(BCRYPT_ALG_HANDLE,PUCHAR,ULONG,PUCHAR,ULONG,PUCHAR,ULONG);
    static const auto fn=[] { auto m=LoadLibraryExW(L"bcrypt.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        return m ? reinterpret_cast<Hash>(GetProcAddress(m,"BCryptHash")) : nullptr; }();
    if(!fn || bytes>0xffffffffu) throw std::runtime_error("SHA256 unavailable");
    std::array<unsigned char,32> digest{};
    if(fn(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,(PUCHAR)data,ULONG(bytes),digest.data(),ULONG(digest.size()))<0)
        throw std::runtime_error("SHA256 failed");
    const char* hex="0123456789abcdef"; std::string text; text.reserve(64);
    for(auto c:digest) { text+=hex[c>>4]; text+=hex[c&15]; } return text;
}
inline uint64_t identity(ID3D11DeviceChild* object) {
    if(!object) return 0;
    std::lock_guard<std::mutex> guard(state().identityMutex);
    uint64_t id=0; UINT size=sizeof(id);
    if(SUCCEEDED(object->GetPrivateData(objectKey,&size,&id)) && size==sizeof(id)) return id;
    id=state().nextId++;
    if(FAILED(object->SetPrivateData(objectKey,sizeof(id),&id))) throw std::runtime_error("Object tag failed");
    return id;
}
inline ShaderTag shader_tag(ID3D11DeviceChild* shader) {
    ShaderTag t{}; if(shader) { UINT n=sizeof(t); if(FAILED(shader->GetPrivateData(shaderKey,&n,&t))) t={}; } return t;
}
inline void created(ID3D11Device* device, ID3D11DeviceChild* shader, const void* code, size_t bytes, const char* stage) noexcept {
    try {
        auto& s=state(); if(device!=s.device || !shader) return;
        Program p; p.hash=sha256(code,bytes); p.stage=stage; p.bytes=bytes;
        try { sim::created(code,bytes,p.hash,p.stage); } catch(...) { ++s.faults; }
        for(const auto& k:candlePrograms) if(p.hash==k.hash && bytes==k.bytes && p.stage==k.stage) p.variants=k.variants;
        ShaderTag tag; tag.id=identity(shader); tag.candle=!p.variants.empty();
        std::memcpy(tag.hash,p.hash.c_str(),65);
        if(FAILED(shader->SetPrivateData(shaderKey,sizeof(tag),&tag))) throw std::runtime_error("Shader tag failed");
        std::lock_guard<std::mutex> guard(s.mutex);
        auto& entry=s.programs[p.hash]; const auto count=entry.made; entry=std::move(p); entry.made=count+1;
    } catch(...) { ++state().faults; }
}
inline bool observes(ID3D11DeviceContext* context) {
    if(executingList || !state().active.load(std::memory_order_relaxed)) return false;
    ComPtr<ID3D11Device> d; context->GetDevice(&d); return d.Get()==state().device;
}
inline ComPtr<Batch> batch(ID3D11DeviceChild* object, const GUID& key, bool make) {
    ComPtr<Batch> value; UINT size=sizeof(IUnknown*);
    if(FAILED(object->GetPrivateData(key,&size,value.GetAddressOf())) && make) {
        value.Attach(new Batch);
        if(FAILED(object->SetPrivateDataInterface(key,value.Get()))) throw std::runtime_error("Batch tag failed");
    }
    return value;
}
inline void resource(std::ostream& out, ID3D11Resource* r) {
    if(!r) { out<<"null"; return; }
    D3D11_RESOURCE_DIMENSION dim{}; r->GetType(&dim);
    out<<"{\"id\":"<<identity(r)<<",\"address\":"<<quote(std::to_string(reinterpret_cast<uintptr_t>(r)))<<",\"dimension\":"<<dim;
    ComPtr<ID3D11Buffer> b;
    if(SUCCEEDED(r->QueryInterface(IID_PPV_ARGS(&b)))) {
        D3D11_BUFFER_DESC d{}; b->GetDesc(&d);
        out<<",\"bytes\":"<<d.ByteWidth<<",\"stride\":"<<d.StructureByteStride<<",\"bind\":"<<d.BindFlags
           <<",\"misc\":"<<d.MiscFlags;
    } else {
        ComPtr<ID3D11Texture2D> t;
        if(SUCCEEDED(r->QueryInterface(IID_PPV_ARGS(&t)))) {
            D3D11_TEXTURE2D_DESC d{}; t->GetDesc(&d);
            out<<",\"width\":"<<d.Width<<",\"height\":"<<d.Height<<",\"mips\":"<<d.MipLevels
               <<",\"array\":"<<d.ArraySize<<",\"format\":"<<d.Format<<",\"misc\":"<<d.MiscFlags;
        }
    }
    out<<'}';
}
// Metadata mode retains no COM references. Simulation mode holds bound buffers until copies are queued.
// IDs are private lifetime tags, never pointer-derived particle IDs.
inline Binding buffer_binding(ID3D11Buffer* b, UINT slot, UINT first=0, UINT count=4096) {
    Binding result; if(!b) return result;
    result.resource=identity(b); if(sim::enabled()) result.simulationBuffer=b; std::ostringstream out;
    out<<"\"slot\":"<<slot<<",\"firstConstant\":"<<first<<",\"numConstants\":"<<count<<",\"resource\":";
    resource(out,b); result.json=out.str(); return result;
}
template<class View> inline Binding view_binding(View* v, UINT slot) {
    Binding result; if(!v) return result;
    ComPtr<ID3D11Resource> r; v->GetResource(&r); result.resource=identity(r.Get());
    if(sim::enabled()) r.As(&result.simulationBuffer);
    std::ostringstream out; out<<"\"slot\":"<<slot<<",\"view\":"<<identity(v)<<",\"resource\":"; resource(out,r.Get());
    if constexpr(std::is_same_v<View,ID3D11ShaderResourceView>) {
        D3D11_SHADER_RESOURCE_VIEW_DESC d{}; v->GetDesc(&d);
        out<<",\"format\":"<<d.Format<<",\"dimension\":"<<d.ViewDimension;
        if(d.ViewDimension==D3D11_SRV_DIMENSION_BUFFER) out<<",\"firstElement\":"<<d.Buffer.FirstElement<<",\"elements\":"<<d.Buffer.NumElements;
        if(d.ViewDimension==D3D11_SRV_DIMENSION_BUFFEREX) out<<",\"firstElement\":"<<d.BufferEx.FirstElement<<",\"elements\":"<<d.BufferEx.NumElements<<",\"flags\":"<<d.BufferEx.Flags;
    } else {
        D3D11_UNORDERED_ACCESS_VIEW_DESC d{}; v->GetDesc(&d);
        out<<",\"format\":"<<d.Format<<",\"dimension\":"<<d.ViewDimension;
        if(d.ViewDimension==D3D11_UAV_DIMENSION_BUFFER) out<<",\"firstElement\":"<<d.Buffer.FirstElement<<",\"elements\":"<<d.Buffer.NumElements<<",\"flags\":"<<d.Buffer.Flags;
    }
    result.json=out.str(); return result;
}
inline void binding_array(std::ostream& out,const std::map<UINT,Binding>& values) {
    out<<'['; bool comma=false;
    for(const auto& item:values) if(item.second.resource) { if(comma) out<<','; comma=true; out<<'{'<<item.second.json<<'}'; }
    out<<']';
}
inline void stage_bindings(std::ostream& out,ID3D11DeviceContext* c,int stage,const Shadow* shadow) {
    if(shadow) {
        const auto& data=shadow->stages[stage];
        out<<"{\"srvs\":"; binding_array(out,data.srvs);
        out<<",\"constantBuffers\":"; binding_array(out,data.constants);
        if(stage==2) { out<<",\"uavs\":"; binding_array(out,data.uavs); }
        out<<'}'; return;
    }
    out<<"{\"srvs\":["; bool comma=false;
    for(UINT slot=0;slot<D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;++slot) {
        ComPtr<ID3D11ShaderResourceView> v;
        if(stage==0) c->VSGetShaderResources(slot,1,&v); else if(stage==1) c->PSGetShaderResources(slot,1,&v); else c->CSGetShaderResources(slot,1,&v);
        if(!v) continue; if(comma) out<<','; comma=true;
        out<<'{'<<view_binding(v.Get(),slot).json<<'}';
    }
    out<<"],\"constantBuffers\":["; comma=false;
    ComPtr<ID3D11DeviceContext1> c1; c->QueryInterface(IID_PPV_ARGS(&c1));
    for(UINT slot=0;slot<D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;++slot) {
        ComPtr<ID3D11Buffer> b; UINT first=0,count=4096;
        if(c1) {
            if(stage==0) c1->VSGetConstantBuffers1(slot,1,&b,&first,&count);
            else if(stage==1) c1->PSGetConstantBuffers1(slot,1,&b,&first,&count);
            else c1->CSGetConstantBuffers1(slot,1,&b,&first,&count);
        } else {
            if(stage==0) c->VSGetConstantBuffers(slot,1,&b); else if(stage==1) c->PSGetConstantBuffers(slot,1,&b); else c->CSGetConstantBuffers(slot,1,&b);
        }
        if(!b) continue; if(comma) out<<','; comma=true;
        out<<'{'<<buffer_binding(b.Get(),slot,first,count).json<<'}';
    }
    out<<']';
    if(stage==2) {
        out<<",\"uavs\":["; comma=false;
        // The D3D11.1 context exposes 64 UAV slots; D3D11.0 exposes eight.
        const UINT slots=c1 ? D3D11_1_UAV_SLOT_COUNT : D3D11_PS_CS_UAV_REGISTER_COUNT;
        for(UINT slot=0;slot<slots;++slot) {
            ComPtr<ID3D11UnorderedAccessView> v; c->CSGetUnorderedAccessViews(slot,1,&v);
            if(!v) continue; if(comma) out<<','; comma=true; out<<'{'<<view_binding(v.Get(),slot).json<<'}';
        }
        out<<']';
    }
    out<<'}';
}
#include "vfx_simulation.hpp"
inline bool sim::enabled() { return data().enabled; }
inline void append(ID3D11DeviceContext* c,std::shared_ptr<Draw> event) {
    auto& s=state(); event->context=identity(c);
    if(!s.active || event->epoch!=s.epoch) { ++s.boundaryDrops; return; }
    event->deferred=c->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED;
    if(event->deferred) {
        auto p=batch(c,contextKey,true);
        if(p->epoch!=s.epoch) { if(!p->draws.empty()) p->unobserved=true; p->draws.clear(); p->calls=p->dispatches=0; p->epoch=s.epoch; }
        p->draws.push_back(std::move(event));
    } else {
        std::lock_guard<std::mutex> guard(s.mutex);
        if(!s.active || event->epoch!=s.epoch) { ++s.boundaryDrops; return; }
        event->sequence=++s.sequence; s.draws.push_back(std::move(event));
    }
}
inline void observe(ID3D11DeviceContext* c,const char* kind,UINT count,UINT instances,UINT first,INT base,UINT firstInstance,
                    ID3D11Buffer* args=nullptr,UINT argOffset=0) noexcept {
    try {
        if(!observes(c)) {
            if(!executingList && c->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED) {
                ComPtr<ID3D11Device> device; c->GetDevice(&device);
                if(device.Get()==state().device) batch(c,contextKey,true)->unobserved=true;
            }
            return;
        }
        const auto epoch=state().epoch.load();
        const bool compute=std::strncmp(kind,"dispatch",8)==0;
        const bool deferred=c->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED;
        auto pending=deferred ? batch(c,contextKey,true) : ComPtr<Batch>();
        const Shadow* shadow=pending ? pending->shadow.get() : nullptr;
        ShaderTag vt,pt,ct;
        if(shadow) { vt=shadow->stages[0].shader; pt=shadow->stages[1].shader; ct=shadow->stages[2].shader; }
        else if(compute) { ComPtr<ID3D11ComputeShader> cs; c->CSGetShader(&cs,nullptr,nullptr); ct=shader_tag(cs.Get()); }
        else { ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps; c->VSGetShader(&vs,nullptr,nullptr); c->PSGetShader(&ps,nullptr,nullptr); vt=shader_tag(vs.Get()); pt=shader_tag(ps.Get()); }
        auto event=std::make_shared<Draw>(); event->epoch=epoch; std::ostringstream out;
        out<<"\"kind\":"<<quote(kind)<<",\"bindingsComplete\":"<<(!shadow || shadow->complete ? "true":"false")
           <<",\"bindingSource\":"<<quote(shadow ? "deferred-setters":"immediate-getters")
           <<",\"candle\":"<<(!compute && (vt.candle || pt.candle) ? "true":"false");
        if(compute) {
            out<<",\"cs\":"<<quote(ct.hash)<<",\"csId\":"<<ct.id<<",\"groups\":";
            if(args) out<<"null"; else out<<'['<<count<<','<<instances<<','<<first<<']';
            out<<",\"CS\":"; stage_bindings(out,c,2,shadow);
        } else {
            out<<",\"vs\":"<<quote(vt.hash)<<",\"ps\":"<<quote(pt.hash)<<",\"vsId\":"<<vt.id<<",\"psId\":"<<pt.id;
            out<<",\"count\":"; if(args || std::strcmp(kind,"auto")==0) out<<"null"; else out<<count;
            out<<",\"instances\":"; if(args || std::strcmp(kind,"auto")==0) out<<"null"; else out<<instances;
            out<<",\"first\":"<<first<<",\"baseVertex\":"<<base<<",\"firstInstance\":"<<firstInstance;
            out<<",\"VS\":"; stage_bindings(out,c,0,shadow);
            out<<",\"PS\":"; stage_bindings(out,c,1,shadow);
            out<<",\"vertices\":";
            if(shadow) binding_array(out,shadow->vertices);
            else {
                out<<'['; bool comma=false;
                for(UINT slot=0;slot<D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;++slot) {
                    ComPtr<ID3D11Buffer> b; UINT stride=0,offset=0; c->IAGetVertexBuffers(slot,1,&b,&stride,&offset);
                    if(!b) continue; if(comma) out<<','; comma=true;
                    out<<"{\"slot\":"<<slot<<",\"stride\":"<<stride<<",\"offset\":"<<offset<<",\"resource\":"; resource(out,b.Get()); out<<'}';
                }
                out<<']';
            }
            out<<",\"indices\":";
            if(shadow) { if(shadow->indices.resource) out<<'{'<<shadow->indices.json<<'}'; else out<<"null"; }
            else {
                ComPtr<ID3D11Buffer> b; DXGI_FORMAT format{}; UINT offset=0; c->IAGetIndexBuffer(&b,&format,&offset);
                out<<"{\"format\":"<<format<<",\"offset\":"<<offset<<",\"resource\":"; resource(out,b.Get()); out<<'}';
            }
        }
        if(args) { out<<",\"indirectOffset\":"<<argOffset<<",\"indirect\":"; resource(out,args); }
        uint64_t pred=shadow ? shadow->predicate : 0;
        if(!shadow) { ComPtr<ID3D11Predicate> p; BOOL v=FALSE; c->GetPredication(&p,&v); pred=identity(p.Get()); }
        out<<",\"predicate\":"<<pred; // Submission != execution when predicated or indirect groups are zero.
        event->metadata=out.str(); append(c,std::move(event));
    } catch(...) { ++state().faults; }
}
// Deferred Get* pipeline calls are unsupported. Track the application's bindings without setting any.
// Simulation diagnostics additionally retain the buffers of the current bindings for deferred contexts.
inline ComPtr<Batch> deferred_state(ID3D11DeviceContext* c) {
    if(executingList) return {};
    ComPtr<ID3D11Device> d; c->GetDevice(&d);
    if(d.Get()!=state().device || c->GetType()!=D3D11_DEVICE_CONTEXT_DEFERRED) return {};
    return batch(c,contextKey,true);
}
inline void clear_shadow(ID3D11DeviceContext* c,bool known=true) {
    if(auto p=deferred_state(c)) { p->shadow=std::make_unique<Shadow>(); p->shadow->complete=known; }
}
inline void bound_shader(ID3D11DeviceContext* c,int stage,ID3D11DeviceChild* shader) noexcept {
    try { if(auto p=deferred_state(c)) p->shadow->stages[stage].shader=shader_tag(shader); } catch(...) { ++state().faults; }
}
inline void bound_srvs(ID3D11DeviceContext* c,int stage,UINT start,UINT n,ID3D11ShaderResourceView* const* views) noexcept {
    try { if(auto p=deferred_state(c)) for(UINT i=0;i<n;++i) {
        auto value=view_binding(views ? views[i] : nullptr,start+i);
        // Buffer hazards unbind input views. Deferred texture subresource ranges are outside this buffer diagnostic.
        for(const auto* outputs:{&p->shadow->stages[2].uavs,&p->shadow->omUavs})
            for(const auto& u:*outputs) if(value.resource && value.resource==u.second.resource) { value={}; }
        p->shadow->stages[stage].srvs[start+i]=std::move(value);
    } } catch(...) { ++state().faults; }
}
inline void bound_constants(ID3D11DeviceContext* c,int stage,UINT start,UINT n,ID3D11Buffer* const* b,const UINT* first=nullptr,const UINT* count=nullptr) noexcept {
    try { if(auto p=deferred_state(c)) for(UINT i=0;i<n;++i)
        p->shadow->stages[stage].constants[start+i]=buffer_binding(b ? b[i] : nullptr,start+i,first ? first[i]:0,count ? count[i]:4096);
    } catch(...) { ++state().faults; }
}
inline void bound_uavs(ID3D11DeviceContext* c,UINT start,UINT n,ID3D11UnorderedAccessView* const* views,const UINT* counts) noexcept {
    try { if(auto p=deferred_state(c)) for(UINT i=0;i<n;++i) {
        auto value=view_binding(views ? views[i]:nullptr,start+i);
        if(value.resource) {
            if(counts) value.json+=",\"initialCount\":"+std::to_string(counts[i]);
            for(auto& stage:p->shadow->stages) for(auto& read:stage.srvs) if(read.second.resource==value.resource) read.second={};
        }
        p->shadow->stages[2].uavs[start+i]=std::move(value);
    } } catch(...) { ++state().faults; }
}

// Originals are distinct for immediate/deferred implementation addresses. Shared addresses
// reuse the already installed detour; a new address never overwrites a live trampoline.
template<int Lane> struct ContextHooks {
    inline static void* real[149]{};
    static void STDMETHODCALLTYPE draw(ID3D11DeviceContext* c,UINT n,UINT f) { observe(c,"draw",n,1,f,0,0); reinterpret_cast<decltype(&draw)>(real[13])(c,n,f); }
    static void STDMETHODCALLTYPE indexed(ID3D11DeviceContext* c,UINT n,UINT f,INT b) { observe(c,"indexed",n,1,f,b,0); reinterpret_cast<decltype(&indexed)>(real[12])(c,n,f,b); }
    static void STDMETHODCALLTYPE instanced(ID3D11DeviceContext* c,UINT n,UINT i,UINT f,UINT fi) { observe(c,"instanced",n,i,f,0,fi); reinterpret_cast<decltype(&instanced)>(real[21])(c,n,i,f,fi); }
    static void STDMETHODCALLTYPE indexed_instanced(ID3D11DeviceContext* c,UINT n,UINT i,UINT f,INT b,UINT fi) { observe(c,"indexedInstanced",n,i,f,b,fi); reinterpret_cast<decltype(&indexed_instanced)>(real[20])(c,n,i,f,b,fi); }
    static void STDMETHODCALLTYPE indirect(ID3D11DeviceContext* c,ID3D11Buffer* b,UINT o) { observe(c,"indirect",0,0,0,0,0,b,o); reinterpret_cast<decltype(&indirect)>(real[40])(c,b,o); }
    static void STDMETHODCALLTYPE indexed_indirect(ID3D11DeviceContext* c,ID3D11Buffer* b,UINT o) { observe(c,"indexedIndirect",0,0,0,0,0,b,o); reinterpret_cast<decltype(&indexed_indirect)>(real[39])(c,b,o); }
    static void STDMETHODCALLTYPE automatic(ID3D11DeviceContext* c) { observe(c,"auto",0,0,0,0,0); reinterpret_cast<decltype(&automatic)>(real[38])(c); }
    static void STDMETHODCALLTYPE dispatch(ID3D11DeviceContext* c,UINT x,UINT y,UINT z) { observe(c,"dispatch",x,y,z,0,0); auto capture=sim::begin_context(c,x,y,z); reinterpret_cast<decltype(&dispatch)>(real[41])(c,x,y,z); sim::end(c,capture); }
    static void STDMETHODCALLTYPE dispatch_indirect(ID3D11DeviceContext* c,ID3D11Buffer* b,UINT o) { observe(c,"dispatchIndirect",0,0,0,0,0,b,o); auto capture=sim::begin_context(c,0,0,0,b,o); reinterpret_cast<decltype(&dispatch_indirect)>(real[42])(c,b,o); sim::end(c,capture); }
#define VFX_SETTERS(prefix,type,stage,shaderSlot,srvSlot,cbSlot,cb1Slot) \
    static void STDMETHODCALLTYPE prefix##_shader(ID3D11DeviceContext* c,type* shader,ID3D11ClassInstance* const* classes,UINT n) { \
        reinterpret_cast<decltype(&prefix##_shader)>(real[shaderSlot])(c,shader,classes,n); bound_shader(c,stage,shader); } \
    static void STDMETHODCALLTYPE prefix##_srvs(ID3D11DeviceContext* c,UINT start,UINT n,ID3D11ShaderResourceView* const* views) { \
        reinterpret_cast<decltype(&prefix##_srvs)>(real[srvSlot])(c,start,n,views); bound_srvs(c,stage,start,n,views); } \
    static void STDMETHODCALLTYPE prefix##_cb(ID3D11DeviceContext* c,UINT start,UINT n,ID3D11Buffer* const* b) { \
        reinterpret_cast<decltype(&prefix##_cb)>(real[cbSlot])(c,start,n,b); bound_constants(c,stage,start,n,b); } \
    static void STDMETHODCALLTYPE prefix##_cb1(ID3D11DeviceContext1* c,UINT start,UINT n,ID3D11Buffer* const* b,const UINT* first,const UINT* count) { \
        reinterpret_cast<decltype(&prefix##_cb1)>(real[cb1Slot])(c,start,n,b,first,count); bound_constants(c,stage,start,n,b,first,count); }
    VFX_SETTERS(vs,ID3D11VertexShader,0,11,25,7,119)
    VFX_SETTERS(ps,ID3D11PixelShader,1,9,8,16,123)
    VFX_SETTERS(cs,ID3D11ComputeShader,2,69,67,71,124)
#undef VFX_SETTERS
    static void STDMETHODCALLTYPE uavs(ID3D11DeviceContext* c,UINT start,UINT n,ID3D11UnorderedAccessView* const* v,const UINT* counts) {
        reinterpret_cast<decltype(&uavs)>(real[68])(c,start,n,v,counts); bound_uavs(c,start,n,v,counts);
    }
    static void STDMETHODCALLTYPE vertices(ID3D11DeviceContext* c,UINT start,UINT n,ID3D11Buffer* const* b,const UINT* strides,const UINT* offsets) {
        reinterpret_cast<decltype(&vertices)>(real[18])(c,start,n,b,strides,offsets);
        try { if(auto p=deferred_state(c)) for(UINT i=0;i<n;++i) {
            auto value=buffer_binding(b ? b[i]:nullptr,start+i);
            value.json+=",\"stride\":"+std::to_string(strides ? strides[i]:0)+",\"offset\":"+std::to_string(offsets ? offsets[i]:0);
            p->shadow->vertices[start+i]=std::move(value);
        } } catch(...) { ++state().faults; }
    }
    static void STDMETHODCALLTYPE indices(ID3D11DeviceContext* c,ID3D11Buffer* b,DXGI_FORMAT format,UINT offset) {
        reinterpret_cast<decltype(&indices)>(real[19])(c,b,format,offset);
        try { if(auto p=deferred_state(c)) { p->shadow->indices=buffer_binding(b,0); p->shadow->indices.json+=",\"format\":"+std::to_string(format)+",\"offset\":"+std::to_string(offset); } } catch(...) { ++state().faults; }
    }
    static void STDMETHODCALLTYPE predicate(ID3D11DeviceContext* c,ID3D11Predicate* pred,BOOL value) {
        reinterpret_cast<decltype(&predicate)>(real[30])(c,pred,value);
        try { if(auto p=deferred_state(c)) p->shadow->predicate=identity(pred); } catch(...) { ++state().faults; }
    }
    static void STDMETHODCALLTYPE clear(ID3D11DeviceContext* c) {
        reinterpret_cast<decltype(&clear)>(real[110])(c); try { clear_shadow(c); } catch(...) { ++state().faults; }
    }
    // Track OM buffer UAV hazards too. No state is changed on the actual context.
    static void STDMETHODCALLTYPE om(ID3D11DeviceContext* c,UINT nr,ID3D11RenderTargetView* const* rt,ID3D11DepthStencilView* ds,UINT start,UINT n,ID3D11UnorderedAccessView* const* u,const UINT* counts) {
        reinterpret_cast<decltype(&om)>(real[34])(c,nr,rt,ds,start,n,u,counts);
        try { if(auto p=deferred_state(c)) if(n!=D3D11_KEEP_UNORDERED_ACCESS_VIEWS) {
            p->shadow->omUavs.clear();
            for(UINT i=0;i<n;++i) {
                auto value=view_binding(u ? u[i]:nullptr,start+i);
                if(value.resource) for(auto& stage:p->shadow->stages) for(auto& read:stage.srvs)
                    if(read.second.resource==value.resource) read.second={};
                p->shadow->omUavs[start+i]=std::move(value);
            }
        } } catch(...) { ++state().faults; }
    }
    static void STDMETHODCALLTYPE swap_state(ID3D11DeviceContext1* c,ID3DDeviceContextState* next,ID3DDeviceContextState** previous) {
        reinterpret_cast<decltype(&swap_state)>(real[131])(c,next,previous);
        try { clear_shadow(c,false); } catch(...) { ++state().faults; }
    }
    static HRESULT STDMETHODCALLTYPE finish(ID3D11DeviceContext* c,BOOL restore,ID3D11CommandList** list) {
        auto hr=reinterpret_cast<decltype(&finish)>(real[114])(c,restore,list);
        try {
            auto pending=deferred_state(c);
            if(SUCCEEDED(hr) && pending) {
                auto saved=ComPtr<Batch>(); saved.Attach(new Batch);
                saved->simulations.swap(pending->simulations);
                saved->draws.swap(pending->draws); saved->epoch=saved->draws.empty() ? state().epoch.load():pending->epoch;
                saved->unobserved=pending->unobserved; pending->unobserved=false;
                if(list && *list && FAILED((*list)->SetPrivateDataInterface(listKey,saved.Get()))) ++state().faults;
                if(!restore) clear_shadow(c);
            }
        } catch(...) { ++state().faults; }
        return hr;
    }
    static void STDMETHODCALLTYPE execute(ID3D11DeviceContext* c,ID3D11CommandList* list,BOOL restore) {
        // Observe submitted metadata once per execution, including reused lists. Never re-execute a draw.
        try {
            if(observes(c)) {
                auto data=batch(list,listKey,false); const auto listId=identity(list); auto& s=state();
                { std::lock_guard<std::mutex> guard(s.mutex); ++s.executes;
                  if(!data || data->unobserved) ++s.unknownLists;
                  else if(data->epoch!=s.epoch) ++s.staleLists;
                  else if(data->executions++) ++s.reusedLists; }
                if(data && data->epoch==s.epoch) for(const auto& source:data->draws) {
                    auto event=std::make_shared<Draw>(*source); event->list=listId;
                    if(c->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED) append(c,event);
                    else { std::lock_guard<std::mutex> guard(s.mutex); event->sequence=++s.sequence; s.draws.push_back(std::move(event)); }
                }
            }
        } catch(...) { ++state().faults; }
        const bool nested=executingList; executingList=true;
        reinterpret_cast<decltype(&execute)>(real[58])(c,list,restore);
        executingList=nested;
        try {
            if(sim::enabled()) { auto captured=batch(list,listKey,false);
                if(captured) for(auto& p:captured->simulations) sim::submit(p); }
        } catch(...) { ++sim::data().faults; }
        try { if(!restore) clear_shadow(c); } catch(...) { ++state().faults; }
    }
};
struct DeviceHooks {
    inline static void* realVS=nullptr; inline static void* realPS=nullptr; inline static void* realCS=nullptr;
    static HRESULT STDMETHODCALLTYPE vs(ID3D11Device* d,const void* p,SIZE_T n,ID3D11ClassLinkage* l,ID3D11VertexShader** out) {
        auto hr=reinterpret_cast<decltype(&vs)>(realVS)(d,p,n,l,out); if(SUCCEEDED(hr) && out) created(d,*out,p,n,"vs"); return hr;
    }
    static HRESULT STDMETHODCALLTYPE ps(ID3D11Device* d,const void* p,SIZE_T n,ID3D11ClassLinkage* l,ID3D11PixelShader** out) {
        auto hr=reinterpret_cast<decltype(&ps)>(realPS)(d,p,n,l,out); if(SUCCEEDED(hr) && out) created(d,*out,p,n,"ps"); return hr;
    }
    static HRESULT STDMETHODCALLTYPE cs(ID3D11Device* d,const void* p,SIZE_T n,ID3D11ClassLinkage* l,ID3D11ComputeShader** out) {
        auto hr=reinterpret_cast<decltype(&cs)>(realCS)(d,p,n,l,out); if(SUCCEEDED(hr) && out) created(d,*out,p,n,"cs"); return hr;
    }
};
inline void hook_failure(std::string message) {
    std::lock_guard<std::mutex> guard(state().mutex); state().hookFailures.push_back(std::move(message));
}
inline bool hook(void* target,void* detour,void** original,const char* name) {
    struct Installed { void* target; void* detour; void** original; std::string name; };
    static auto* installed=new std::vector<Installed>;
    for(const auto& i:*installed) {
        if(i.target==target) {
            if(i.name==name) return true; // same method shared between interface versions/contexts
            hook_failure(std::string(name)+": aliases a different method "+i.name); return false;
        }
        if(i.original==original) { hook_failure(std::string(name)+": changed implementation address"); return false; }
    }
    if(!state().host.load()->add_hook(target,detour,original)) { hook_failure(std::string(name)+": add_hook refused"); return false; }
    installed->push_back({target,detour,original,name}); return true;
}
template<int Lane> inline void hook_context(ID3D11DeviceContext* c) {
    using H=ContextHooks<Lane>; auto** t=*reinterpret_cast<void***>(c);
#define TGPT_VFX_HOOK(slot,method) hook(t[slot],reinterpret_cast<void*>(&H::method),&H::real[slot],#method)
    TGPT_VFX_HOOK(13,draw); TGPT_VFX_HOOK(12,indexed); TGPT_VFX_HOOK(21,instanced); TGPT_VFX_HOOK(20,indexed_instanced);
    TGPT_VFX_HOOK(40,indirect); TGPT_VFX_HOOK(39,indexed_indirect); TGPT_VFX_HOOK(38,automatic);
    TGPT_VFX_HOOK(41,dispatch); TGPT_VFX_HOOK(42,dispatch_indirect);
    TGPT_VFX_HOOK(114,finish); TGPT_VFX_HOOK(58,execute); TGPT_VFX_HOOK(110,clear);
    TGPT_VFX_HOOK(11,vs_shader); TGPT_VFX_HOOK(9,ps_shader); TGPT_VFX_HOOK(69,cs_shader);
    TGPT_VFX_HOOK(25,vs_srvs); TGPT_VFX_HOOK(8,ps_srvs); TGPT_VFX_HOOK(67,cs_srvs);
    TGPT_VFX_HOOK(7,vs_cb); TGPT_VFX_HOOK(16,ps_cb); TGPT_VFX_HOOK(71,cs_cb);
    TGPT_VFX_HOOK(68,uavs); TGPT_VFX_HOOK(18,vertices); TGPT_VFX_HOOK(19,indices);
    TGPT_VFX_HOOK(30,predicate); TGPT_VFX_HOOK(34,om);
    ComPtr<ID3D11DeviceContext1> c1; c->QueryInterface(IID_PPV_ARGS(&c1));
    if(c1) { t=*reinterpret_cast<void***>(c1.Get()); TGPT_VFX_HOOK(119,vs_cb1); TGPT_VFX_HOOK(123,ps_cb1); TGPT_VFX_HOOK(124,cs_cb1); TGPT_VFX_HOOK(131,swap_state); }
#undef TGPT_VFX_HOOK
}
struct ContextCreation {
    inline static void* originals[4]{};
    template<int N,class Device,class Context> static HRESULT STDMETHODCALLTYPE create(Device* d,UINT flags,Context** result) {
        using Fn=HRESULT(STDMETHODCALLTYPE*)(Device*,UINT,Context**);
        const auto hr=reinterpret_cast<Fn>(originals[N])(d,flags,result);
        if(SUCCEEDED(hr) && result && *result) try {
            if(d==state().device) {
                std::lock_guard<std::recursive_mutex> lock(state().installMutex);
                hook_context<1>(*result); clear_shadow(*result);
            }
        } catch(...) { ++state().faults; }
        return hr;
    }
};
inline void install(tgb::Host& host,bool simulation=false) noexcept {
    try {
        auto& s=state(); std::lock_guard<std::recursive_mutex> lock(s.installMutex);
        s.active=false; s.host=&host; s.device=host.game_device(); s.generation=host.game_device_generation();
        if(!s.device) throw std::runtime_error("No game D3D11 device");
        sim::enable(s.device,simulation);
        auto** t=*reinterpret_cast<void***>(s.device.load());
        hook(t[12],reinterpret_cast<void*>(&DeviceHooks::vs),&DeviceHooks::realVS,"CreateVertexShader");
        hook(t[15],reinterpret_cast<void*>(&DeviceHooks::ps),&DeviceHooks::realPS,"CreatePixelShader");
        hook(t[18],reinterpret_cast<void*>(&DeviceHooks::cs),&DeviceHooks::realCS,"CreateComputeShader");
        ComPtr<ID3D11DeviceContext> immediate; s.device.load()->GetImmediateContext(&immediate); hook_context<0>(immediate.Get());
        ComPtr<ID3D11DeviceContext> deferred;
        if(SUCCEEDED(s.device.load()->CreateDeferredContext(0,&deferred))) hook_context<1>(deferred.Get());
        else hook_failure("CreateDeferredContext failed");
        hook(t[27],reinterpret_cast<void*>(&ContextCreation::create<0,ID3D11Device,ID3D11DeviceContext>),&ContextCreation::originals[0],"CreateDeferredContext");
        ComPtr<ID3D11Device3> d3; s.device.load()->QueryInterface(IID_PPV_ARGS(&d3));
        if(d3) {
            auto** v=*reinterpret_cast<void***>(d3.Get());
            hook(v[44],reinterpret_cast<void*>(&ContextCreation::create<1,ID3D11Device1,ID3D11DeviceContext1>),&ContextCreation::originals[1],"CreateDeferredContext1");
            hook(v[51],reinterpret_cast<void*>(&ContextCreation::create<2,ID3D11Device2,ID3D11DeviceContext2>),&ContextCreation::originals[2],"CreateDeferredContext2");
            hook(v[62],reinterpret_cast<void*>(&ContextCreation::create<3,ID3D11Device3,ID3D11DeviceContext3>),&ContextCreation::originals[3],"CreateDeferredContext3");
        }
        s.installed=true;
        host.log("TGPT","VFX diagnostic observer installed; capture off until requested; hook failures are reported in every frame");
    } catch(...) { ++state().faults; }
}
inline bool requested(tgb::Host& host) {
    return std::filesystem::exists(std::filesystem::path(host.bepinex_root())/L"TGPathTracing"/L"Harness"/L"vfx-observer.enable");
}
inline bool simulation_requested(tgb::Host& host) {
    const auto marker=std::filesystem::path(host.bepinex_root())/L"TGPathTracing"/L"Harness"/L"vfx-observer.enable";
    std::ifstream in(marker); std::string mode; std::getline(in,mode);
    wchar_t value[16]{};
    const auto config=std::filesystem::path(host.bepinex_root())/L"config"/L"com.wessberg.tgpathtracing.cfg";
    GetPrivateProfileStringW(L"Advanced",L"Particle simulation diagnostic",L"false",value,16,config.c_str());
    return mode=="simulation-buffers" && _wcsicmp(value,L"true")==0;
}
inline int configure(const wchar_t* directory) noexcept {
    try {
        auto& s=state(); s.active=false; sim::data().sampling=false; std::lock_guard<std::mutex> guard(s.mutex);
        s.draws.clear(); s.calls=s.dispatches=s.sequence=s.executes=s.unknownLists=s.reusedLists=s.staleLists=0; s.previousFrame=-1; s.completedFrame=-1; s.boundaryDrops=0;
        ++s.epoch;
        if(!directory || !*directory) return 0;
        if(!s.installed) return -1;
        s.directory=std::filesystem::absolute(directory).make_preferred();
        // Shader evidence uses 64-character hashes; worker/harness paths can exceed MAX_PATH.
        auto path=s.directory.native();
        if(path.rfind(LR"(\\?\)",0)!=0) s.directory=path.rfind(LR"(\\)",0)==0 ?
            std::filesystem::path(LR"(\\?\UNC\)"+path.substr(2)):std::filesystem::path(LR"(\\?\)"+path);
        std::filesystem::create_directories(s.directory);
        { std::lock_guard<std::mutex> lock(sim::data().mutex); sim::data().current.clear(); sim::data().boundary=-1; }
        sim::data().sampling=true; s.active=true; return 0;
    } catch(...) { ++state().faults; return -2; }
}
inline void trace_marker() noexcept {
    // The marker appears in sequence order with submitted draws, including PT Off (zero markers).
    try { if(!state().active) return; auto d=std::make_shared<Draw>(); d->metadata="\"kind\":\"traceBegin\"";
        auto& s=state(); std::lock_guard<std::mutex> lock(s.mutex); d->sequence=++s.sequence; s.draws.push_back(std::move(d));
    } catch(...) { ++state().faults; }
}
inline void __stdcall frame(int frameId) noexcept {
    try {
        auto& s=state(); if(!s.active) return;
        std::lock_guard<std::mutex> guard(s.mutex);
        if(s.generation!=s.host.load()->game_device_generation()) { ++s.faults; }
        sim::write(s.host.load()->game_context(),s.directory,frameId);
        const auto file=s.directory/("frame-"+std::to_string(frameId)+".json");
        std::ofstream out(file); out.exceptions(std::ios::badbit|std::ios::failbit);
        out<<"{\"schema\":2,\"bindingScope\":\"VS/PS/CS buffers; deferred texture subresource hazards not decoded\",\"diagnosticOnly\":true,\"particleCaptureComplete\":false,\"frame\":"<<frameId
           <<",\"previousBoundary\":"<<s.previousFrame<<",\"deviceGeneration\":"<<s.generation<<",\"currentDeviceGeneration\":"<<s.host.load()->game_device_generation()
           <<",\"epoch\":"<<s.epoch<<",\"boundaryDrops\":"<<s.boundaryDrops.exchange(0)<<",\"faults\":"<<s.faults<<",\"executions\":"<<s.executes
           <<",\"unknownLists\":"<<s.unknownLists<<",\"staleLists\":"<<s.staleLists<<",\"reusedLists\":"<<s.reusedLists<<",\"hookFailures\":[";
        bool comma=false; for(const auto& f:s.hookFailures) { if(comma) out<<','; comma=true; out<<quote(f); }
        out<<"],\"programs\":["; comma=false;
        for(const auto& item:s.programs) { const auto& p=item.second; if(comma) out<<','; comma=true;
            out<<"{\"sha256\":"<<quote(p.hash)<<",\"stage\":"<<quote(p.stage)<<",\"bytes\":"<<p.bytes<<",\"created\":"<<p.made<<",\"variants\":"<<quote(p.variants)<<'}'; }
        out<<"],\"events\":["; comma=false;
        size_t dispatches=0,draws=0,candles=0;
        for(const auto& d:s.draws) {
            if(comma) out<<','; comma=true;
            out<<"{\"sequence\":"<<d->sequence<<",\"context\":"<<d->context<<",\"deferred\":"<<(d->deferred ? "true":"false")<<",\"list\":"<<d->list<<','<<d->metadata<<'}';
            if(d->metadata.find("\"kind\":\"dispatch")==0) ++dispatches;
            else if(d->metadata.find("\"kind\":\"traceBegin")==std::string::npos) ++draws;
            if(d->metadata.find("\"candle\":true")!=std::string::npos) ++candles;
        }
        out<<"],\"dispatchCalls\":"<<dispatches<<",\"drawCalls\":"<<draws<<",\"candleDrawCalls\":"<<candles<<"}\n"; out.close();
        const auto message="VFX diagnostic frame="+std::to_string(frameId)+" dispatches="+std::to_string(dispatches)+" draws="+std::to_string(draws)+" candlePasses="+std::to_string(candles)+" faults="+std::to_string(s.faults)+" (bindings only; GPU counts unknown for indirect calls)";
        s.host.load()->log("TGPT",message.c_str());
        s.draws.clear(); s.calls=s.dispatches=s.sequence=s.executes=s.unknownLists=s.reusedLists=s.staleLists=0; s.previousFrame=frameId;
        s.completedFrame=frameId;
    } catch(const std::exception& error) {
        state().active=false; ++state().faults;
        const auto message=std::string("VFX diagnostic frame failed: ")+error.what();
        if(auto host=state().host.load()) host->log("TGPT",message.c_str());
    } catch(...) { state().active=false; ++state().faults; }
}
} // namespace tgpt::vfx
