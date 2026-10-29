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
        internal uint AutoMask;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NeuralStatus
    {
        internal uint Size, Abi, State, LastResult;
        internal ulong Evaluated, Skipped, Failed;
        internal uint Width, Height, LastFrame, Resets;
        internal double GpuMilliseconds;
        internal ulong TransferBytes;
    }
    internal sealed class NativeNeural
    {
        internal const uint Abi = 0x00010100;
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)] private static extern IntPtr GetModuleHandleW(string name);
        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true)] private static extern IntPtr GetProcAddress(IntPtr module, string name);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate uint GetAbi();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr GetEvent();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int Read(ref NeuralStatus status);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int Reason([Out] byte[] text, uint size);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr Queue(ref NeuralFrame frame, uint stage);
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
                if (Marshal.SizeOf(typeof(NeuralFrame)) != 100 || Marshal.SizeOf(typeof(NeuralStatus)) != 72 || Bind<GetAbi>(module, "TGNR_GetAbiVersion")() != Abi)
                    throw new InvalidOperationException("TG Neural Rendering native ABI mismatch.");
                queue = Bind<Queue>(module, "TGNR_QueueFrame"); reason = Bind<Reason>(module, "TGNR_GetReason");
                RenderEvent = Bind<GetEvent>(module, "TGNR_GetRenderEvent")(); read = Bind<Read>(module, "TGNR_ReadStatus");
            }
            var status = new NeuralStatus { Size = 72, Abi = Abi };
            if (read(ref status) != 0) return;
            Status = status;
            var text = new byte[1024]; int length = reason(text, (uint)text.Length);
            Detail = length > 0 ? Encoding.UTF8.GetString(text, 0, Math.Min(length, text.Length - 1)) : "";
        }
        internal IntPtr Submit(ref NeuralFrame frame, uint stage) => queue == null ? IntPtr.Zero : queue(ref frame, stage);
    }
}
