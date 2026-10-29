// SPDX-License-Identifier: MIT
// CPU checks of community selection, file identity and the public parameter contract.
#define NOMINMAX
#include "tgnr_runtime_profile.hpp"
#include "tgnr_community_api.hpp"
#include "runtime_identity.hpp"
#include "community_parameters.hpp"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <stdexcept>
void expect(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
int wmain(int argc,wchar_t** argv) {
    try {
        using namespace tgnr;
        expect(runtime_kind(true)==RuntimeKind::Nvidia,"Signed runtime left SDK route");
        expect(runtime_kind(false)==RuntimeKind::Community,"Community runtime required an allowlist");
        expect(community_api_supported(0x13),"Supported direct API rejected");
        for(uint32_t api:{0u,0x12u,0x14u,0x15u,0xffffffffu})
            expect(!community_api_supported(api),"Unsupported direct API accepted");
        if(argc!=3) throw std::runtime_error("Pass scratch directory and community adapter DLL");
        auto folder=std::filesystem::absolute(argv[1]); std::filesystem::create_directories(folder);
        auto file=folder/L"identity-test.bin";
        { std::ofstream out(file,std::ios::binary); out<<"abc"; }
        {
            RuntimeFileLock lock(file.c_str());
            expect(lock.valid(),"File lock failed");
            expect(lock.sha256()=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 incorrect");
            expect(lock.sha256()=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","Repeated hash did not rewind");
            const auto identity=lock.sha256();
            expect(lock.matches(identity),"Selected file identity rejected");
            auto changed=identity; changed[0]='0';
            expect(!lock.matches(changed) && !lock.matches(""),"Changed file identity accepted");
            auto writer=CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
            const auto writerError=GetLastError(); if(writer!=INVALID_HANDLE_VALUE) CloseHandle(writer);
            expect(writer==INVALID_HANDLE_VALUE && writerError==ERROR_SHARING_VIOLATION,"Verified file remained writable");
            expect(!DeleteFileW(file.c_str()) && GetLastError()==ERROR_SHARING_VIOLATION,"Verified file could be replaced");
        }
        expect(DeleteFileW(file.c_str())!=0,"Identity handle was not released");
        expect(RuntimeFileLock(file.c_str()).sha256().empty(),"Missing file hashed as known");
        expect(!RuntimeFileLock(file.c_str()).matches(""),"Missing file matched missing identity");
        CommunityParameters parameters; NVSDK_NGX_Parameter* p=&parameters;
        p->Set("width",256u); unsigned w=0; expect(NVSDK_NGX_SUCCEED(p->Get("width",&w)) && w==256,"Unsigned ABI mismatch");
        int integer=0; expect(NVSDK_NGX_SUCCEED(p->Get("width",&integer)) && integer==256,"Numeric conversion failed");
        p->Set("strength",.5f); float strength=0; expect(NVSDK_NGX_SUCCEED(p->Get("strength",&strength)) && strength==.5f,"Float ABI mismatch");
        double wide=0; expect(NVSDK_NGX_SUCCEED(p->Get("strength",&wide)) && wide==.5,"Double conversion failed");
        auto resource=reinterpret_cast<ID3D12Resource*>(uintptr_t(0x1234)); p->Set("resource",resource);
        ID3D12Resource* returned=nullptr; expect(NVSDK_NGX_SUCCEED(p->Get("resource",&returned)) && returned==resource,"Resource ABI mismatch");
        void* opaque=nullptr; expect(NVSDK_NGX_SUCCEED(p->Get("resource",&opaque)) && opaque==resource,"Opaque resource conversion failed");
        w=91; expect(NVSDK_NGX_FAILED(p->Get("resource",&w)) && w==91,"Resource converted to dimension");
        expect(NVSDK_NGX_FAILED(p->Get("missing",&w)) && w==91,"Missing key changed destination");
        expect(NVSDK_NGX_FAILED(p->Get(nullptr,&w)),"Null key accepted");
        expect(NVSDK_NGX_FAILED(p->Get("width",static_cast<unsigned*>(nullptr))),"Null destination accepted");
        p->Reset(); expect(NVSDK_NGX_FAILED(p->Get("width",&w)),"Reset kept old values");
        auto dll=LoadLibraryExW(std::filesystem::absolute(argv[2]).c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        expect(dll!=nullptr,"Adapter load failed");
        auto get=reinterpret_cast<GetCommunityApi>(GetProcAddress(dll,"TGNR_GetCommunityApi"));
        expect(get!=nullptr,"Adapter export missing"); CommunityApi api;
        expect(get(nullptr)!=0,"Null API request accepted"); api.abi++;
        expect(get(&api)!=0,"Wrong API version accepted"); api={}; api.size--;
        expect(get(&api)!=0,"Wrong API size accepted"); api={};
        expect(get(&api)==0 && api.initialize && api.scratch && api.create && api.evaluate && api.release,"Incomplete API accepted");
        size_t scratch=0; expect(NVSDK_NGX_FAILED(api.scratch(static_cast<NVSDK_NGX_Feature>(18),p,&scratch)),"Uninitialized model dispatched");
        FreeLibrary(dll);
        std::puts("PASS: automatic runtime selection, direct API compatibility, file identity/locking and parameter/adapter ABI guards.");
        return 0;
    } catch(const std::exception& e) { std::printf("FAIL: %s\n",e.what()); return 1; }
}
