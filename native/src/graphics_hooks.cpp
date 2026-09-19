// SPDX-License-Identifier: MIT
#include "dx11_bridge.hpp"
#include <MinHook.h>
#include <unordered_map>
#include <filesystem>
#include <type_traits>
namespace tgfg {
namespace {
struct Hook { void* original; void* detour; };
std::mutex hooksMutex;
std::unordered_map<void*,Hook> hooks;
decltype(&::CreateDXGIFactory) realFactory=nullptr;
decltype(&::CreateDXGIFactory1) realFactory1=nullptr;
decltype(&::CreateDXGIFactory2) realFactory2=nullptr;
decltype(&::D3D11CreateDeviceAndSwapChain) realCombined=nullptr;
using CreateSwap=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*,IUnknown*,DXGI_SWAP_CHAIN_DESC*,IDXGISwapChain**);
using CreateHwnd=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*,IUnknown*,HWND,const DXGI_SWAP_CHAIN_DESC1*,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*,IDXGIOutput*,IDXGISwapChain1**);
void describe(const DXGI_SWAP_CHAIN_DESC& in,DXGI_SWAP_CHAIN_DESC1& out,DXGI_SWAP_CHAIN_FULLSCREEN_DESC& full) {
    out={};out.Width=in.BufferDesc.Width;out.Height=in.BufferDesc.Height;out.Format=in.BufferDesc.Format;
    out.SampleDesc=in.SampleDesc;out.BufferUsage=in.BufferUsage;out.BufferCount=in.BufferCount;
    out.SwapEffect=in.SwapEffect;out.Flags=in.Flags;out.Scaling=DXGI_SCALING_STRETCH;out.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
    full={};full.RefreshRate=in.BufferDesc.RefreshRate;full.Scaling=in.BufferDesc.Scaling;
    full.ScanlineOrdering=in.BufferDesc.ScanlineOrdering;full.Windowed=in.Windowed;
}
HRESULT STDMETHODCALLTYPE swap_hook(IDXGIFactory* f,IUnknown* device,DXGI_SWAP_CHAIN_DESC* d,IDXGISwapChain** out) {
    auto original=reinterpret_cast<CreateSwap>(vtable_original(f,10));if(!original)return E_FAIL;
    if(!insideSL && d && out) {
        DXGI_SWAP_CHAIN_DESC1 desc{};DXGI_SWAP_CHAIN_FULLSCREEN_DESC full{};describe(*d,desc,full);
        ComPtr<IDXGISwapChain1> replacement;
        const auto hr=try_create_dx11_bridge(device,f,d->OutputWindow,desc,&full,nullptr,&replacement);
        if(hr==S_OK){*out=replacement.Detach();return S_OK;}
        if(FAILED(hr))log("D3D11 bridge creation failed; retaining the original game swapchain path");
    }
    // Not all factory users belong to the game's primary rendering path.
    SLGuard bypass;return original(f,device,d,out);
}
HRESULT STDMETHODCALLTYPE hwnd_hook(IDXGIFactory2* f,IUnknown* device,HWND hwnd,const DXGI_SWAP_CHAIN_DESC1* desc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* full,IDXGIOutput* restrictTo,IDXGISwapChain1** out) {
    auto original=reinterpret_cast<CreateHwnd>(vtable_original(f,15));if(!original)return E_FAIL;
    if(!insideSL && desc && out) {
        const auto hr=try_create_dx11_bridge(device,f,hwnd,*desc,full,restrictTo,out);
        if(hr==S_OK)return S_OK;
        if(FAILED(hr))log("D3D11 HWND bridge creation failed; retaining original creation path");
    }
    SLGuard bypass;return original(f,device,hwnd,desc,full,restrictTo,out);
}
void hook_factory(IUnknown* ptr) {
    if(insideSL || !ptr)return;
    ComPtr<IDXGIFactory> f;if(FAILED(ptr->QueryInterface(IID_PPV_ARGS(&f))))return;
    bool good=add_vtable_hook(f.Get(),10,reinterpret_cast<void*>(&swap_hook));
    ComPtr<IDXGIFactory2> f2;if(SUCCEEDED(f.As(&f2)))good=add_vtable_hook(f2.Get(),15,reinterpret_cast<void*>(&hwnd_hook))&&good;
    if(!good)failure(Reason::NativeError,"Could not intercept D3D11 factory creation; existing chain is not replaced late");
}
HRESULT WINAPI factory_hook(REFIID iid,void** out) {
    const auto r=realFactory(iid,out);if(SUCCEEDED(r)&&out&&*out)hook_factory(static_cast<IUnknown*>(*out));return r;
}
HRESULT WINAPI factory1_hook(REFIID iid,void** out) {
    const auto r=realFactory1(iid,out);if(SUCCEEDED(r)&&out&&*out)hook_factory(static_cast<IUnknown*>(*out));return r;
}
HRESULT WINAPI factory2_hook(UINT flags,REFIID iid,void** out) {
    const auto r=realFactory2(flags,iid,out);if(SUCCEEDED(r)&&out&&*out)hook_factory(static_cast<IUnknown*>(*out));return r;
}
HRESULT WINAPI combined_hook(IDXGIAdapter* adapter,D3D_DRIVER_TYPE driver,HMODULE software,UINT flags,
    const D3D_FEATURE_LEVEL* levels,UINT levelCount,UINT sdk,const DXGI_SWAP_CHAIN_DESC* desc,
    IDXGISwapChain** chain,ID3D11Device** device,D3D_FEATURE_LEVEL* selected,ID3D11DeviceContext** context) {
    if(insideSL || !desc || !chain || !is_game_window(desc->OutputWindow,desc->BufferDesc.Width,desc->BufferDesc.Height))
        return realCombined(adapter,driver,software,flags,levels,levelCount,sdk,desc,chain,device,selected,context);
    *chain=nullptr;if(device)*device=nullptr;if(context)*context=nullptr;
    // D3D11CreateDeviceAndSwapChain can bypass public DXGI factory exports.
    // Create the original D3D11 device without its chain, then select our chain.
    ComPtr<ID3D11Device> game;ComPtr<ID3D11DeviceContext> immediate;
    HRESULT result;
    {SLGuard bypass;result=realCombined(adapter,driver,software,flags,levels,levelCount,sdk,nullptr,nullptr,&game,selected,&immediate);}
    if(FAILED(result))return result;
    ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> ad;ComPtr<IDXGIFactory> factory;
    result=game.As(&dxgi);if(SUCCEEDED(result))result=dxgi->GetAdapter(&ad);
    if(SUCCEEDED(result))result=ad->GetParent(IID_PPV_ARGS(&factory));if(FAILED(result))return result;
    DXGI_SWAP_CHAIN_DESC1 d{};DXGI_SWAP_CHAIN_FULLSCREEN_DESC full{};describe(*desc,d,full);
    ComPtr<IDXGISwapChain1> replacement;
    result=try_create_dx11_bridge(game.Get(),factory.Get(),desc->OutputWindow,d,&full,nullptr,&replacement);
    if(result==S_OK)*chain=replacement.Detach();
    else {auto originalDesc=*desc;SLGuard bypass;result=factory->CreateSwapChain(game.Get(),&originalDesc,chain);}
    if(FAILED(result))return result;
    if(device)*device=game.Detach();if(context)*context=immediate.Detach();return result;
}
static_assert(std::is_same_v<decltype(&combined_hook),decltype(&::D3D11CreateDeviceAndSwapChain)>);
}
HRESULT create_native_factory(REFIID iid,void** out) {
    // The trampoline reaches the system CreateDXGIFactory2 without our export hook, so the factory
    // is neither re-hooked nor a Streamline proxy. Its swapchain calls still pass our vtable hooks,
    // which step aside while insideSL is set.
    if(!realFactory2) return E_FAIL;
    SLGuard bypass; return realFactory2(0,iid,out);
}
bool add_hook(void* target,void* detour,void** original) {
    if(!target || !detour) return false;
    std::lock_guard<std::mutex> guard(hooksMutex);
    auto existing=hooks.find(target);
    if(existing!=hooks.end()) {
        if(existing->second.detour!=detour) return false;
        if(original) *original=existing->second.original;
        return true;
    }
    void* trampoline=nullptr;
    auto r=MH_CreateHook(target,detour,&trampoline);
    if(r!=MH_OK) { log("MH_CreateHook failed: "+std::string(MH_StatusToString(r))); return false; }
    // Publish the trampoline before enabling the detour.
    hooks.emplace(target,Hook{trampoline,detour});
    if(original) *original=trampoline;
    r=MH_EnableHook(target);
    if(r!=MH_OK) { hooks.erase(target); MH_RemoveHook(target); log("MH_EnableHook failed"); return false; }
    return true;
}
bool add_vtable_hook(void* object,size_t slot,void* detour) {
    return object && add_hook((*reinterpret_cast<void***>(object))[slot],detour,nullptr);
}
void* vtable_original(void* object,size_t slot) {
    auto address=(*reinterpret_cast<void***>(object))[slot];
    std::lock_guard<std::mutex> guard(hooksMutex);
    auto it=hooks.find(address); return it==hooks.end()?nullptr:it->second.original;
}

bool install_graphics_hooks() {
    auto r=MH_Initialize();if(r!=MH_OK&&r!=MH_ERROR_ALREADY_INITIALIZED)return false;
    const auto directory=system_directory();if(directory.empty())return false;
    const auto dxgi=LoadLibraryW((std::filesystem::path(directory)/L"dxgi.dll").c_str());
    const auto d11=LoadLibraryW((std::filesystem::path(directory)/L"d3d11.dll").c_str());
    if(!dxgi||!d11)return false;
#define HOOK(module,name,detour,original) if(!add_hook(reinterpret_cast<void*>(GetProcAddress(module,name)),reinterpret_cast<void*>(&detour),reinterpret_cast<void**>(&original)))return false
    HOOK(dxgi,"CreateDXGIFactory",factory_hook,realFactory);
    HOOK(dxgi,"CreateDXGIFactory1",factory1_hook,realFactory1);
    HOOK(dxgi,"CreateDXGIFactory2",factory2_hook,realFactory2);
    HOOK(d11,"D3D11CreateDeviceAndSwapChain",combined_hook,realCombined);
#undef HOOK
    log("D3D11 swapchain-creation hooks installed. Game factories remain native; only the private D3D12 output is replaced.");
    return true;
}
} // namespace tgfg
