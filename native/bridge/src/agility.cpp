// SPDX-License-Identifier: MIT
#include "agility.hpp"
#include "diagnostics.hpp"
#include <windows.h>
#include <cstdint>
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

std::string align_agility_sdk() {
    const HMODULE exe=GetModuleHandleW(nullptr);
    auto* const requested=reinterpret_cast<uint32_t*>(GetProcAddress(exe, "D3D12SDKVersion"));
    auto* const folder=reinterpret_cast<const char* const*>(GetProcAddress(exe, "D3D12SDKPath"));
    if(!requested || !folder || !*folder) return outcome("none", 0, 0, 0, "Direct3D 12: the game names no Agility SDK, so Windows' own runtime serves every device");
    const uint32_t wanted=*requested;
    const std::string prefix="Direct3D 12: the game asks for Agility SDK "+std::to_string(wanted)+" from "+*folder;
    wchar_t system[MAX_PATH]{};
    const UINT length=GetSystemDirectoryW(system, MAX_PATH);
    uint32_t windows=0;
    if(!length || length>=MAX_PATH || !exported_sdk_version(std::filesystem::path(system)/L"D3D12Core.dll", windows))
        return outcome("system", wanted, 0, 0, prefix+"; this Windows has no Agility SDK support, so its own runtime is used");
    if(windows>=wanted) return outcome("system", wanted, windows, 0, prefix+"; Windows' own runtime is "+std::to_string(windows)+", so that one is used");
    const auto shipped=module_file(exe).parent_path()/std::filesystem::path(*folder)/L"D3D12Core.dll";
    uint32_t available=0;
    if(!exported_sdk_version(shipped, available))
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
