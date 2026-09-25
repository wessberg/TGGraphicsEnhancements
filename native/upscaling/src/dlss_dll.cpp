// SPDX-License-Identifier: MIT
// The game's own DLSS Super Resolution runs from whichever nvngx_dlss.dll NVIDIA's runtime (_nvngx.dll,
// part of the driver) loads when the game initializes DLSS: the one next to the game, with one
// LoadLibraryExW call by full path. This mod bundles a newer, NVIDIA-signed copy in Native/DLSS. When
// the file asked for is older, the load returns the bundled copy instead; an equal or newer file (the
// driver's own download, a newer swap) is kept, and so is one that belongs to a DLSS mod's set of DLLs
// in the same folder (DLSS 5's, see part_of_dlss_set). Nothing on disk changes, and every other load
// passes through untouched.
#include "runtime.hpp"
#include <atomic>
#include <cwctype>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace tgup {
namespace {
std::wstring bundled;
uint64_t bundledVersion=0;
decltype(&LoadLibraryExW) realLoadLibraryExW=nullptr;
// GetFileVersionInfoW maps the file through LoadLibraryExW itself: those calls pass straight through.
thread_local bool reading=false;
std::mutex decisionMutex;
std::unordered_map<std::wstring, bool> decisions; // normalized lower-case path: load the bundled copy
// The last load decided, for the performance overlay.
std::atomic<uint32_t> lastSource{TGUP_DLSS_NONE};
std::atomic<uint64_t> lastRequested{0};

uint64_t file_version(const std::wstring& path) {
    const bool previous=reading; reading=true;
    uint64_t version=0;
    DWORD ignored=0;
    const DWORD size=GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if(size) {
        std::vector<uint8_t> data(size);
        VS_FIXEDFILEINFO* info=nullptr; UINT length=0;
        if(GetFileVersionInfoW(path.c_str(), 0, size, data.data()) &&
           VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) && info && length>=sizeof(*info))
            version=(uint64_t(info->dwFileVersionMS)<<32)|info->dwFileVersionLS;
    }
    reading=previous;
    return version;
}
std::wstring normalized(const std::wstring& path) {
    auto text=std::filesystem::path(path).lexically_normal().make_preferred().wstring();
    for(auto& c:text) c=wchar_t(towlower(c));
    return text;
}
bool replace(const wchar_t* requested) {
    const std::wstring path(requested);
    const auto key=normalized(path);
    std::lock_guard<std::mutex> lock(decisionMutex);
    if(const auto found=decisions.find(key); found!=decisions.end()) return found->second;
    bool use=false;
    if(key!=normalized(bundled)) {
        const uint64_t theirs=file_version(path);
        use=use_bundled_dlss(theirs, bundledVersion);
        // A DLSS mod's set in that folder keeps its own nvngx_dlss.dll.
        std::string set;
        if(use) {
            const auto folder=std::filesystem::path(path).parent_path();
            const auto sibling=[&](const wchar_t* name) {
                const auto file=folder/name;
                std::error_code error;
                return std::filesystem::exists(file, error) ? file_version(file.wstring()) : 0ull;
            };
            const uint64_t neural=sibling(L"nvngx_dlssnr.dll"), reconstruction=sibling(L"nvngx_dlssd.dll"), generation=sibling(L"nvngx_dlssg.dll");
            const bool neuralPresent=[&]{ std::error_code error; return std::filesystem::exists(folder/L"nvngx_dlssnr.dll", error); }();
            if(part_of_dlss_set(theirs, neuralPresent, {reconstruction, generation})) {
                use=false;
                set=neuralPresent ? "DLSS neural rendering "+(neural ? file_version_text(neural) : std::string("(no version)"))+" beside it"
                    : "DLSS "+std::string(same_dlss_release(theirs, generation) ? "frame generation "+file_version_text(generation)
                                                                                 : "ray reconstruction "+file_version_text(reconstruction))+" of the same release beside it";
            }
        }
        lastRequested=theirs;
        lastSource=use ? TGUP_DLSS_BUNDLED : TGUP_DLSS_KEPT;
        const auto shown=std::filesystem::path(path).lexically_normal().make_preferred().u8string();
        if(use) log("NVIDIA's runtime asked for DLSS "+file_version_text(theirs)+" ("+shown+"); it gets the bundled "+
                    file_version_text(bundledVersion)+" instead");
        else if(!set.empty()) log("NVIDIA's runtime keeps DLSS "+file_version_text(theirs)+" ("+shown+"): a DLSS mod's set of DLLs, with "+set+
                    ", works with its own nvngx_dlss.dll");
        else log("NVIDIA's runtime keeps DLSS "+(theirs ? file_version_text(theirs) : std::string("without a version"))+" ("+shown+
                 "): not older than the bundled "+file_version_text(bundledVersion));
    }
    decisions.emplace(key, use);
    return use;
}
HMODULE WINAPI load_library_hook(LPCWSTR path, HANDLE file, DWORD flags) {
    if(!reading && is_dlss_sr_path(path)) {
        try { if(replace(path)) return realLoadLibraryExW(bundled.c_str(), file, flags); }
        catch(...) {} // never fail a load over the decision: the file asked for loads as usual
    }
    return realLoadLibraryExW(path, file, flags);
}
} // namespace

uint32_t dlss_dll(uint64_t& loaded, uint64_t& requested, uint64_t& bundledOut) {
    const uint32_t source=lastSource;
    requested=lastRequested; bundledOut=bundledVersion;
    loaded=source==TGUP_DLSS_BUNDLED ? bundledVersion : source==TGUP_DLSS_KEPT ? requested : 0;
    return source;
}
bool install_dlss_dll(const std::wstring& folder) {
    const auto path=std::filesystem::path(folder)/L"DLSS"/L"nvngx_dlss.dll";
    if(!std::filesystem::exists(path)) { log("No bundled DLSS in Native/DLSS; the game's DLSS uses its own nvngx_dlss.dll"); return false; }
    if(!host().trusted_signer(path.c_str(), L"NVIDIA")) {
        log("Native/DLSS/nvngx_dlss.dll is not NVIDIA-signed; the game's DLSS uses its own (details in TGGraphicsBridge.Native.log)");
        return false;
    }
    bundledVersion=file_version(path.wstring());
    if(!bundledVersion) { log("Native/DLSS/nvngx_dlss.dll has no version; the game's DLSS uses its own"); return false; }
    bundled=path.wstring();
    const auto target=GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "LoadLibraryExW");
    if(!target || !host().add_hook(reinterpret_cast<void*>(target), reinterpret_cast<void*>(&load_library_hook),
                                   reinterpret_cast<void**>(&realLoadLibraryExW))) {
        bundled.clear();
        log("Could not hook LoadLibraryExW; the game's DLSS uses its own nvngx_dlss.dll");
        return false;
    }
    log("NVIDIA DLSS "+file_version_text(bundledVersion)+" bundled in Native/DLSS (NVIDIA-signed); the game's DLSS gets it "
        "when the nvngx_dlss.dll it loads is older");
    return true;
}
} // namespace tgup
