// SPDX-License-Identifier: MIT
// Runs the real community helper against our unsigned model fixture. No GPU or vendor model.
#define NOMINMAX
#include "tgnr_community_api.hpp"
#include "runtime_identity.hpp"
#include <filesystem>
#include <cstdio>
#include <stdexcept>
void expect(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
int wmain(int argc,wchar_t** argv) {
    try {
        expect(argc==4,"Pass helper, mock runtime and mode");
        const auto helper=std::filesystem::absolute(argv[1]), file=std::filesystem::absolute(argv[2]);
        const std::wstring mode=argv[3];
        SetEnvironmentVariableW(L"TGNR_ADAPTER_TEST_MODE",mode.c_str());
        auto dll=LoadLibraryExW(helper.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        expect(dll!=nullptr,"Helper did not load");
        auto get=reinterpret_cast<tgnr::GetCommunityApi>(GetProcAddress(dll,"TGNR_GetCommunityApi"));
        tgnr::CommunityApi api; expect(get && get(&api)==0,"Helper ABI rejected");
        auto identity=tgnr::RuntimeFileLock(file.c_str()).sha256(); expect(identity.size()==64,"Fixture hash failed");
        if(mode==L"changed") identity[0]=identity[0]=='0' ? '1':'0';
        // Opaque tokens passed only to our fixture, which never calls a graphics API.
        auto device=reinterpret_cast<ID3D12Device*>(uintptr_t(1));
        auto adapter=reinterpret_cast<IDXGIAdapter*>(uintptr_t(2));
        auto list=reinterpret_cast<ID3D12GraphicsCommandList*>(uintptr_t(3));
        NVSDK_NGX_Parameter* p=nullptr; tgnr::CommunityInfo info;
        auto result=api.initialize(file.c_str(),identity.c_str(),file.parent_path().c_str(),device,adapter,&p,&info);
        auto mock=GetModuleHandleW(file.filename().c_str());
        if(mode==L"changed") {
            expect(NVSDK_NGX_FAILED(result) && !p && !mock,"Replaced file executed");
        } else {
            expect(mock!=nullptr,"Unlisted fixture was not loaded");
            auto calls=reinterpret_cast<unsigned(__cdecl*)()>(GetProcAddress(mock,"TGNR_TestInitCalls"));
            expect(calls!=nullptr,"Fixture instrumentation missing");
            if(mode==L"api" || mode==L"unsupported") {
                expect(NVSDK_NGX_FAILED(result) && !p && calls()==0,"Incompatible runtime was initialized");
            } else {
                expect(mode==L"success" && NVSDK_NGX_SUCCEED(result) && p && calls()==1,"Compatible unlisted runtime rejected");
                unsigned value=0; expect(NVSDK_NGX_SUCCEED(p->Get("Fixture",&value)) && value==123,"Parameter population failed");
                size_t bytes=99; auto feature=static_cast<NVSDK_NGX_Feature>(18);
                expect(NVSDK_NGX_SUCCEED(api.scratch(feature,p,&bytes)) && bytes==0,"Scratch dispatch failed");
                NVSDK_NGX_Handle* handle=nullptr;
                expect(NVSDK_NGX_SUCCEED(api.create(list,feature,p,&handle)) && handle,"Create dispatch failed");
                expect(NVSDK_NGX_SUCCEED(api.evaluate(list,handle,p,nullptr)),"Evaluate dispatch failed");
                expect(NVSDK_NGX_SUCCEED(api.release(handle)),"Release dispatch failed");
                expect(NVSDK_NGX_FAILED(api.release(handle)),"Released handle dispatched twice");
                expect(NVSDK_NGX_FAILED(api.evaluate(list,handle,p,nullptr)),"Released handle evaluated");
            }
        }
        std::printf("PASS: community adapter fixture %ls (CPU only).\n",mode.c_str()); return 0;
    } catch(const std::exception& e) { std::printf("FAIL: %s\n",e.what()); return 1; }
}
