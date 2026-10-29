// SPDX-License-Identifier: MIT
// Persistent bytecode / driver PSO blobs. Cache rejection always retries the exact same shader.
#pragma once
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace tgpt::pass_cache {
inline std::filesystem::path directory;
inline std::atomic<uint32_t> completed{0}, total{0}, hits{0};
inline std::string hash(const void* data, size_t size, std::string seed={}) {
    uint64_t a=1469598103934665603ull, b=7809847782465536322ull;
    auto add=[&](const unsigned char* p, size_t n) { for(size_t i=0;i<n;++i) { a=(a^p[i])*1099511628211ull; b=(b+p[i])*14029467366897019727ull; b^=b>>31; } };
    add(reinterpret_cast<const unsigned char*>(seed.data()), seed.size()); add(static_cast<const unsigned char*>(data), size);
    std::ostringstream s; s<<std::hex<<std::setfill('0')<<std::setw(16)<<a<<std::setw(16)<<b; return s.str();
}
inline void initialize(ID3D12Device* device) {
    try {
        wchar_t overridePath[32768]{};
        std::filesystem::path base;
        if(GetEnvironmentVariableW(L"TGPT_SHADER_CACHE", overridePath, DWORD(std::size(overridePath)))) base=overridePath;
        else {
            HMODULE module=nullptr; GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&initialize), &module);
            wchar_t path[32768]{}; GetModuleFileNameW(module, path, DWORD(std::size(path)));
            base=std::filesystem::path(path).parent_path()/L"ShaderCache";
        }
        ComPtr<IDXGIFactory4> factory; ComPtr<IDXGIAdapter1> adapter;
        check(host().create_native_factory(IID_PPV_ARGS(&factory)), "Shader cache adapter factory");
        check(factory->EnumAdapterByLuid(device->GetAdapterLuid(), IID_PPV_ARGS(&adapter)), "Shader cache adapter");
        DXGI_ADAPTER_DESC1 desc{}; check(adapter->GetDesc1(&desc), "Shader cache card"); LARGE_INTEGER driver{};
        check(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver), "Shader cache driver");
        std::string identity="TGPT-cache-v1/"+std::to_string(desc.VendorId)+"/"+std::to_string(desc.DeviceId)+"/"+
            std::to_string(desc.SubSysId)+"/"+std::to_string(desc.Revision)+"/"+std::to_string(driver.QuadPart);
        directory=base/hash(identity.data(), identity.size());
        std::filesystem::create_directories(directory);
        log("Shader cache: "+directory.string()+" (card and driver keyed; shader and root signature hashed per pass)");
    } catch(const std::exception& e) { directory.clear(); log(std::string("Shader cache unavailable: ")+e.what()); }
}
inline std::vector<char> read(const std::string& key) {
    if(directory.empty()) return {};
    std::ifstream file(directory/(key+".bin"), std::ios::binary|std::ios::ate);
    if(!file) return {};
    const auto size=file.tellg(); if(size<=0 || size>256ll*1024*1024) return {};
    std::vector<char> data(static_cast<size_t>(size)); file.seekg(0); file.read(data.data(), size);
    if(!file) return {}; return data;
}
inline void write(const std::string& key, const void* data, size_t size) {
    if(directory.empty()) return;
    // Atomic replacement also permits two game/test processes warming the same cache.
    const auto target=directory/(key+".bin"), temp=directory/(key+"."+std::to_string(GetCurrentProcessId())+"."+std::to_string(GetCurrentThreadId())+".tmp");
    try {
        { std::ofstream file(temp, std::ios::binary|std::ios::trunc); file.write(static_cast<const char*>(data), size); if(!file) return; }
        if(!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) std::filesystem::remove(temp);
    } catch(...) { /* Disk cache is optional; the identical compiled pass still runs. */ }
}
inline void timing(const char* name, const char* stage, std::chrono::steady_clock::time_point began, bool hit) {
    char line[384]; std::snprintf(line, sizeof(line), "Shader pass %s: %s %.3f s; disk cache %s", name, stage,
        std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count(), hit ? "hit" : "miss");
    if(hit) ++hits; log(line);
}
}
