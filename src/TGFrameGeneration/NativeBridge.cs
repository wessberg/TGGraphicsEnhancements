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
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int TextCall(byte[] buffer, uint size);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int TimingCall(ref NativeTiming timing, uint reset);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int NoteCall([MarshalAs(UnmanagedType.LPStr)] string text, uint flags);
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)]
        private static extern IntPtr GetModuleHandleW(string name);
        internal static bool IsLoaded(string module) => GetModuleHandleW(module)!=IntPtr.Zero;
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
        private FrameCall setNoTearing; // optional: present without the tearing flag (HybridVsync)
        private FrameCall setDlssPreset; // optional: DLSS Frame Generation's preset
        private TextCall readDiagnostics; // optional, the troubleshooting report's: facts, timings, notes
        private TimingCall readTimings;
        private NoteCall logNote;
        internal IntPtr RenderEvent { get; private set; }
        internal bool Connected => submit != null && read != null && RenderEvent!=IntPtr.Zero;
        internal string Detail { get; private set; } = "TGFG.Native.dll not loaded. No FG. It starts through TG Graphics Bridge: check BepInEx/TGGraphicsBridge/Autoload.log " +
            "and BepInEx/TGFrameGeneration/graphics-bridge-extension.json; start normally through Steam.";
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
                { Detail="Native ABI mismatch. This managed build requires native ABI 1.1 (0x00010100). Rebuild/package the matching backend."; return false; }
                var newSubmit=Bind<Submit>(module,"TGFG_Submit");
                var newRead=Bind<Read>(module,"TGFG_ReadStatus");
                begin=Bind<FrameCall>(module,"TGFG_BeginSimulation");
                end=Bind<FrameCall>(module,"TGFG_EndSimulation");
                setCamera=Bind<CameraCall>(module,"TGFG_SetCamera");
                try { setHdrp=Bind<HdrpCall>(module,"TGFG_SetHdrpInputs"); } catch(EntryPointNotFoundException) { setHdrp=null; }
                try { setHudless=Bind<FrameCall>(module,"TGFG_SetHudlessCapture"); } catch(EntryPointNotFoundException) { setHudless=null; }
                try { hudlessPacket=Bind<HudlessCall>(module,"TGFG_SetHudless"); } catch(EntryPointNotFoundException) { hudlessPacket=null; }
                try { setDebug=Bind<FrameCall>(module,"TGFG_SetDebugFlags"); } catch(EntryPointNotFoundException) { setDebug=null; }
                try { setNoTearing=Bind<FrameCall>(module,"TGFG_SetNoTearing"); } catch(EntryPointNotFoundException) { setNoTearing=null; }
                try { setDlssPreset=Bind<FrameCall>(module,"TGFG_SetDlssPreset"); } catch(EntryPointNotFoundException) { setDlssPreset=null; }
                try { readDiagnostics=Bind<TextCall>(module,"TGFG_ReadDiagnostics"); } catch(EntryPointNotFoundException) { readDiagnostics=null; }
                try { readTimings=Bind<TimingCall>(module,"TGFG_ReadTimings"); } catch(EntryPointNotFoundException) { readTimings=null; }
                try { logNote=Bind<NoteCall>(module,"TGFG_LogNote"); } catch(EntryPointNotFoundException) { logNote=null; }
                RenderEvent=Bind<GetEvent>(module,"TGFG_GetRenderEvent")();
                if(RenderEvent==IntPtr.Zero) throw new InvalidOperationException("No render event callback.");
                submit=newSubmit; read=newRead;
                Detail="Bound TGFG.Native.dll, TG Graphics Bridge's frame generation extension, ABI 1.1. Gameplay plugin binds only; native startup belongs to the bridge's BepInEx preloader.";
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
        internal int SetNoTearing(bool on) => Connected && setNoTearing!=null?setNoTearing(on?1u:0u):-100;
        internal int SetDlssPreset(uint preset) => Connected && setDlssPreset!=null?setDlssPreset(preset):-100;
        // What the native side learned (key=value): Streamline, DLSS-G, Reflex, FSR, XeSS, Anti-Lag 2, the last failure.
        // Read from the module even when the plugin never bound it, which is when these explain the most.
        internal System.Collections.Generic.Dictionary<string,string> Facts()
        {
            var read=readDiagnostics;
            if(read==null)
            {
                var module=GetModuleHandleW("TGFG.Native.dll");
                IntPtr address=module!=IntPtr.Zero?GetProcAddress(module,"TGFG_ReadDiagnostics"):IntPtr.Zero;
                if(address==IntPtr.Zero) return new System.Collections.Generic.Dictionary<string,string>();
                read=readDiagnostics=Marshal.GetDelegateForFunctionPointer<TextCall>(address);
            }
            return TGGraphicsBridge.GraphicsBridge.ParseFacts(Text(read));
        }
        private static string Text(TextCall read)
        {
            try
            {
                var buffer=new byte[16384];
                int length=read(buffer,(uint)buffer.Length);
                if(length>=buffer.Length) { buffer=new byte[length+1]; length=read(buffer,(uint)buffer.Length); }
                return length>0?System.Text.Encoding.UTF8.GetString(buffer,0,Math.Min(length,buffer.Length-1)):"";
            }
            catch { return ""; }
        }
        internal bool ReadTimings(bool reset,out NativeTiming timing)
        {
            timing=new NativeTiming { Size=NativeTiming.SizeBytes, Abi=Abi.Version };
            return Connected && readTimings!=null && readTimings(ref timing,reset?1u:0u)==0;
        }
        // One line in TGFG.Native.log. endWindow ends the pipeline summary's window at the next present; hold
        // (with it) keeps the 5 s summaries back until the next endWindow, so one summary covers one window.
        internal void Note(string text,bool endWindow=false,bool hold=false)
        {
            try { if(Connected) logNote?.Invoke(TGGraphicsBridge.GraphicsBridge.AsciiText(text),(endWindow?1u:0u)|(hold?2u:0u)); } catch { }
        }
    }
}
