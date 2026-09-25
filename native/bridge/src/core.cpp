// SPDX-License-Identifier: MIT
#include "core.hpp"
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>

namespace tgb {
thread_local bool internalCall=false;
Core& core() { static auto* instance=new Core; return *instance; } // process lifetime

void log(const std::string& message) {
    static std::mutex logMutex;
    // Never throws: it is called from catch handlers, some of them in noexcept functions.
    try {
        std::lock_guard<std::mutex> guard(logMutex);
        const auto line="[TGGB] "+message+"\n";
        OutputDebugStringA(line.c_str());
        const auto& root=core().root;
        if(!root.empty()) {
            std::ofstream file(std::filesystem::path(root)/L"TGGraphicsBridge.Native.log", std::ios::app);
            file<<GetTickCount64()<<" "<<line;
        }
    } catch(...) {}
}
void log_throttled(const std::string& message) {
    static std::mutex throttleMutex; static std::string last; static uint64_t lastTime=0;
    try {
        std::lock_guard<std::mutex> guard(throttleMutex);
        const uint64_t now=GetTickCount64();
        if(message==last && now-lastTime<5000) return;
        last=message; lastTime=now;
    } catch(...) { return; }
    log(message);
}
std::wstring module_path(HMODULE module) {
    // Heap-grown: never place a 64 KB buffer on the stack of an arbitrary caller thread.
    std::vector<wchar_t> buffer(MAX_PATH);
    for(;;) {
        const DWORD length=GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if(!length) return {};
        if(length<buffer.size()) return std::wstring(buffer.data(), length);
        if(buffer.size()>=32768) return {};
        buffer.resize(buffer.size()*2);
    }
}
std::wstring system_directory() {
    const UINT needed=GetSystemDirectoryW(nullptr, 0);
    if(!needed) return {};
    std::wstring directory(needed, static_cast<wchar_t>(0));
    const UINT written=GetSystemDirectoryW(&directory[0], needed);
    if(!written || written>=needed) return {};
    directory.resize(written); return directory;
}
std::string narrow(const std::wstring& text) {
    if(text.empty()) return {};
    const int bytes=WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(bytes>0?size_t(bytes):0, '\0');
    if(bytes>0) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), &out[0], bytes, nullptr, nullptr);
    return out;
}
bool trusted_signer(const std::wstring& path, const wchar_t* expected) {
    WINTRUST_FILE_INFO file{}; file.cbStruct=sizeof(file); file.pcwszFilePath=path.c_str();
    WINTRUST_DATA data{}; data.cbStruct=sizeof(data); data.dwUIChoice=WTD_UI_NONE;
    data.fdwRevocationChecks=WTD_REVOKE_NONE; data.dwUnionChoice=WTD_CHOICE_FILE;
    data.pFile=&file; data.dwStateAction=WTD_STATEACTION_VERIFY;
    data.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID policy=WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG result=WinVerifyTrust(nullptr, &policy, &data);
    bool vendor=false;
    if(result==ERROR_SUCCESS) {
        auto* provider=WTHelperProvDataFromStateData(data.hWVTStateData);
        auto* signer=provider ? WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0) : nullptr;
        if(signer && signer->csCertChain && signer->pasCertChain[0].pCert) {
            wchar_t name[512]{};
            CertGetNameStringW(signer->pasCertChain[0].pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 512);
            std::wstring n(name); std::transform(n.begin(), n.end(), n.begin(), [](wchar_t c){ return std::towupper(c); });
            vendor=n.find(expected)!=std::wstring::npos;
        }
    }
    data.dwStateAction=WTD_STATEACTION_CLOSE; WinVerifyTrust(nullptr, &policy, &data);
    if(result!=ERROR_SUCCESS || !vendor)
        log("Signature validation rejected "+narrow(std::filesystem::path(path).filename().wstring())+" (expected signer "+
            narrow(expected)+"); WinVerifyTrust="+std::to_string(result));
    return result==ERROR_SUCCESS && vendor;
}
HMODULE load_signed(const std::wstring& path, const wchar_t* vendor) {
    if(!trusted_signer(path, vendor)) return nullptr;
    const HMODULE module=LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!module) log("Could not load "+narrow(std::filesystem::path(path).filename().wstring())+": error "+std::to_string(GetLastError()));
    return module;
}
} // namespace tgb
