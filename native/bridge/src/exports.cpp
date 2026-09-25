// SPDX-License-Identifier: MIT
#include "core.hpp"
#include <algorithm>
#include <cstring>

namespace tgb {
int32_t add_extension(const wchar_t* path);
uint32_t bootstrap();
void read_performance(TGB_Performance& out);
}
#include "diagnostics.hpp"

TGB_EXPORT uint32_t TGB_CALL TGGraphicsBridge_GetAbiVersion() { return TGB_ABI; }
TGB_EXPORT int32_t TGB_CALL TGGraphicsBridge_AddExtension(const wchar_t* path) {
    try { return tgb::add_extension(path); } catch(...) { return -10; }
}
TGB_EXPORT uint32_t __stdcall TGGraphicsBridge_Bootstrap(void*) {
    try { return tgb::bootstrap(); }
    catch(const std::exception& e) { tgb::log(std::string("Bootstrap exception: ")+e.what()); return 4; }
    catch(...) { return 4; }
}
TGB_EXPORT int32_t TGB_CALL TGGraphicsBridge_ReadStatus(TGB_Status* out) {
    if(!out || out->size!=sizeof(TGB_Status) || out->abi!=TGB_ABI) return -1;
    try { auto& c=tgb::core(); std::lock_guard<std::recursive_mutex> lock(c.mutex); *out=c.status; return 0; }
    catch(...) { return -2; }
}
TGB_EXPORT int32_t TGB_CALL TGGraphicsBridge_GetExtensionName(uint32_t index, char* buffer, uint32_t size) {
    try {
        auto& c=tgb::core(); std::lock_guard<std::recursive_mutex> lock(c.mutex);
        if(index>=c.extensions.size() || !buffer || !size) return -1;
        const auto& e=c.extensions[index];
        const std::string name=e.name.empty() ? tgb::narrow(e.path) : e.name;
        const size_t count=std::min<size_t>(name.size(), size-1);
        std::memcpy(buffer, name.data(), count); buffer[count]=0;
        return int32_t(count);
    } catch(...) { return -2; }
}
TGB_EXPORT int32_t TGB_CALL TGGraphicsBridge_GetExtensionState(uint32_t index) {
    try {
        auto& c=tgb::core(); std::lock_guard<std::recursive_mutex> lock(c.mutex);
        if(index>=c.extensions.size()) return -1;
        return c.extensions[index].active ? 1 : 0;
    } catch(...) { return -2; }
}
TGB_EXPORT int32_t TGB_CALL TGGraphicsBridge_ReadPerformance(TGB_Performance* out) {
    if(!out || out->size!=sizeof(TGB_Performance) || out->abi!=TGB_ABI) return -1;
    try { tgb::read_performance(*out); return 0; } catch(...) { return -2; }
}
TGB_EXPORT int32_t TGB_CALL TGGraphicsBridge_ReadDiagnostics(char* buffer, uint32_t size) {
    try {
        const std::string text=tgb::all_facts();
        if(buffer && size) {
            const size_t count=std::min<size_t>(text.size(), size-1);
            std::memcpy(buffer, text.data(), count); buffer[count]=0;
        }
        return int32_t(std::min<size_t>(text.size(), INT32_MAX));
    } catch(...) { return -2; }
}
TGB_EXPORT void TGB_CALL TGGraphicsBridge_LogNote(const char* text) {
    try { if(text) tgb::log(std::string("Note: ")+text); } catch(...) {}
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    // No DisableThreadLibraryCalls: the static C runtime needs thread notifications (Microsoft says not to
    // call it from a DLL linked to the static CRT).
    if(reason==DLL_PROCESS_ATTACH) tgb::core().self=module;
    // Never initialize MinHook, load extensions or touch the GPU under loader lock.
    return TRUE;
}
