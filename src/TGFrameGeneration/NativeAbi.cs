// SPDX-License-Identifier: MIT
using System;
using System.Runtime.InteropServices;
namespace TGFrameGeneration
{
    [Flags] internal enum Suspend : uint
    {
        None=0, Unfocused=1, CursorUnlocked=2, TimePaused=4, VideoMenu=8,
        Stopping=16, ResolutionChanged=32, NoGameCamera=64
    }
    internal enum Reason : uint
    {
        Ready=0, NoClient=1, WaitingBackend=2, UnsupportedAPI=3, Suspended=4, UserOff=5,
        CapsUnknown=6, MultiplierUnsupported=7, DynamicUnsupported=8, UnsupportedFormat=9,
        NativeError=10, OptionsAccepted=11, InputDispatched=12, ClientTimeout=13,
        MissingRenderEvent=14, NoDlssInputs=15, StaleFrame=16, MultipleDlssViews=17,
        UnsupportedQueue=18, InputContractInvalid=19, ExistingStreamline=20,
        RuntimeNotSupported=21, CameraInvalid=22, PresentFailed=23, BridgeNotCreated=24, InteropFailure=25, UnsupportedColorSpace=26,
        SourceDeviceMismatch=27, UnsupportedSourceLayout=28, BridgeReady=29, VsyncUnsupported=30
    }
    [StructLayout(LayoutKind.Sequential, Pack=4)]
    internal struct NativeRequest
    {
        internal uint Size, Abi;
        internal ulong Sequence;
        internal uint Mode, FixedMultiplier;
        internal float DynamicTargetFps, ReflexBaseFpsLimit;
        internal uint SuspensionMask, SourceGraphicsApi;
        internal uint ReflexMode; // 0 Off, 1 On, 2 On + Boost; native forces at least On while FG is active
    }
    [StructLayout(LayoutKind.Sequential, Pack=4)]
    internal struct NativeStatus
    {
        internal uint Size, Abi;
        internal ulong RequestedSequence, AppliedSequence, SuccessfulInputDispatches;
        internal uint RequestedMode, LastAcceptedMode, MaxGeneratedFrames, DynamicSupported, ReasonValue;
        internal int LastSetOptionsResult, LastGetStateResult;
        internal uint DlssgStatus, SwapchainFormat, Flags, PresentedSinceQuery, RequestAgeMs;
        internal float RequestedDynamicTargetFps, RequestedBaseFpsLimit;
        internal uint SourceApi, PresentationApi, BridgeFlags;
        internal int LastInteropHresult;
        internal ulong SourcePresents, OutputFramesReported, CopiedFrames, CapturedNgxCalls;
        internal uint SourceFormat, OutputColorSpace, NativeLoadPid, CompletionFenceUsed;
        internal Reason Reason => (Reason)ReasonValue;
        internal bool CapsKnown => (Flags & 4) != 0;
        internal bool RuntimeQueried => (Flags & 8) != 0;
        internal string DescribeBrief()
        {
            string cap = CapsKnown ? "max " + (MaxGeneratedFrames + 1) + "x; Dynamic " + (DynamicSupported != 0 ? "yes" : "no") : "caps unknown";
            return Reason + " | requested/accepted " + (GenerationMode)RequestedMode + "/" + (GenerationMode)LastAcceptedMode +
                "\nSource DX" + SourceApi + " -> output DX" + PresentationApi + " | " + cap +
                "\nCopies " + CopiedFrames + " | input pairs " + SuccessfulInputDispatches +
                " | interop 0x" + LastInteropHresult.ToString("X8") + " | DLSS-G 0x" + DlssgStatus.ToString("X");
        }
        internal string Describe()
        {
            string cap = CapsKnown ? "max " + (MaxGeneratedFrames + 1) + "x; Dynamic " + (DynamicSupported != 0 ? "yes" : "no") : "caps unknown";
            return Reason + " | mode " + ((GenerationMode)RequestedMode) + "/" + ((GenerationMode)LastAcceptedMode) + " | requested/accepted " + RequestedSequence + "/" + AppliedSequence +
                " | " + cap + " | native errors " + LastSetOptionsResult + "/" + LastGetStateResult +
                " | DLSS-G status 0x" + DlssgStatus.ToString("X") + " | format " + SwapchainFormat +
                " | captured input pairs " + SuccessfulInputDispatches +
                " | source DX" + SourceApi + " -> presentation DX" + PresentationApi +
                " | bridge 0x" + BridgeFlags.ToString("X") + " | interop HRESULT 0x" + LastInteropHresult.ToString("X8") +
                " | source presents/copies " + SourcePresents + "/" + CopiedFrames +
                " | NVIDIA-reported output frames " + OutputFramesReported + " (not a display measurement)" +
                " | NGX calls " + CapturedNgxCalls + " | source/output format " + SourceFormat + "/" + SwapchainFormat +
                " | output color space " + OutputColorSpace + " | native PID " + NativeLoadPid +
                " | NVIDIA completion fence " + (CompletionFenceUsed != 0 ? "used" : "not supplied");
        }
    }
    [StructLayout(LayoutKind.Sequential, Pack=4)]
    internal unsafe struct NativeCamera
    {
        internal uint Size, Abi, FrameId, ResetHistory, Width, Height, CameraId, Reserved;
        internal fixed float ViewToClip[16];
        internal fixed float ClipToView[16];
        internal fixed float ClipToPrev[16];
        internal fixed float PrevToClip[16];
        internal fixed float Position[3];
        internal fixed float Up[3];
        internal fixed float Right[3];
        internal fixed float Forward[3];
        internal float Near, Far, FovRadians, Aspect;
    }
    internal static class Abi
    {
        internal const uint Version = 0x00040000;
        internal const int RequestSize = 44;
        internal static void Validate()
        {
            if (Marshal.SizeOf(typeof(NativeRequest)) != RequestSize || Marshal.SizeOf(typeof(NativeStatus)) != 152 || Marshal.SizeOf(typeof(NativeCamera)) != 352)
                throw new InvalidOperationException("Native ABI packing mismatch.");
        }
    }
}
