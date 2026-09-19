// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <TlHelp32.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
namespace fs=std::filesystem;
static std::wstring module_path(HMODULE module) {
    std::vector<wchar_t> buffer(MAX_PATH);
    for(;;) {
        const DWORD length=GetModuleFileNameW(module,buffer.data(),static_cast<DWORD>(buffer.size()));
        if(!length) return {};
        if(length<buffer.size()) return std::wstring(buffer.data(),length);
        if(buffer.size()>=32768) return {};
        buffer.resize(buffer.size()*2);
    }
}
struct Handle {
    HANDLE h=nullptr;
    explicit Handle(HANDLE value=nullptr):h(value) {}
    ~Handle(){if(h && h!=INVALID_HANDLE_VALUE) CloseHandle(h);}
    Handle(const Handle&)=delete; Handle& operator=(const Handle&)=delete;
};
static std::runtime_error error(const char* action) {
    return std::runtime_error(std::string(action)+" (Win32 "+std::to_string(GetLastError())+")");
}
static std::wstring quote(const std::wstring& input) {
    std::wstring out=L"\""; size_t slash=0;
    for(auto c:input) {
        if(c==L'\\') { ++slash; continue; }
        if(c==L'\"') out.append(slash*2+1,L'\\'); else out.append(slash,L'\\');
        slash=0; out+=c;
    }
    out.append(slash*2,L'\\'); out+=L'\"'; return out;
}
static uintptr_t remote_module(DWORD pid,const std::wstring& name) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid));
    if(snapshot.h==INVALID_HANDLE_VALUE) throw error("Snapshot process modules");
    MODULEENTRY32W entry{}; entry.dwSize=sizeof(entry);
    if(Module32FirstW(snapshot.h,&entry)) do {
        if(_wcsicmp(entry.szModule,name.c_str())==0) return reinterpret_cast<uintptr_t>(entry.modBaseAddr);
    } while(Module32NextW(snapshot.h,&entry));
    return 0;
}
static uintptr_t remote_system_function(DWORD pid,FARPROC address) {
    HMODULE owner=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT|GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(address),&owner)) throw error("Find loader function module");
    auto remote=remote_module(pid,fs::path(module_path(owner)).filename().wstring());
    if(!remote) throw std::runtime_error("System loader module is not yet visible in the suspended child. No address was guessed.");
    return remote+(reinterpret_cast<uintptr_t>(address)-reinterpret_cast<uintptr_t>(owner));
}
static DWORD call(HANDLE process,uintptr_t function,void* parameter) {
    Handle thread(CreateRemoteThread(process,nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(function),parameter,0,nullptr));
    if(!thread.h) throw error("Start bootstrap thread");
    if(WaitForSingleObject(thread.h,60000)!=WAIT_OBJECT_0) throw std::runtime_error("Native bootstrap did not complete; terminating this child process.");
    DWORD result=0; if(!GetExitCodeThread(thread.h,&result)) throw error("Read bootstrap result");
    return result;
}

