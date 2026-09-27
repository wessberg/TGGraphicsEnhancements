// SPDX-License-Identifier: MIT
#include "agility.hpp"
#include "core.hpp"
#include "diagnostics.hpp"
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace tgb {
namespace {
// The UINT a DLL exports as D3D12SDKVersion, read from the file: nothing is loaded or mapped, so
// d3d12.dll's own choice of D3D12Core.dll later is unaffected.
bool exported_sdk_version(const std::filesystem::path& file, uint32_t& version) {
    std::ifstream in(file, std::ios::binary|std::ios::ate);
    if(!in) return false;
    const auto end=in.tellg();
    if(end<=0) return false;
    std::vector<char> bytes(static_cast<size_t>(end));
    if(!in.seekg(0).read(bytes.data(), end)) return false;
    const auto fits=[&](size_t offset, size_t size) { return offset<=bytes.size() && size<=bytes.size()-offset; };
    if(!fits(0, sizeof(IMAGE_DOS_HEADER))) return false;
    IMAGE_DOS_HEADER dos; std::memcpy(&dos, bytes.data(), sizeof(dos));
    if(dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<0 || !fits(size_t(dos.e_lfanew), sizeof(IMAGE_NT_HEADERS64))) return false;
    IMAGE_NT_HEADERS64 nt; std::memcpy(&nt, bytes.data()+dos.e_lfanew, sizeof(nt));
    if(nt.Signature!=IMAGE_NT_SIGNATURE || nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC) return false;
    const size_t sections=size_t(dos.e_lfanew)+offsetof(IMAGE_NT_HEADERS64, OptionalHeader)+nt.FileHeader.SizeOfOptionalHeader;
    // A file offset for size bytes at rva, inside one section's raw data.
    const auto offset_of=[&](uint32_t rva, size_t size, size_t& offset) {
        for(WORD i=0;i<nt.FileHeader.NumberOfSections;++i) {
            const size_t at=sections+i*sizeof(IMAGE_SECTION_HEADER);
            if(!fits(at, sizeof(IMAGE_SECTION_HEADER))) return false;
            IMAGE_SECTION_HEADER s; std::memcpy(&s, bytes.data()+at, sizeof(s));
            if(rva<s.VirtualAddress || rva-s.VirtualAddress>=s.SizeOfRawData) continue;
            if(size>s.SizeOfRawData-(rva-s.VirtualAddress)) return false;
            offset=size_t(s.PointerToRawData)+(rva-s.VirtualAddress);
            return fits(offset, size);
        }
        return false;
    };
    const auto& directory=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    size_t at=0;
    if(!directory.VirtualAddress || !offset_of(directory.VirtualAddress, sizeof(IMAGE_EXPORT_DIRECTORY), at)) return false;
    IMAGE_EXPORT_DIRECTORY exports; std::memcpy(&exports, bytes.data()+at, sizeof(exports));
    size_t names=0, ordinals=0, functions=0;
    if(!offset_of(exports.AddressOfNames, size_t(exports.NumberOfNames)*4, names) ||
       !offset_of(exports.AddressOfNameOrdinals, size_t(exports.NumberOfNames)*2, ordinals) ||
       !offset_of(exports.AddressOfFunctions, size_t(exports.NumberOfFunctions)*4, functions)) return false;
    static const char wanted[]="D3D12SDKVersion";
    for(DWORD i=0;i<exports.NumberOfNames;++i) {
        uint32_t nameRva; std::memcpy(&nameRva, bytes.data()+names+i*4, 4);
        size_t name=0;
        if(!offset_of(nameRva, sizeof(wanted), name) || std::memcmp(bytes.data()+name, wanted, sizeof(wanted))) continue;
        uint16_t ordinal; std::memcpy(&ordinal, bytes.data()+ordinals+i*2, 2);
        if(ordinal>=exports.NumberOfFunctions) return false;
        uint32_t valueRva; std::memcpy(&valueRva, bytes.data()+functions+size_t(ordinal)*4, 4);
        size_t value=0;
        if(!offset_of(valueRva, 4, value)) return false;
        std::memcpy(&version, bytes.data()+value, 4);
        return true;
    }
    return false;
}
std::filesystem::path module_file(HMODULE module) {
    std::wstring path(MAX_PATH, L'\0');
    for(;;) {
        const DWORD n=GetModuleFileNameW(module, path.data(), DWORD(path.size()));
        if(n==0) return {};
        if(n<path.size()) { path.resize(n); return path; }
        path.resize(path.size()*2);
    }
}
} // namespace

namespace {
// The outcome, for the mods' explanations: which runtime serves D3D12 devices and whether it can.
std::string outcome(const char* state, uint32_t requested, uint32_t system, uint32_t shipped, std::string line) {
    set_fact("agility.state", state);
    if(requested) set_fact("agility.requested", std::to_string(requested));
    if(system) set_fact("agility.system", std::to_string(system));
    if(shipped) set_fact("agility.shipped", std::to_string(shipped));
    set_fact("agility", line);
    return line;
}
} // namespace

// The note of an extension runtime Direct3D 12 failed on (see note_agility_device), beside the bridge's log.
std::filesystem::path failure_note() { return std::filesystem::path(core().root)/L"agility-extension-failed.txt"; }
std::string redirectedKey; // this start's extension runtime and Windows' own, when the request names it

// The GPUs and their driver versions as DirectX records them (HKLM\SOFTWARE\Microsoft\DirectX\{adapter}), for the failure
// note: another GPU or driver tries the runtime again. From the registry, since loading dxgi.dll this early would stand in
// for a dxgi.dll proxy in the game folder (ReShade) when the game loads it by name.
std::string gpu_identity() {
    HKEY root=nullptr;
    if(RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\DirectX", 0, KEY_READ, &root)!=ERROR_SUCCESS) return "GPUs unknown";
    std::vector<std::string> gpus;
    for(DWORD i=0;;++i) {
        wchar_t name[128]; DWORD length=128;
        if(RegEnumKeyExW(root, i, name, &length, nullptr, nullptr, nullptr, nullptr)!=ERROR_SUCCESS) break;
        HKEY key=nullptr;
        if(RegOpenKeyExW(root, name, 0, KEY_READ, &key)!=ERROR_SUCCESS) continue;
        DWORD vendor=0, device=0, size=sizeof(DWORD); unsigned long long driver=0;
        bool ok=RegQueryValueExW(key, L"VendorId", nullptr, nullptr, reinterpret_cast<BYTE*>(&vendor), &size)==ERROR_SUCCESS;
        size=sizeof(DWORD); ok=ok && RegQueryValueExW(key, L"DeviceId", nullptr, nullptr, reinterpret_cast<BYTE*>(&device), &size)==ERROR_SUCCESS;
        size=sizeof(driver); RegQueryValueExW(key, L"DriverVersion", nullptr, nullptr, reinterpret_cast<BYTE*>(&driver), &size);
        RegCloseKey(key);
        if(!ok || vendor==0x1414) continue; // Microsoft's software adapters
        char text[64];
        std::snprintf(text, sizeof text, "%04lX:%04lX driver %llX", static_cast<unsigned long>(vendor), static_cast<unsigned long>(device), driver);
        gpus.emplace_back(text);
    }
    RegCloseKey(root);
    std::sort(gpus.begin(), gpus.end());
    gpus.erase(std::unique(gpus.begin(), gpus.end()), gpus.end());
    std::string all;
    for(const auto& gpu:gpus) all+=(all.empty() ? "" : ", ")+gpu;
    return all.empty() ? "GPUs unknown" : "GPU "+all;
}

// An extension's newer runtime (see agility.hpp): true when the request now names it. line: what was found,
// empty when no extension ships one that applies.
bool use_extension_runtime(const std::vector<std::wstring>& extensions, uint32_t* requested, const char** folder,
                           uint32_t wanted, uint32_t windows, uint32_t otherwise, const std::string& prefix, std::string& line) {
    const auto exeDir=module_file(GetModuleHandleW(nullptr)).parent_path();
    std::filesystem::path best; uint32_t newest=0;
    for(const auto& extension:extensions) {
        const auto core=std::filesystem::path(extension).parent_path()/L"D3D12"/L"D3D12Core.dll";
        std::error_code missing;
        uint32_t version=0;
        if(!std::filesystem::exists(core, missing) || !exported_sdk_version(core, version)) continue;
        if(version<600 || version>=700 || version<=newest) continue;
        newest=version; best=core.parent_path();
    }
    // Only newer than whatever would serve without it: Windows' own, or the game's file (not only its request).
    if(!newest || newest<=windows || newest<=wanted || newest<=otherwise) return false;
    const std::string offered=prefix+"; an extension ships Agility SDK "+std::to_string(newest)+" ("+narrow(best.wstring())+")";
    // What the note is about: this runtime, Windows' own, the Windows build and the GPUs with their drivers (a change of any
    // tries again).
    const std::string key=std::to_string(newest)+" on "+std::to_string(windows)+", Windows build "+fact("windows.build")+", "+gpu_identity();
    {
        std::ifstream note(failure_note());
        std::string previous;
        if(note && std::getline(note, previous) && previous==key) {
            line=outcome("extension-failed-before", wanted, windows, newest, offered+", but Direct3D 12 could not create a device with it on the last "
                "start, so it is not used (a newer runtime, a Windows update, another GPU or driver tries it again; so does deleting "+narrow(failure_note().wstring())+")");
            return false;
        }
    }
    std::error_code error;
    const auto relative=std::filesystem::relative(best, exeDir, error);
    bool inside=!error && !relative.empty();
    for(const auto& part:relative) if(part==L"..") inside=false;
    if(!inside) { line=outcome("extension-outside", wanted, windows, newest, offered+" outside the game folder, so it is not used"); return false; }
    if(!trusted_signer((best/L"D3D12Core.dll").wstring(), L"MICROSOFT")) { line=outcome("extension-unsigned", wanted, windows, newest, offered+" that is not Microsoft-signed, so it is not used"); return false; }
    if(GetModuleHandleW(L"d3d12.dll")) { line=outcome("too-late", wanted, windows, newest, offered+"; something loaded d3d12.dll before TG Graphics Bridge started, so it is not used"); return false; }
    // The exe's D3D12SDKPath export is a pointer to its string: it is pointed at this module's copy of the path.
    static std::string path;
    path=".\\"+narrow(relative.wstring())+"\\";
    DWORD protection=0, pathProtection=0;
    if(!VirtualProtect(requested, sizeof(*requested), PAGE_READWRITE, &protection)) { line=outcome("failed", wanted, windows, newest, offered+"; pointing the game's request at it failed (error "+std::to_string(GetLastError())+")"); return false; }
    if(!VirtualProtect(folder, sizeof(*folder), PAGE_READWRITE, &pathProtection)) {
        VirtualProtect(requested, sizeof(*requested), protection, &protection);
        line=outcome("failed", wanted, windows, newest, offered+"; pointing the game's request at it failed (error "+std::to_string(GetLastError())+")");
        return false;
    }
    *requested=newest; *folder=path.c_str();
    redirectedKey=key;
    VirtualProtect(folder, sizeof(*folder), pathProtection, &pathProtection);
    VirtualProtect(requested, sizeof(*requested), protection, &protection);
    line=outcome("extension", wanted, windows, newest, offered+", newer than Windows' "+std::to_string(windows)+": the game's request now names it, from "+path);
    return true;
}

void note_agility_device(bool created) {
    if(redirectedKey.empty()) return;
    std::error_code error;
    // A later attempt that succeeds takes a failed one's note back.
    if(created) { std::filesystem::remove(failure_note(), error); return; }
    std::ofstream note(failure_note(), std::ios::trunc);
    note<<redirectedKey<<"\n";
    log("Direct3D 12 could not create a device with the extension's Agility SDK runtime ("+redirectedKey+
        "); unless a later attempt succeeds, the next start uses Windows' or the game's runtime instead");
}

std::string align_agility_sdk(const std::vector<std::wstring>& extensions) {
    const HMODULE exe=GetModuleHandleW(nullptr);
    auto* const requested=reinterpret_cast<uint32_t*>(GetProcAddress(exe, "D3D12SDKVersion"));
    auto* const folder=reinterpret_cast<const char**>(GetProcAddress(exe, "D3D12SDKPath"));
    if(!requested || !folder || !*folder) return outcome("none", 0, 0, 0, "Direct3D 12: the game names no Agility SDK, so Windows' own runtime serves every device");
    const uint32_t wanted=*requested;
    const std::string prefix="Direct3D 12: the game asks for Agility SDK "+std::to_string(wanted)+" from "+*folder;
    wchar_t system[MAX_PATH]{};
    const UINT length=GetSystemDirectoryW(system, MAX_PATH);
    uint32_t windows=0;
    if(!length || length>=MAX_PATH || !exported_sdk_version(std::filesystem::path(system)/L"D3D12Core.dll", windows))
        return outcome("system", wanted, 0, 0, prefix+"; this Windows has no Agility SDK support, so its own runtime is used");
    // What serves without an extension's runtime: Windows' own when it is at least the request, otherwise the
    // game's own file, whichever version that is (a mismatch with the request is corrected below).
    const auto shipped=module_file(exe).parent_path()/std::filesystem::path(*folder)/L"D3D12Core.dll";
    uint32_t available=0;
    const bool readable=windows<wanted && exported_sdk_version(shipped, available);
    const uint32_t otherwise=windows>=wanted ? windows : (readable ? available : wanted);
    std::string redirected;
    if(use_extension_runtime(extensions, requested, folder, wanted, windows, otherwise, prefix, redirected)) return redirected;
    if(!redirected.empty()) log(redirected);
    if(windows>=wanted) return outcome("system", wanted, windows, 0, prefix+"; Windows' own runtime is "+std::to_string(windows)+", so that one is used");
    if(!readable)
        return outcome("unreadable", wanted, windows, 0, prefix+"; Windows' own runtime is "+std::to_string(windows)+" and the game's D3D12Core.dll could not be read, so D3D12 devices may fail");
    if(available==wanted) return outcome("shipped", wanted, windows, available, prefix+"; Windows' own runtime is "+std::to_string(windows)+", so the game's D3D12Core.dll is used");
    const std::string found=prefix+" but ships D3D12Core.dll "+std::to_string(available)+". Windows' own runtime is "+std::to_string(windows)+
        ", older than the request, so Windows loads the game's file and refuses every D3D12 device over the mismatch (0x887E0003)";
    // d3d12.dll reads the request when it loads: once another mod or tool has loaded it, the answer stands.
    if(GetModuleHandleW(L"d3d12.dll")) return outcome("too-late", wanted, windows, available, found+". Something loaded d3d12.dll before TG Graphics Bridge started, so it is too late to correct");
    DWORD protection=0;
    if(!VirtualProtect(requested, sizeof(*requested), PAGE_READWRITE, &protection))
        return outcome("failed", wanted, windows, available, found+". Correcting it failed (error "+std::to_string(GetLastError())+")");
    *requested=available;
    VirtualProtect(requested, sizeof(*requested), protection, &protection);
    return outcome("corrected", wanted, windows, available, found+". The game's request now names "+std::to_string(available)+", the version it ships");
}
} // namespace tgb
