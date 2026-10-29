// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <string>
#include <string_view>
namespace tgnr {
// Keep this handle alive while the module lives: deny replacement or writes between hashing and
// LoadLibrary. It also makes a failed read a rejection, never an unknown-file compatibility fallback.
class RuntimeFileLock {
    HANDLE file_=INVALID_HANDLE_VALUE;
public:
    explicit RuntimeFileLock(const wchar_t* file) : file_(CreateFileW(file,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)) {}
    ~RuntimeFileLock() { if(valid()) CloseHandle(file_); }
    RuntimeFileLock(const RuntimeFileLock&)=delete;
    RuntimeFileLock& operator=(const RuntimeFileLock&)=delete;
    bool valid() const { return file_!=INVALID_HANDLE_VALUE; }
    bool matches(std::string_view expected) const {
        const auto actual=sha256(); return !actual.empty() && actual==expected;
    }
    std::string sha256() const {
        if(!valid()) return {};
        LARGE_INTEGER zero{}; if(!SetFilePointerEx(file_,zero,nullptr,FILE_BEGIN)) return {};
        BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
        if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return {};
        if(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0) { BCryptCloseAlgorithmProvider(algorithm,0); return {}; }
        std::array<UCHAR,4096> bytes{}; DWORD count=0; bool ok=true;
        for(;;) {
            if(!ReadFile(file_,bytes.data(),DWORD(bytes.size()),&count,nullptr)) { ok=false; break; }
            if(!count) break;
            if(BCryptHashData(hash,bytes.data(),count,0)<0) { ok=false; break; }
        }
        std::array<UCHAR,32> digest{};
        ok=ok && BCryptFinishHash(hash,digest.data(),ULONG(digest.size()),0)>=0;
        BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm,0);
        if(!ok) return {};
        std::string result; result.reserve(64); constexpr char hex[]="0123456789abcdef";
        for(auto b:digest) { result.push_back(hex[b>>4]); result.push_back(hex[b&15]); } return result;
    }
};
}
