// SPDX-License-Identifier: MIT
// CPU, GPU and video memory use for the performance overlay, sampled twice a second on a thread of its own.
// The thread starts with the first read, so nothing runs while no overlay asks.
//   CPU: this process's share of all logical processors (GetProcessTimes), and the whole system's
//        (GetSystemTimes).
//   GPU: Windows' per-engine utilization counters (PDH "GPU Engine"), taking the busiest engine type,
//        for this process and for all of them, as Task Manager does.
//   Video memory: this process's use of the game adapter's local memory and the budget Windows gives it
//        (IDXGIAdapter3::QueryVideoMemoryInfo), and the adapter's dedicated memory.
#include "core.hpp"
#include <pdh.h>
#include <pdhmsg.h>
#include <algorithm>
#include <cwctype>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tgb {
namespace {
std::mutex sampleMutex;
TGB_Performance latest{};
std::once_flag started;

uint64_t ticks(const FILETIME& f) { return (uint64_t(f.dwHighDateTime)<<32)|f.dwLowDateTime; }

class GpuCounters {
    PDH_HQUERY query_=nullptr;
    PDH_HCOUNTER counter_=nullptr;
    std::wstring self_=L"pid_"+std::to_wstring(GetCurrentProcessId())+L"_";
    std::vector<uint8_t> buffer_;
public:
    GpuCounters() {
        if(PdhOpenQueryW(nullptr, 0, &query_)!=ERROR_SUCCESS) { query_=nullptr; return; }
        if(PdhAddEnglishCounterW(query_, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter_)!=ERROR_SUCCESS) {
            PdhCloseQuery(query_); query_=nullptr; return;
        }
        PdhCollectQueryData(query_); // a rate needs two samples
    }
    // Percent busy of the busiest engine of the game's GPU, this process's and everyone's, as Task Manager
    // counts it: an engine is one physical engine of one adapter ("luid_..._phys_N_eng_N_engtype_X" in the
    // instance name), its usage the sum over the processes on it. Two 3D engines at 40% are 40%, not 80%,
    // and a laptop's other GPU doesn't count. luid: the game's adapter, or null for every adapter.
    bool sample(float& process, float& system, const LUID* luid) {
        if(!query_ || PdhCollectQueryData(query_)!=ERROR_SUCCESS) return false;
        DWORD bytes=0, count=0;
        if(PdhGetFormattedCounterArrayW(counter_, PDH_FMT_DOUBLE, &bytes, &count, nullptr)!=PDH_MORE_DATA || !bytes) return false;
        buffer_.resize(bytes);
        auto* items=reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer_.data());
        if(PdhGetFormattedCounterArrayW(counter_, PDH_FMT_DOUBLE, &bytes, &count, items)!=ERROR_SUCCESS) return false;
        std::wstring adapter;
        if(luid) {
            wchar_t text[48]{};
            swprintf_s(text, L"LUID_0X%08X_0X%08X", static_cast<unsigned>(luid->HighPart), static_cast<unsigned>(luid->LowPart));
            adapter=text;
        }
        std::map<std::wstring, double> mine, all; // by engine
        for(DWORD i=0;i<count;++i) {
            const auto status=items[i].FmtValue.CStatus;
            if((status!=PDH_CSTATUS_VALID_DATA && status!=PDH_CSTATUS_NEW_DATA) || !items[i].szName) continue;
            std::wstring name=items[i].szName;
            const size_t at=name.find(L"luid_");
            if(at==std::wstring::npos || name.find(L"engtype_")==std::wstring::npos) continue;
            std::wstring engine=name.substr(at);
            for(auto& c:engine) c=static_cast<wchar_t>(towupper(c));
            if(!adapter.empty() && engine.compare(0, adapter.size(), adapter)!=0) continue;
            const double value=items[i].FmtValue.doubleValue;
            all[engine]+=value;
            if(name.compare(0, self_.size(), self_)==0) mine[engine]+=value;
        }
        double p=0, s=0;
        for(const auto& e:mine) p=std::max(p, e.second);
        for(const auto& e:all) s=std::max(s, e.second);
        process=float(std::min(100.0, p)); system=float(std::min(100.0, s));
        return true;
    }
};

