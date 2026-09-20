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
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int HdrpCall(ref NativeHdrpInputs inputs);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int HudlessCall(ref NativeHudless inputs);
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)]
        private static extern IntPtr GetModuleHandleW(string name);
        [DllImport("kernel32.dll", CharSet=CharSet.Ansi, ExactSpelling=true)]
        private static extern IntPtr GetProcAddress(IntPtr module, string name);
        private Submit submit;
        private Read read;
        private FrameCall begin, end;
        private CameraCall setCamera;
        private HdrpCall setHdrp; // optional: a native backend without it takes inputs from DLSS only
        private FrameCall setHudless; // optional: a native backend without it interpolates the HUD
        private HudlessCall hudlessPacket;
        private FrameCall setDebug;   // optional: SDK debug overlays
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
                { Detail="Native ABI mismatch. This managed build requires native ABI v5 (0x00050000). Rebuild/package the matching backend."; return false; }
                var newSubmit=Bind<Submit>(module,"TGFG_Submit");
                var newRead=Bind<Read>(module,"TGFG_ReadStatus");
                begin=Bind<FrameCall>(module,"TGFG_BeginSimulation");
                end=Bind<FrameCall>(module,"TGFG_EndSimulation");
                setCamera=Bind<CameraCall>(module,"TGFG_SetCamera");
                try { setHdrp=Bind<HdrpCall>(module,"TGFG_SetHdrpInputs"); } catch(EntryPointNotFoundException) { setHdrp=null; }
                try { setHudless=Bind<FrameCall>(module,"TGFG_SetHudlessCapture"); } catch(EntryPointNotFoundException) { setHudless=null; }
                try { hudlessPacket=Bind<HudlessCall>(module,"TGFG_SetHudless"); } catch(EntryPointNotFoundException) { hudlessPacket=null; }
                try { setDebug=Bind<FrameCall>(module,"TGFG_SetDebugFlags"); } catch(EntryPointNotFoundException) { setDebug=null; }
                RenderEvent=Bind<GetEvent>(module,"TGFG_GetRenderEvent")();
                if(RenderEvent==IntPtr.Zero) throw new InvalidOperationException("No render event callback.");
                submit=newSubmit; read=newRead;
                Detail="Bound standalone TGFG.Native.dll, DX11-to-DX12 presentation bridge ABI v5. Gameplay plugin binds only; native startup belongs to the BepInEx preloader.";
                return true;
            }
            catch(Exception ex) { Detail="Native binding failed: "+ex.Message; return false; }
        }
        internal bool Exchange(ref NativeRequest request,out NativeStatus status)
        {
            status=new NativeStatus { Size=(uint)Abi.StatusSize, Abi=Abi.Version };
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
        internal bool CanSetHdrpInputs => Connected && setHdrp!=null;
        internal int SetHdrpInputs(ref NativeHdrpInputs inputs) => CanSetHdrpInputs?setHdrp(ref inputs):-100;
        internal bool CanSetHudless => Connected && setHudless!=null && hudlessPacket!=null;
        internal int SetHudless(ref NativeHudless packet) => CanSetHudless?hudlessPacket(ref packet):-100;
        internal int SetHudlessCapture(bool enabled) => Connected && setHudless!=null?setHudless(enabled?1u:0u):-100;
        internal int SetDebugFlags(uint flags) => Connected && setDebug!=null?setDebug(flags):-100;
    }
}
