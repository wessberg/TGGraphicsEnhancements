// SPDX-License-Identifier: MIT
using System;
using System.Runtime.InteropServices;
using System.Text;
namespace TGNeuralRendering
{
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NeuralFrame
    {
        internal uint Size, Abi, Frame, Camera;
        internal ulong Color, Depth, Motion;
        internal uint Width, Height, RenderWidth, RenderHeight, Flags;
        internal float Strength, JitterX, JitterY, Near, Far;
        internal uint Model;
        internal float LocalTone, LocalStructure, SkinStructure;
        internal uint AutoMask, Passes;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NeuralStatus
    {
        internal uint Size, Abi, State, LastResult;
        internal ulong Evaluated, Skipped, Failed;
        internal uint Width, Height, LastFrame, Resets;
        internal double GpuMilliseconds;
        internal ulong TransferBytes;
        internal ulong ModelEvaluations;
    }
    internal sealed class NativeNeural
    {
        internal const uint Abi = 0x00010300;
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)] private static extern IntPtr GetModuleHandleW(string name);
        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true)] private static extern IntPtr GetProcAddress(IntPtr module, string name);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate uint GetAbi();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr GetEvent();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int Read(ref NeuralStatus status);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int Reason([Out] byte[] text, uint size);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr Queue(ref NeuralFrame frame, uint stage);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int DiagnosticOutput([MarshalAs(UnmanagedType.LPWStr)] string folder);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int DiagnosticExposure(IntPtr ticket, IntPtr texture);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int DiagnosticReplay(uint flags);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int DiagnosticInputScale(float scale);
        private DiagnosticInputScale diagnosticInputScale;
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int DiagnosticToneMap(uint mode);
        private DiagnosticToneMap diagnosticToneMap;
        private DiagnosticReplay diagnosticReplay;
        private DiagnosticOutput diagnosticOutput; private DiagnosticExposure diagnosticExposure;
        internal bool DiagnosticsEnabled { get; private set; }
        internal bool PostToneMapDiagnostic { get; private set; }
        private Read read; private Reason reason; private Queue queue;
        internal IntPtr RenderEvent { get; private set; }
        internal NeuralStatus Status;
        internal string Detail = "TG Neural Rendering has not connected to the graphics bridge.";
        private static T Bind<T>(IntPtr module, string symbol) where T : Delegate
        {
            var address = GetProcAddress(module, symbol);
            if (address == IntPtr.Zero) throw new EntryPointNotFoundException(symbol);
            return Marshal.GetDelegateForFunctionPointer<T>(address);
        }
        internal void Poll()
        {
            if (read == null)
            {
                var module = GetModuleHandleW("TGNeuralRendering.Native.dll");
                if (module == IntPtr.Zero) return;
                if (Marshal.SizeOf(typeof(NeuralFrame)) != 104 || Marshal.SizeOf(typeof(NeuralStatus)) != 80 || Bind<GetAbi>(module, "TGNR_GetAbiVersion")() != Abi)
                    throw new InvalidOperationException("TG Neural Rendering native ABI mismatch.");
                queue = Bind<Queue>(module, "TGNR_QueueFrame"); reason = Bind<Reason>(module, "TGNR_GetReason");
                RenderEvent = Bind<GetEvent>(module, "TGNR_GetRenderEvent")(); read = Bind<Read>(module, "TGNR_ReadStatus");
                // Older compatible native builds keep working; diagnostics explicitly require these additions.
                if (GetProcAddress(module, "TGNR_SetDiagnostics") != IntPtr.Zero)
                {
                    diagnosticOutput = Bind<DiagnosticOutput>(module, "TGNR_SetDiagnostics");
                    diagnosticExposure = Bind<DiagnosticExposure>(module, "TGNR_QueueDiagnosticExposure");
                    if (GetProcAddress(module, "TGNR_SetDiagnosticInputScale") != IntPtr.Zero)
                        diagnosticInputScale = Bind<DiagnosticInputScale>(module, "TGNR_SetDiagnosticInputScale");
                    if (GetProcAddress(module, "TGNR_SetDiagnosticToneMap") != IntPtr.Zero)
                        diagnosticToneMap = Bind<DiagnosticToneMap>(module, "TGNR_SetDiagnosticToneMap");
                    if (GetProcAddress(module, "TGNR_SetDiagnosticReplay") != IntPtr.Zero)
                        diagnosticReplay = Bind<DiagnosticReplay>(module, "TGNR_SetDiagnosticReplay");
                }
            }
            var status = new NeuralStatus { Size = 80, Abi = Abi };
            if (read(ref status) != 0) return;
            Status = status;
            var text = new byte[1024]; int length = reason(text, (uint)text.Length);
            Detail = length > 0 ? Encoding.UTF8.GetString(text, 0, Math.Min(length, text.Length - 1)) : "";
        }
        internal void ConfigureDiagnostics(string folder)
        {
            if (PostToneMapDiagnostic) { PostToneMapDiagnostic = false; SceneCapture.Reset(); }
            if (diagnosticOutput == null)
            {
                if (!string.IsNullOrEmpty(folder)) throw new InvalidOperationException("This native NR build lacks stage diagnostics.");
                return;
            }
            DiagnosticsEnabled = false;
            if (diagnosticOutput(folder) != 0) throw new InvalidOperationException("Could not configure NR stage diagnostics.");
            DiagnosticsEnabled = !string.IsNullOrEmpty(folder);
        }
        internal void ConfigurePostToneMap(bool enable)
        {
            if (enable && (!DiagnosticsEnabled || Session.BeforeUpscaling || !UpscalePlacement.ToneMapHookAvailable ||
                GetProcAddress(GetModuleHandleW("TGNeuralRendering.Native.dll"), "TGNR_PostToneMapDiagnosticVersion") == IntPtr.Zero))
                throw new InvalidOperationException("Tone-mapped NR comparison requires current native diagnostics and Quality placement.");
            if (PostToneMapDiagnostic != enable) { PostToneMapDiagnostic = enable; SceneCapture.Reset(); }
        }
        internal void ConfigureToneMap(uint mode)
        {
            if (diagnosticToneMap == null || diagnosticToneMap(mode) != 0)
                throw new InvalidOperationException("Could not configure NR tone-map diagnostic.");
        }
        internal void ConfigureInputScale(float scale)
        {
            if (diagnosticInputScale == null || diagnosticInputScale(scale) != 0)
                throw new InvalidOperationException("Could not configure NR input conditioning.");
        }
        internal void ConfigureReplay(uint flags)
        {
            if (diagnosticReplay == null || diagnosticReplay(flags) != 0)
                throw new InvalidOperationException("Could not configure identical-input NR replay.");
        }
        internal void AttachDiagnosticExposure(IntPtr ticket, IntPtr texture)
        {
            if (diagnosticExposure(ticket, texture) != 0) throw new InvalidOperationException("NR diagnostic frame ticket was not retained.");
        }
        internal IntPtr Submit(ref NeuralFrame frame, uint stage) => queue == null ? IntPtr.Zero : queue(ref frame, stage);
    }
}
