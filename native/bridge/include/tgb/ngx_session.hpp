// SPDX-License-Identifier: MIT
// Additive bridge exports: old extensions keep ABI 1; NGX consumers share a recursive lock.
#pragma once
#include "bridge_api.hpp"
#include <filesystem>
#include <string>
#include <vector>
namespace tgb {
class NgxScope {
    using Fn = void (__cdecl*)();
    Fn unlock_=nullptr;
public:
    NgxScope() {
        const auto module=GetModuleHandleW(L"TGGraphicsBridge.Native.dll");
        const auto lock=reinterpret_cast<Fn>(GetProcAddress(module,"TGGraphicsBridge_LockNgx"));
        unlock_=reinterpret_cast<Fn>(GetProcAddress(module,"TGGraphicsBridge_UnlockNgx"));
        if(lock && unlock_) lock(); else unlock_=nullptr;
    }
    ~NgxScope() { if(unlock_) unlock_(); }
    NgxScope(const NgxScope&)=delete;
    NgxScope& operator=(const NgxScope&)=delete;
};
// Pure discovery, usable before extensions bootstrap or Streamline starts NGX. An invalid preferred
// file stays selected for an explicit rejection; never silently replace it with another build.
inline std::filesystem::path neural_runtime_file(const wchar_t* root) {
    if(!root || !*root) return {};
    const std::filesystem::path bepinex(root);
    std::error_code ec;
    if(std::filesystem::exists(bepinex/L"TGNeuralRendering"/L"disable-autoload.txt",ec)) return {};
    if(!std::filesystem::exists(bepinex/L"TGNeuralRendering"/L"Native"/L"TGNeuralRendering.Native.dll",ec)) return {};
    const auto local=bepinex/L"TGNeuralRendering"/L"Native"/L"DLSS"/L"nvngx_dlssnr.dll";
    if(std::filesystem::exists(local,ec)) return local;
    const auto game=bepinex.parent_path()/L"nvngx_dlssnr.dll";
    return std::filesystem::exists(game,ec) ? game : std::filesystem::path{};
}
inline std::wstring neural_runtime_folder(Host& host) {
    const auto file=neural_runtime_file(host.bepinex_root());
    return !file.empty() && host.trusted_signer(file.c_str(),L"NVIDIA") ? file.parent_path().wstring() : std::wstring{};
}
}
