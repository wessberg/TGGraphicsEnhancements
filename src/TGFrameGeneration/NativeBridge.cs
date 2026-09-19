// SPDX-License-Identifier: MIT
using System;
using System.Runtime.InteropServices;
namespace TGFrameGeneration
{
    internal sealed class NativeBridge
    {
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate uint GetAbi();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int Submit(ref NativeRequest request);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int Read(ref NativeStatus status);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int FrameCall(uint frameId);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int CameraCall(ref NativeCamera camera);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr GetEvent();
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)]
        private static extern IntPtr GetModuleHandleW(string name);
        [DllImport("kernel32.dll", CharSet=CharSet.Ansi, ExactSpelling=true)]
        private static extern IntPtr GetProcAddress(IntPtr module, string name);
        private Submit submit;
        private Read read;
        private FrameCall begin, end;
        private CameraCall setCamera;
        internal IntPtr RenderEvent { get; private set; }
        internal bool Connected => submit != null && read != null && RenderEvent!=IntPtr.Zero;
        internal string Detail { get; private set; } = "TGFG.Native.dll not loaded. No FG. Check BepInEx/patchers/TGFG.Autoload.dll and BepInEx/TGFrameGeneration/Autoload.log; start normally through Steam.";
        private static T Bind<T>(IntPtr module,string symbol) where T:Delegate
        {
            IntPtr address=GetProcAddress(module,symbol);
            if(address==IntPtr.Zero) throw new EntryPointNotFoundException(symbol);
            return Marshal.GetDelegateForFunctionPointer<T>(address);
        }
        internal bool Discover()
        {
            if(Connected) return true;
            try
            {
                var module=GetModuleHandleW("TGFG.Native.dll");
                if(module==IntPtr.Zero) return false;
                if(Bind<GetAbi>(module,"TGFG_GetAbiVersion")()!=Abi.Version)
                { Detail="Native ABI mismatch. This managed build requires native ABI v4 (0x00040000). Rebuild/package the matching backend."; return false; }
                var newSubmit=Bind<Submit>(module,"TGFG_Submit");
                var newRead=Bind<Read>(module,"TGFG_ReadStatus");
                begin=Bind<FrameCall>(module,"TGFG_BeginSimulation");
                end=Bind<FrameCall>(module,"TGFG_EndSimulation");
                setCamera=Bind<CameraCall>(module,"TGFG_SetCamera");
                RenderEvent=Bind<GetEvent>(module,"TGFG_GetRenderEvent")();
                if(RenderEvent==IntPtr.Zero) throw new InvalidOperationException("No render event callback.");
                submit=newSubmit; read=newRead;
                Detail="Bound standalone TGFG.Native.dll, DX11-to-DX12 direct Streamline ABI v4. Gameplay plugin binds only; native startup belongs to the BepInEx preloader.";
                return true;
            }
            catch(Exception ex) { Detail="Native binding failed: "+ex.Message; return false; }
        }
        internal bool Exchange(ref NativeRequest request,out NativeStatus status)
        {
            status=new NativeStatus { Size=152, Abi=Abi.Version };
            if(!Connected) return false;
            int result=submit(ref request);
            if(result!=0) { Detail="TGFG_Submit rejected request: "+result; return false; }
            result=read(ref status);
            if(result!=0) { Detail="TGFG_ReadStatus failed: "+result; return false; }
            return true;
        }
        internal int Begin(uint id) => Connected?begin(id):-100;
        internal int End(uint id) => Connected?end(id):-100;
        internal int SetCamera(ref NativeCamera camera) => Connected?setCamera(ref camera):-100;
    }
}
