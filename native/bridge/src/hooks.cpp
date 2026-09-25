// SPDX-License-Identifier: MIT
// One MinHook registry for the process, and the DXGI/D3D11 swapchain-creation hooks. Game factory
// calls still take the original D3D11 path; for the game's window the bridge observes the device,
// creates the shared D3D12 device while ReShade's own swapchain call is on the stack, and offers the
// swapchain to the presentation provider, if an extension is one.
#include "core.hpp"
#include <MinHook.h>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <sstream>
#include <type_traits>
#include <unordered_map>

namespace tgb {
namespace {
struct Hook { void* original; void* detour; };
std::string hex(uint32_t value) { std::ostringstream s; s<<std::hex<<std::uppercase<<value; return s.str(); }
std::mutex hooksMutex;
std::unordered_map<void*, Hook> hooks;
decltype(&::CreateDXGIFactory) realFactory=nullptr;
decltype(&::CreateDXGIFactory1) realFactory1=nullptr;
decltype(&::CreateDXGIFactory2) realFactory2=nullptr;
decltype(&::D3D11CreateDeviceAndSwapChain) realCombined=nullptr;
using CreateSwap=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
using CreateHwnd=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
void describe(const DXGI_SWAP_CHAIN_DESC& in, DXGI_SWAP_CHAIN_DESC1& out, DXGI_SWAP_CHAIN_FULLSCREEN_DESC& full) {
    out={}; out.Width=in.BufferDesc.Width; out.Height=in.BufferDesc.Height; out.Format=in.BufferDesc.Format;
    out.SampleDesc=in.SampleDesc; out.BufferUsage=in.BufferUsage; out.BufferCount=in.BufferCount;
    out.SwapEffect=in.SwapEffect; out.Flags=in.Flags; out.Scaling=DXGI_SCALING_STRETCH; out.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
    full={}; full.RefreshRate=in.BufferDesc.RefreshRate; full.Scaling=in.BufferDesc.Scaling;
    full.ScanlineOrdering=in.BufferDesc.ScanlineOrdering; full.Windowed=in.Windowed;
}
// The swapchain the game asked for, so a refused bridge says why in the same line.
std::string requested(const DXGI_SWAP_CHAIN_DESC1& d, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* full) {
    return std::to_string(d.Width)+"x"+std::to_string(d.Height)+" format="+std::to_string(uint32_t(d.Format))+
        " buffers="+std::to_string(d.BufferCount)+" swapEffect="+std::to_string(uint32_t(d.SwapEffect))+
        " flags="+std::to_string(uint32_t(d.Flags))+
        (full ? (full->Windowed ? ", windowed" : ", exclusive fullscreen") : ", no fullscreen description");
}
HRESULT STDMETHODCALLTYPE swap_hook(IDXGIFactory* f, IUnknown* device, DXGI_SWAP_CHAIN_DESC* d, IDXGISwapChain** out) {
    auto original=reinterpret_cast<CreateSwap>(vtable_original(f, 10)); if(!original) return E_FAIL;
    if(!internalCall && d && out) {
        DXGI_SWAP_CHAIN_DESC1 desc{}; DXGI_SWAP_CHAIN_FULLSCREEN_DESC full{}; describe(*d, desc, full);
        ComPtr<IDXGISwapChain1> replacement;
        const auto hr=on_game_swapchain(device, f, d->OutputWindow, desc, &full, nullptr, &replacement);
        if(hr==S_OK) { *out=replacement.Detach(); return S_OK; }
        if(FAILED(hr)) log("The presentation provider could not replace the swapchain; the game keeps its own. The game asked for "+
            requested(desc, &full));
    }
    // Not all factory users belong to the game's primary rendering path.
    InternalGuard bypass; return original(f, device, d, out);
}
HRESULT STDMETHODCALLTYPE hwnd_hook(IDXGIFactory2* f, IUnknown* device, HWND hwnd, const DXGI_SWAP_CHAIN_DESC1* desc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* full, IDXGIOutput* restrictTo, IDXGISwapChain1** out) {
    auto original=reinterpret_cast<CreateHwnd>(vtable_original(f, 15)); if(!original) return E_FAIL;
    if(!internalCall && desc && out) {
        const auto hr=on_game_swapchain(device, f, hwnd, *desc, full, restrictTo, out);
        if(hr==S_OK) return S_OK;
        if(FAILED(hr)) log("The presentation provider could not replace the HWND swapchain; the game keeps its own. The game asked for "+
            requested(*desc, full));
    }
    InternalGuard bypass; return original(f, device, hwnd, desc, full, restrictTo, out);
}
void hook_factory(IUnknown* ptr) {
    if(internalCall || !ptr) return;
    ComPtr<IDXGIFactory> f; if(FAILED(ptr->QueryInterface(IID_PPV_ARGS(&f)))) return;
    bool good=add_vtable_hook(f.Get(), 10, reinterpret_cast<void*>(&swap_hook));
    ComPtr<IDXGIFactory2> f2; if(SUCCEEDED(f.As(&f2))) good=add_vtable_hook(f2.Get(), 15, reinterpret_cast<void*>(&hwnd_hook))&&good;
    if(!good) {
        // Only when free: a thread holding the core lock may be waiting on this caller's.
        std::unique_lock<std::recursive_mutex> lock(core().mutex, std::try_to_lock);
        if(lock.owns_lock()) core().status.lastError=HRESULT_FROM_WIN32(ERROR_HOOK_NOT_INSTALLED);
        log_throttled("Could not intercept D3D11 factory swapchain creation; an existing swapchain is never replaced late");
    }
}
HRESULT WINAPI factory_hook(REFIID iid, void** out) {
    const auto r=realFactory(iid, out); if(SUCCEEDED(r)&&out&&*out) hook_factory(static_cast<IUnknown*>(*out)); return r;
}
HRESULT WINAPI factory1_hook(REFIID iid, void** out) {
    const auto r=realFactory1(iid, out); if(SUCCEEDED(r)&&out&&*out) hook_factory(static_cast<IUnknown*>(*out)); return r;
}
HRESULT WINAPI factory2_hook(UINT flags, REFIID iid, void** out) {
    const auto r=realFactory2(flags, iid, out); if(SUCCEEDED(r)&&out&&*out) hook_factory(static_cast<IUnknown*>(*out)); return r;
}
HRESULT WINAPI combined_hook(IDXGIAdapter* adapter, D3D_DRIVER_TYPE driver, HMODULE software, UINT flags,
    const D3D_FEATURE_LEVEL* levels, UINT levelCount, UINT sdk, const DXGI_SWAP_CHAIN_DESC* desc,
    IDXGISwapChain** chain, ID3D11Device** device, D3D_FEATURE_LEVEL* selected, ID3D11DeviceContext** context) {
    if(internalCall || !desc || !chain || !is_game_window(desc->OutputWindow, desc->BufferDesc.Width, desc->BufferDesc.Height))
        return realCombined(adapter, driver, software, flags, levels, levelCount, sdk, desc, chain, device, selected, context);
    *chain=nullptr; if(device) *device=nullptr; if(context) *context=nullptr;
    // D3D11CreateDeviceAndSwapChain can bypass the public DXGI factory exports: create the game's
    // device without its chain, then the chain through the same path every other game uses.
    ComPtr<ID3D11Device> game; ComPtr<ID3D11DeviceContext> immediate;
    HRESULT result;
    { InternalGuard bypass; result=realCombined(adapter, driver, software, flags, levels, levelCount, sdk, nullptr, nullptr, &game, selected, &immediate); }
    if(FAILED(result)) return result;
    ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> ad; ComPtr<IDXGIFactory> factory;
    result=game.As(&dxgi); if(SUCCEEDED(result)) result=dxgi->GetAdapter(&ad);
    if(SUCCEEDED(result)) result=ad->GetParent(IID_PPV_ARGS(&factory)); if(FAILED(result)) return result;
    DXGI_SWAP_CHAIN_DESC1 d{}; DXGI_SWAP_CHAIN_FULLSCREEN_DESC full{}; describe(*desc, d, full);
    ComPtr<IDXGISwapChain1> replacement;
    result=on_game_swapchain(game.Get(), factory.Get(), desc->OutputWindow, d, &full, nullptr, &replacement);
    if(result==S_OK) *chain=replacement.Detach();
    else {
        if(FAILED(result)) log("The presentation provider could not replace the combined-creation swapchain; the game keeps its own. The game asked for "+
            requested(d, &full));
        auto originalDesc=*desc; InternalGuard bypass; result=factory->CreateSwapChain(game.Get(), &originalDesc, chain);
    }
    if(FAILED(result)) return result;
    if(device) *device=game.Detach(); if(context) *context=immediate.Detach(); return result;
}
static_assert(std::is_same_v<decltype(&combined_hook), decltype(&::D3D11CreateDeviceAndSwapChain)>);
} // namespace

HRESULT create_native_factory(REFIID iid, void** out) {
    // The trampoline reaches the system CreateDXGIFactory2 without the export hook, so the factory is
    // neither re-hooked nor a Streamline proxy. Its swapchain calls still pass the vtable hooks, which
    // step aside while the caller's thread is marked internal.
    if(!realFactory2) return E_FAIL;
    InternalGuard bypass; return realFactory2(0, iid, out);
}
bool initialize_hooks() {
    const auto r=MH_Initialize();
    return r==MH_OK || r==MH_ERROR_ALREADY_INITIALIZED;
}
bool add_hook(void* target, void* detour, void** original) {
    if(!target || !detour) return false;
    std::lock_guard<std::mutex> guard(hooksMutex);
    auto existing=hooks.find(target);
    if(existing!=hooks.end()) {
        if(existing->second.detour!=detour) return false;
        if(original) *original=existing->second.original;
        return true;
    }
    void* trampoline=nullptr;
    auto r=MH_CreateHook(target, detour, &trampoline);
    if(r!=MH_OK) { log("MH_CreateHook failed: "+std::string(MH_StatusToString(r))); return false; }
    // Publish the trampoline before enabling the detour.
    hooks.emplace(target, Hook{trampoline, detour});
    if(original) *original=trampoline;
    r=MH_EnableHook(target);
    if(r!=MH_OK) { hooks.erase(target); MH_RemoveHook(target); log("MH_EnableHook failed"); return false; }
    return true;
}
bool add_vtable_hook(void* object, size_t slot, void* detour) {
    return object && add_hook((*reinterpret_cast<void***>(object))[slot], detour, nullptr);
}
void* vtable_original(void* object, size_t slot) {
    auto address=(*reinterpret_cast<void***>(object))[slot];
    std::lock_guard<std::mutex> guard(hooksMutex);
    auto it=hooks.find(address); return it==hooks.end() ? nullptr : it->second.original;
}
// The swapchain methods every factory dxgi.dll creates shares, hooked before anything else hooks DXGI.
//
// Hooks on one function run last-installed first. ReShade creates the bridge's D3D12 device unwrapped
// only while its own swapchain hook is on the stack, so the bridge's hook has to run inside ReShade's:
// installed before it. ReShade hooks these methods on the first factory created through its hooks, and
// hooks dxgi.dll's exports the moment a LoadLibrary call loads it. So dxgi.dll is loaded here without
// LoadLibrary, and the bridge's factory comes from the untouched export; the game's factories, created
// later, then reach the bridge from inside ReShade's hook (or directly, without ReShade). Waiting for the
// game's first factory instead misses it: ReShade forwards it past hooks installed after its own.
namespace {
// The first bytes of dxgi.dll's factory exports as the file on disk has them, put back for one call and
// then restored: a hook already on an export (ReShade hooks dxgi.dll the moment it loads, whichever way
// it is loaded) would otherwise see the bridge's factory and hook the swapchain methods before the
// bridge. Only a jump written over the start of an export is undone, and only where no relocation falls.
class ExportsAsOnDisk {
    struct Patch { uint8_t* at; size_t length; uint8_t saved[16]; DWORD protection; };
    std::vector<Patch> patches;
public:
    ExportsAsOnDisk(HMODULE module, const std::wstring& path, std::initializer_list<const char*> names) {
        const HANDLE file=CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if(file==INVALID_HANDLE_VALUE) return;
        const HANDLE mapping=CreateFileMappingW(file, nullptr, PAGE_READONLY|SEC_IMAGE, 0, 0, nullptr);
        CloseHandle(file);
        if(!mapping) return;
        auto* view=static_cast<const uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0)); // sections at their RVAs, not relocated
        CloseHandle(mapping);
        if(!view) return;
        auto* base=reinterpret_cast<uint8_t*>(module);
        const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS*>(view+reinterpret_cast<const IMAGE_DOS_HEADER*>(view)->e_lfanew);
        const auto& relocations=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        const auto relocated=[&](size_t from, size_t to) {
            for(size_t offset=0; offset+sizeof(IMAGE_BASE_RELOCATION)<=relocations.Size;) {
                const auto* block=reinterpret_cast<const IMAGE_BASE_RELOCATION*>(view+relocations.VirtualAddress+offset);
                if(block->SizeOfBlock<sizeof(IMAGE_BASE_RELOCATION)) break;
                const auto* entries=reinterpret_cast<const WORD*>(block+1);
                for(size_t i=0;i<(block->SizeOfBlock-sizeof(IMAGE_BASE_RELOCATION))/sizeof(WORD);++i) {
                    const size_t at=block->VirtualAddress+(entries[i]&0xFFF);
                    if((entries[i]>>12)!=IMAGE_REL_BASED_ABSOLUTE && at+8>from && at<to) return true;
                }
                offset+=block->SizeOfBlock;
            }
            return false;
        };
        for(const char* name:names) {
            auto* target=reinterpret_cast<uint8_t*>(GetProcAddress(module, name));
            if(!target) continue;
            if(target<base || size_t(target-base)+16>nt->OptionalHeader.SizeOfImage) continue; // not dxgi.dll's own code
            const size_t rva=size_t(target-base);
            const uint8_t* original=view+rva;
            size_t length=0;
            for(size_t i=0;i<16;++i) if(target[i]!=original[i]) length=i+1;
            if(!length) continue;
            const uint8_t first=target[0];
            if(!(first==0xE9 || first==0xEB || first==0xFF || first==0x68 || first==0x48 || first==0x49) || relocated(rva, rva+length)) continue;
            Patch p{target, length, {}, 0};
            std::memcpy(p.saved, target, length);
            if(!VirtualProtect(target, length, PAGE_EXECUTE_READWRITE, &p.protection)) continue;
            std::memcpy(target, original, length);
            FlushInstructionCache(GetCurrentProcess(), target, length);
            patches.push_back(p);
        }
        UnmapViewOfFile(view);
    }
    // Last first: exports on one page each recorded the protection the one before left.
    ~ExportsAsOnDisk() {
        for(auto it=patches.rbegin(); it!=patches.rend(); ++it) {
            auto& p=*it;
            std::memcpy(p.at, p.saved, p.length);
            FlushInstructionCache(GetCurrentProcess(), p.at, p.length);
            DWORD ignored=0; VirtualProtect(p.at, p.length, p.protection, &ignored);
        }
    }
    size_t count() const { return patches.size(); }
    ExportsAsOnDisk(const ExportsAsOnDisk&)=delete; ExportsAsOnDisk& operator=(const ExportsAsOnDisk&)=delete;
};
} // namespace
bool prime_swapchain_hooks() {
    if(!initialize_hooks()) return false;
    const auto directory=system_directory(); if(directory.empty()) return false;
    const auto path=(std::filesystem::path(directory)/L"dxgi.dll").wstring();
    const HMODULE dxgi=LoadLibraryW(path.c_str());
    const auto create=dxgi ? reinterpret_cast<decltype(&::CreateDXGIFactory1)>(GetProcAddress(dxgi, "CreateDXGIFactory1")) : nullptr;
    if(!create) { log("dxgi.dll or its CreateDXGIFactory1 is missing; swapchain creation is not intercepted"); return false; }
    ComPtr<IDXGIFactory1> own;
    HRESULT hr;
    size_t bypassed=0;
    {
        ExportsAsOnDisk unhooked(dxgi, path, {"CreateDXGIFactory", "CreateDXGIFactory1", "CreateDXGIFactory2"});
        bypassed=unhooked.count();
        hr=create(IID_PPV_ARGS(&own));
    }
    if(FAILED(hr) || !own) { log("Could not create a DXGI factory to intercept swapchain creation (0x"+hex(uint32_t(hr))+")"); return false; }
    hook_factory(own.Get());
    if(bypassed) log("Created the bridge's DXGI factory past "+std::to_string(bypassed)+" hook(s) already on dxgi.dll's exports, so the bridge's swapchain hooks go in before theirs");
    return vtable_original(own.Get(), 10)!=nullptr;
}
bool install_graphics_hooks() {
    if(!initialize_hooks()) return false;
    const auto directory=system_directory(); if(directory.empty()) return false;
    const auto dxgi=LoadLibraryW((std::filesystem::path(directory)/L"dxgi.dll").c_str());
    const auto d11=LoadLibraryW((std::filesystem::path(directory)/L"d3d11.dll").c_str());
    if(!dxgi || !d11) return false;
#define HOOK(module,name,detour,original) if(!add_hook(reinterpret_cast<void*>(GetProcAddress(module,name)),reinterpret_cast<void*>(&detour),reinterpret_cast<void**>(&original))) return false
    HOOK(dxgi, "CreateDXGIFactory", factory_hook, realFactory);
    HOOK(dxgi, "CreateDXGIFactory1", factory1_hook, realFactory1);
    HOOK(dxgi, "CreateDXGIFactory2", factory2_hook, realFactory2);
    HOOK(d11, "D3D11CreateDeviceAndSwapChain", combined_hook, realCombined);
#undef HOOK
    log("D3D11 swapchain-creation hooks installed. Game factories remain native.");
    return true;
}
} // namespace tgb
