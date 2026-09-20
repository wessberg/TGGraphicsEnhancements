// SPDX-License-Identifier: MIT
#pragma once
#include <d3d11.h>

namespace tgfg {
// Observe the real draw before a caller such as RenoDX restores its pipeline state.
// Kept shared with the WARP detour test so it exercises the production entry points.
struct D3D11DrawObserver {
    enum Kind { Draw, Indexed, Instanced, IndexedInstanced, Indirect, IndexedIndirect, Execute, Count };
    struct Call { Kind kind; UINT count=0,instances=0; };
    using Callback=void(*)(ID3D11DeviceContext*,Call) noexcept;
    inline static Callback callback=nullptr;
    inline static void(STDMETHODCALLTYPE* draw)(ID3D11DeviceContext*,UINT,UINT)=nullptr;
    inline static void(STDMETHODCALLTYPE* indexed)(ID3D11DeviceContext*,UINT,UINT,INT)=nullptr;
    inline static void(STDMETHODCALLTYPE* instanced)(ID3D11DeviceContext*,UINT,UINT,UINT,UINT)=nullptr;
    inline static void(STDMETHODCALLTYPE* indexedInstanced)(ID3D11DeviceContext*,UINT,UINT,UINT,INT,UINT)=nullptr;
    inline static void(STDMETHODCALLTYPE* indirect)(ID3D11DeviceContext*,ID3D11Buffer*,UINT)=nullptr;
    inline static void(STDMETHODCALLTYPE* indexedIndirect)(ID3D11DeviceContext*,ID3D11Buffer*,UINT)=nullptr;
    inline static void(STDMETHODCALLTYPE* execute)(ID3D11DeviceContext*,ID3D11CommandList*,BOOL)=nullptr;
    static void notify(ID3D11DeviceContext* c,Call call) noexcept {if(callback)callback(c,call);}
    static void STDMETHODCALLTYPE on_draw(ID3D11DeviceContext* c,UINT n,UINT first) {
        draw(c,n,first);notify(c,{Draw,n,1});
    }
    static void STDMETHODCALLTYPE on_indexed(ID3D11DeviceContext* c,UINT n,UINT first,INT base) {
        indexed(c,n,first,base);notify(c,{Indexed,n,1});
    }
    static void STDMETHODCALLTYPE on_instanced(ID3D11DeviceContext* c,UINT n,UINT instances,UINT first,UINT base) {
        instanced(c,n,instances,first,base);notify(c,{Instanced,n,instances});
    }
    static void STDMETHODCALLTYPE on_indexed_instanced(ID3D11DeviceContext* c,UINT n,UINT instances,UINT first,INT base,UINT initial) {
        indexedInstanced(c,n,instances,first,base,initial);notify(c,{IndexedInstanced,n,instances});
    }
    static void STDMETHODCALLTYPE on_indirect(ID3D11DeviceContext* c,ID3D11Buffer* args,UINT offset) {
        indirect(c,args,offset);notify(c,{Indirect});
    }
    static void STDMETHODCALLTYPE on_indexed_indirect(ID3D11DeviceContext* c,ID3D11Buffer* args,UINT offset) {
        indexedIndirect(c,args,offset);notify(c,{IndexedIndirect});
    }
    static void STDMETHODCALLTYPE on_execute(ID3D11DeviceContext* c,ID3D11CommandList* list,BOOL restore) {
        execute(c,list,restore);notify(c,{Execute});
    }
    // The originals above are one set per process. Installing against a second, different vtable would
    // republish them while the first set of detours is still live, so those detours would then call
    // the wrong originals with the wrong object. A bridge can now be built more than once in a
    // process, so refuse rather than overwrite; the same vtable reuses the hooks already in place.
    inline static void** installedTable=nullptr;
    template<class Installer> static bool install(ID3D11DeviceContext* context,Installer installHook,Callback observer) {
        auto** table=*reinterpret_cast<void***>(context);
        if(installedTable && installedTable!=table) return false;
        callback=observer;
        const bool installed=
            installHook(table[13],reinterpret_cast<void*>(&on_draw),reinterpret_cast<void**>(&draw)) &&
            installHook(table[12],reinterpret_cast<void*>(&on_indexed),reinterpret_cast<void**>(&indexed)) &&
            installHook(table[21],reinterpret_cast<void*>(&on_instanced),reinterpret_cast<void**>(&instanced)) &&
            installHook(table[20],reinterpret_cast<void*>(&on_indexed_instanced),reinterpret_cast<void**>(&indexedInstanced)) &&
            installHook(table[40],reinterpret_cast<void*>(&on_indirect),reinterpret_cast<void**>(&indirect)) &&
            installHook(table[39],reinterpret_cast<void*>(&on_indexed_indirect),reinterpret_cast<void**>(&indexedIndirect)) &&
            installHook(table[58],reinterpret_cast<void*>(&on_execute),reinterpret_cast<void**>(&execute));
        if(installed) installedTable=table;
        return installed;
    }
};
}