// The game's adapter, from the game's device; the bridge keeps every device it observed alive.
ComPtr<IDXGIAdapter3> game_adapter(uint32_t& generation) {
    auto& c=core();
    ComPtr<ID3D11Device5> device;
    {
        std::lock_guard<std::recursive_mutex> lock(c.mutex);
        if(!c.gameDevices.empty()) device=c.gameDevices.back();
        generation=c.status.gameDeviceGeneration;
    }
    ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> adapter; ComPtr<IDXGIAdapter3> adapter3;
    if(device && SUCCEEDED(device.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&adapter))) adapter.As(&adapter3);
    return adapter3;
}

void sampler() {
    const double processors=std::max<DWORD>(1, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    GpuCounters gpu;
    uint64_t lastProcess=0, lastWall=0, lastIdle=0, lastKernel=0, lastUser=0;
    ComPtr<IDXGIAdapter3> adapter;
    uint32_t adapterGeneration=0;
    // The baselines, so that the first sample already has CPU figures.
    {
        FILETIME created, exited, kernel, user, now, idle, systemKernel, systemUser;
        if(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
            GetSystemTimeAsFileTime(&now);
            lastProcess=ticks(kernel)+ticks(user); lastWall=ticks(now);
        }
        if(GetSystemTimes(&idle, &systemKernel, &systemUser)) { lastIdle=ticks(idle); lastKernel=ticks(systemKernel); lastUser=ticks(systemUser); }
    }
    for(;;) {
        Sleep(500);
        TGB_Performance p{};
        FILETIME created, exited, kernel, user, now, idle, systemKernel, systemUser;
        if(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
            GetSystemTimeAsFileTime(&now);
            const uint64_t process=ticks(kernel)+ticks(user), wall=ticks(now);
            if(lastWall && wall>lastWall) { p.cpuProcess=float(std::min(100.0, 100.0*double(process-lastProcess)/(double(wall-lastWall)*processors))); p.valid|=1u; }
            lastProcess=process; lastWall=wall;
        }
        if(GetSystemTimes(&idle, &systemKernel, &systemUser)) {
            const uint64_t i=ticks(idle), k=ticks(systemKernel), u=ticks(systemUser);
            if(lastKernel) {
                const uint64_t total=(k-lastKernel)+(u-lastUser); // kernel time includes idle time
                if(total) p.cpuSystem=float(std::min(100.0, 100.0*double(total-(i-lastIdle))/double(total)));
            }
            lastIdle=i; lastKernel=k; lastUser=u;
        }
        LUID luid{}; bool haveLuid=false;
        if(adapter) { DXGI_ADAPTER_DESC d{}; if(SUCCEEDED(adapter->GetDesc(&d))) { luid=d.AdapterLuid; haveLuid=true; } }
        if(gpu.sample(p.gpuProcess, p.gpuSystem, haveLuid ? &luid : nullptr)) p.valid|=2u;
        uint32_t generation=0;
        {
            std::lock_guard<std::recursive_mutex> lock(core().mutex);
            generation=core().status.gameDeviceGeneration;
        }
        if(!adapter || generation!=adapterGeneration) adapter=game_adapter(adapterGeneration);
        if(adapter) {
            DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
            DXGI_ADAPTER_DESC desc{};
            if(SUCCEEDED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory)) && SUCCEEDED(adapter->GetDesc(&desc))) {
                p.vramProcess=memory.CurrentUsage; p.vramBudget=memory.Budget; p.vramTotal=desc.DedicatedVideoMemory;
                p.valid|=4u;
            }
        }
        std::lock_guard<std::mutex> lock(sampleMutex);
        latest=p;
    }
}
} // namespace

void read_performance(TGB_Performance& out) {
    // A detached thread: an exception leaving it would end the game. After one, sampling starts over.
    std::call_once(started, []{ std::thread([]{
        for(;;) {
            try { sampler(); }
            catch(...) { log_throttled("The performance sampler hit an error and starts over in 5 s"); }
            Sleep(5000);
        }
    }).detach(); });
    const uint32_t size=out.size, abi=out.abi;
    {
        std::lock_guard<std::mutex> lock(sampleMutex);
        out=latest;
    }
    out.size=size; out.abi=abi;
}
} // namespace tgb