// Run only the Windows loader, then rendezvous at the executable's PE entry
// point. Injecting into a just-created suspended process can deadlock on loader
// initialization or find KernelBase absent. A restored entry-point breakpoint
// avoids both, without patching any file on disk or attaching to an existing game.
template<class T>T remote_read(HANDLE process,uintptr_t address) {
    T result{};SIZE_T bytes=0;
    if(!ReadProcessMemory(process,reinterpret_cast<void*>(address),&result,sizeof(result),&bytes)||bytes!=sizeof(result))
        throw error("Read child image header");
    return result;
}
static void write_code_byte(HANDLE process,uintptr_t address,BYTE value) {
    DWORD protection=0,ignored=0;SIZE_T bytes=0;void* at=reinterpret_cast<void*>(address);
    if(!VirtualProtectEx(process,at,1,PAGE_EXECUTE_READWRITE,&protection))throw error("Protect child entrypoint");
    const BOOL written=WriteProcessMemory(process,at,&value,1,&bytes);
    const DWORD writeError=GetLastError();
    const BOOL restored=VirtualProtectEx(process,at,1,protection,&ignored);
    FlushInstructionCache(process,at,1);
    if(!written||bytes!=1){SetLastError(writeError);throw error("Write child entrypoint");}
    if(!restored)throw error("Restore child entrypoint protection");
}
static void stop_at_entrypoint(PROCESS_INFORMATION& process,bool& debugging) {
    if(ResumeThread(process.hThread)==DWORD(-1))throw error("Run Windows loader");
    uintptr_t entry=0;BYTE original=0;bool firstLoaderBreakpoint=true;
    const ULONGLONG started=GetTickCount64();
    while(GetTickCount64()-started<60000) {
        DEBUG_EVENT event{};
        if(!WaitForDebugEvent(&event,1000)) {
            if(GetLastError()==ERROR_SEM_TIMEOUT)continue;
            throw error("Wait for loader rendezvous");
        }
        DWORD continuation=DBG_CONTINUE;
        if(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT) {
            if(event.u.CreateProcessInfo.hFile)CloseHandle(event.u.CreateProcessInfo.hFile);
            const auto base=reinterpret_cast<uintptr_t>(event.u.CreateProcessInfo.lpBaseOfImage);
            auto dos=remote_read<IMAGE_DOS_HEADER>(process.hProcess,base);
            if(dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<=0 || dos.e_lfanew>1048576)
                throw std::runtime_error("Child is not a valid PE image.");
            auto nt=remote_read<IMAGE_NT_HEADERS64>(process.hProcess,base+dos.e_lfanew);
            if(nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
               nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC || !nt.OptionalHeader.AddressOfEntryPoint ||
               nt.OptionalHeader.AddressOfEntryPoint>=nt.OptionalHeader.SizeOfImage)
                throw std::runtime_error("Launcher requires an x64 PE executable with a valid entry point.");
            entry=base+nt.OptionalHeader.AddressOfEntryPoint;
            original=remote_read<BYTE>(process.hProcess,entry);
            if(original==0xCC)throw std::runtime_error("Executable entrypoint is already a breakpoint. No breakpoint was replaced.");
            write_code_byte(process.hProcess,entry,0xCC);
        } else if(event.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT) {
            if(event.u.LoadDll.hFile)CloseHandle(event.u.LoadDll.hFile);
        } else if(event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT) {
            auto& info=event.u.Exception;
            if(info.ExceptionRecord.ExceptionCode==EXCEPTION_BREAKPOINT &&
               reinterpret_cast<uintptr_t>(info.ExceptionRecord.ExceptionAddress)==entry && entry) {
                if(event.dwThreadId!=process.dwThreadId)throw std::runtime_error("Entrypoint reached on an unexpected thread.");
                write_code_byte(process.hProcess,entry,original);
                CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL;
                if(!GetThreadContext(process.hThread,&context))throw error("Read entrypoint context");
                context.Rip=entry;
                if(!SetThreadContext(process.hThread,&context))throw error("Restore entrypoint instruction pointer");
                // Retain one explicit suspend count after continuing/detaching.
                if(SuspendThread(process.hThread)==DWORD(-1))throw error("Hold main thread at entrypoint");
                if(!ContinueDebugEvent(event.dwProcessId,event.dwThreadId,DBG_CONTINUE))throw error("Continue entrypoint event");
                if(!DebugActiveProcessStop(process.dwProcessId))throw error("Detach entrypoint debugger");
                debugging=false;
                std::wcout<<L"PID "<<process.dwProcessId<<L": Windows loader completed; held at restored executable entrypoint.\n";
                return;
            }
            if(info.ExceptionRecord.ExceptionCode==EXCEPTION_BREAKPOINT && firstLoaderBreakpoint)firstLoaderBreakpoint=false;
            else continuation=DBG_EXCEPTION_NOT_HANDLED;
        } else if(event.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT) {
            const auto code=event.u.ExitProcess.dwExitCode;
            ContinueDebugEvent(event.dwProcessId,event.dwThreadId,DBG_CONTINUE);debugging=false;
            throw std::runtime_error("Game exited before entrypoint bootstrap: "+std::to_string(code));
        }
        if(!ContinueDebugEvent(event.dwProcessId,event.dwThreadId,continuation))throw error("Continue Windows loader");
    }
    throw std::runtime_error("Timed out before the game executable entrypoint.");
}
int wmain(int argc,wchar_t** argv) {
    fs::path game,native; std::vector<std::wstring> arguments;
    native=fs::path(module_path(nullptr)).parent_path()/L"TGFG.Native.dll";
    for(int i=1;i<argc;++i) {
        std::wstring a=argv[i];
        if(a==L"--") { while(++i<argc) arguments.emplace_back(argv[i]); break; }
        if((a==L"--game" || a==L"--native") && i+1<argc) {
            auto value=fs::absolute(argv[++i]); if(a==L"--game") game=value; else native=value;
        } else {
            std::wcerr<<L"Usage: TGFG.Launcher.exe --game <game.exe> [--native <TGFG.Native.dll>] -- -force-d3d11 [game args]\n"; return 2;
        }
    }
    if(game.empty() || !fs::is_regular_file(game) || !fs::is_regular_file(native)) {
        std::wcerr<<L"Supply an existing game executable and TGFG.Native.dll. No process was started.\n"; return 2;
    }
    // The game stays D3D11. D3D12 is private to TGFG's output backend.
    bool d3d11=false;
    for(auto& a:arguments) {
        if(a==L"-force-d3d12" || a==L"-force-vulkan" || a==L"-force-glcore") { std::wcerr<<L"This build requires the game to use D3D11.\n"; return 2; }
        if(a==L"-force-d3d11") d3d11=true;
    }
    if(!d3d11) { std::wcerr<<L"Pass -- -force-d3d11 explicitly. D3D12 is used only by the private TGFG presentation bridge.\n"; return 2; }
    std::wstring command=quote(game.wstring()); for(auto& a:arguments) command+=L" "+quote(a);
    std::vector<wchar_t> mutableCommand(command.begin(),command.end()); mutableCommand.push_back(0);
    STARTUPINFOW startup{}; startup.cb=sizeof(startup); PROCESS_INFORMATION process{};
    if(!CreateProcessW(game.c_str(),mutableCommand.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|DEBUG_ONLY_THIS_PROCESS,nullptr,game.parent_path().c_str(),&startup,&process)) {
        std::cerr<<error("Create suspended game").what()<<"\n"; return 3;
    }
    Handle processHandle(process.hProcess),mainThread(process.hThread);
    void* remotePath=nullptr; bool debugging=true;
    try {
        stop_at_entrypoint(process,debugging);
        std::wstring dll=fs::absolute(native).wstring(); SIZE_T bytes=(dll.size()+1)*sizeof(wchar_t),written=0;
        remotePath=VirtualAllocEx(process.hProcess,nullptr,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        if(!remotePath) throw error("Allocate DLL path in child");
        if(!WriteProcessMemory(process.hProcess,remotePath,dll.c_str(),bytes,&written) || written!=bytes) throw error("Write DLL path");
        auto load=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");
        call(process.hProcess,remote_system_function(process.dwProcessId,load),remotePath);
        VirtualFreeEx(process.hProcess,remotePath,0,MEM_RELEASE); remotePath=nullptr;
        auto remoteBase=remote_module(process.dwProcessId,native.filename().wstring());
        if(!remoteBase) throw std::runtime_error("TGFG.Native.dll was not loaded. Check x64 architecture and VC++ runtime dependencies.");
        // Map only to obtain export RVA. DllMain is NOT run in the launcher.
        HMODULE image=LoadLibraryExW(native.c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES);
        if(!image) throw error("Map native export table");
        auto bootstrap=GetProcAddress(image,"TGFG_Bootstrap");
        auto rva=bootstrap?reinterpret_cast<uintptr_t>(bootstrap)-reinterpret_cast<uintptr_t>(image):0;
        FreeLibrary(image);
        if(!rva) throw std::runtime_error("TGFG_Bootstrap export is missing.");
        DWORD result=call(process.hProcess,remoteBase+rva,nullptr);
        if(result!=0) throw std::runtime_error("TGFG_Bootstrap failed with code "+std::to_string(result)+". See TGFG.Native.log beside the DLL.");
        if(ResumeThread(process.hThread)==DWORD(-1)) throw error("Resume game");
        std::wcout<<L"Started PID "<<process.dwProcessId<<L" with DX11-to-DX12 Streamline bootstrap. No proxy DLL was replaced.\n";
        std::wcout<<L"Steam must already be running. A Steam-relaunched child will not inherit these in-process hooks.\n";
        return 0;
    } catch(const std::exception& e) {
        // This helper only operates on the process it just created. Never leave
        // that process running with a partial graphics-hook installation.
        TerminateProcess(process.hProcess,1);
        if(debugging)DebugActiveProcessStop(process.dwProcessId);
        WaitForSingleObject(process.hProcess,5000);
        std::cerr<<e.what()<<"\n"; return 4;
    }
}
