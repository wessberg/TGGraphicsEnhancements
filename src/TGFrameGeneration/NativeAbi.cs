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
        SourceDeviceMismatch=27, UnsupportedSourceLayout=28, BridgeReady=29, VsyncUnsupported=30,
        BackendUnavailable=31, BackendFailed=32
    }
    [StructLayout(LayoutKind.Sequential, Pack=4)]
    internal struct NativeRequest
    {
        internal uint Size, Abi;
        internal ulong Sequence;
        internal uint Mode, FixedMultiplier; // multiplier: total, clamped natively to the backend's maximum
        internal float DynamicTargetFps, BaseFpsLimit;
        internal uint SuspensionMask, SourceGraphicsApi;
        internal uint LatencyMode; // LowLatencySetting; native enforces the pairing each technology requires
        internal uint Backend; // FrameGenerationSetting: 0 Off, 1 DLSS, 2 FSR, 3 XeSS
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
        internal uint BackendsAvailable, FsrMaxGenerated, XessMaxGenerated; // bit 1<<backend; maxima reported by each runtime
        internal uint LatencyAvailable; // bit 1<<LowLatencySetting usable on its own
        internal uint ActiveBackend, ActiveLatency, GpuVendor, BackendFailedMask;
        internal int BackendResult;
        internal Reason Reason => (Reason)ReasonValue;
        internal bool Has(FrameGenerationSetting backend) => backend != FrameGenerationSetting.Off && (BackendsAvailable & (1u << (int)backend)) != 0;
        // Generated frames per rendered frame the runtime reports for a backend (0 when unavailable).
        internal uint MaxGenerated(FrameGenerationSetting backend) =>
            !Has(backend) ? 0 : backend == FrameGenerationSetting.DLSS ? MaxGeneratedFrames :
            backend == FrameGenerationSetting.FSR ? FsrMaxGenerated : XessMaxGenerated;
        internal bool Supports(LowLatencySetting latency) => (LatencyAvailable & (1u << (int)latency)) != 0;
        private static readonly string[] OutputNames = { "standard", "?", "FSR", "XeSS" };
        private string Output => ActiveBackend < OutputNames.Length ? OutputNames[ActiveBackend] : "?";
        internal bool CapsKnown => (Flags & 4) != 0;
        internal bool RuntimeQueried => (Flags & 8) != 0;
        internal string DescribeBrief()
        {
            string cap = CapsKnown ? "max " + (MaxGeneratedFrames + 1) + "x; Dynamic " + (DynamicSupported != 0 ? "yes" : "no") : "caps unknown";
            return Reason + " | requested/accepted " + (GenerationMode)RequestedMode + "/" + (GenerationMode)LastAcceptedMode +
                " | output " + Output + ", latency " + (LowLatencySetting)ActiveLatency +
                "\nSource DX" + SourceApi + " -> output DX" + PresentationApi + " | " + cap +
                "\nCopies " + CopiedFrames + " | input pairs " + SuccessfulInputDispatches +
                " | interop 0x" + LastInteropHresult.ToString("X8") + " | DLSS-G 0x" + DlssgStatus.ToString("X");
        }
        internal string Describe()
        {
            string cap = CapsKnown ? "DLSS " + (Has(FrameGenerationSetting.DLSS) ? "max " + (MaxGeneratedFrames + 1) + "x, Dynamic " + (DynamicSupported != 0 ? "yes" : "no") : "no") +
                "; FSR " + (Has(FrameGenerationSetting.FSR) ? "max " + (FsrMaxGenerated + 1) + "x" : "no") +
                "; XeSS " + (Has(FrameGenerationSetting.XeSS) ? "max " + (XessMaxGenerated + 1) + "x" : "no") +
                "; latency mask 0x" + LatencyAvailable.ToString("X") + "; GPU vendor 0x" + GpuVendor.ToString("X4") : "caps unknown";
            return Reason + " | mode " + ((GenerationMode)RequestedMode) + "/" + ((GenerationMode)LastAcceptedMode) + " | requested/accepted " + RequestedSequence + "/" + AppliedSequence +
                " | output " + Output + ", latency " + (LowLatencySetting)ActiveLatency +
                (BackendFailedMask != 0 ? " | failed backends 0x" + BackendFailedMask.ToString("X") + " (last error " + BackendResult + ")" : "") +
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
        internal fixed float WorldToView[16];
    }
    // HDRP's own depth and motion vectors for a frame (see HdrpInputs and TGFG_HdrpInputs).
    [StructLayout(LayoutKind.Sequential, Pack=4)]
    internal struct NativeHdrpInputs
    {
        internal uint Size, Abi, FrameId, Flags; // Flags: 1 = reversed-Z depth
        internal ulong Depth, Motion;            // ID3D11Resource* of the camera's render textures
        internal uint RenderWidth, RenderHeight;
        internal float JitterX, JitterY;         // HDCamera.taaJitter.xy, render pixels
    }
    internal static class Abi
    {
        internal const uint Version = 0x00050000;
        internal const int RequestSize = 48, StatusSize = 188, CameraSize = 416, HdrpInputsSize = 48;
        internal static void Validate()
        {
            if (Marshal.SizeOf(typeof(NativeRequest)) != RequestSize || Marshal.SizeOf(typeof(NativeStatus)) != StatusSize ||
                Marshal.SizeOf(typeof(NativeCamera)) != CameraSize || Marshal.SizeOf(typeof(NativeHdrpInputs)) != HdrpInputsSize)
                throw new InvalidOperationException("Native ABI packing mismatch.");
        }
    }
}
