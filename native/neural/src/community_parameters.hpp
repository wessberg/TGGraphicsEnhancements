// SPDX-License-Identifier: MIT
#pragma once
#include <nvsdk_ngx_params.h>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>
namespace tgnr {
// The public parameter interface, implemented and owned here. No private driver vtable offsets,
// no casting model handles to driver handles, and no cross-CRT allocation/deallocation.
class CommunityParameters final : public NVSDK_NGX_Parameter {
    using Value=std::variant<unsigned long long,float,double,unsigned,int,ID3D11Resource*,ID3D12Resource*,void*>;
    std::unordered_map<std::string,Value> values_;
    template<class T> NVSDK_NGX_Result get(const char* key,T* out) const {
        if(!key || !out) return NVSDK_NGX_Result_FAIL_InvalidParameter;
        auto it=values_.find(key); if(it==values_.end()) return NVSDK_NGX_Result_FAIL_InvalidParameter;
        bool ok=false;
        std::visit([&](auto value) {
            using V=decltype(value);
            if constexpr(std::is_arithmetic_v<T> && std::is_arithmetic_v<V>) { *out=static_cast<T>(value); ok=true; }
            else if constexpr(std::is_pointer_v<T> && std::is_pointer_v<V>) { *out=reinterpret_cast<T>(value); ok=true; }
        },it->second);
        return ok ? NVSDK_NGX_Result_Success:NVSDK_NGX_Result_FAIL_InvalidParameter;
    }
public:
#define TGNR_PARAMETER(T) void Set(const char* key,T value) override { if(key) values_.insert_or_assign(key,value); } \
    NVSDK_NGX_Result Get(const char* key,T* out) const override { return get(key,out); }
    TGNR_PARAMETER(unsigned long long)
    TGNR_PARAMETER(float)
    TGNR_PARAMETER(double)
    TGNR_PARAMETER(unsigned)
    TGNR_PARAMETER(int)
    TGNR_PARAMETER(ID3D11Resource*)
    TGNR_PARAMETER(ID3D12Resource*)
    TGNR_PARAMETER(void*)
#undef TGNR_PARAMETER
    void Reset() override { values_.clear(); }
};
}
