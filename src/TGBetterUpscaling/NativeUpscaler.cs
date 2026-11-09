// SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
namespace TGBetterUpscaling
{
    // One upscaling request (TGUP_Frame in tgup_api.h).
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal unsafe struct UpscaleFrame
    {
        internal uint Size, Abi, Sequence, Backend;
        internal ulong Color, Depth, Motion, Output; // ID3D11Resource* of HDRP's render textures
        internal uint RenderWidth, RenderHeight, OutputWidth, OutputHeight;
        internal float JitterX, JitterY;             // HDCamera.taaJitter.xy, render pixels
        internal float MotionScaleX, MotionScaleY;   // HDRP motion vectors to render pixels
        internal float CameraNear, CameraFar, FovY, FrameTimeMs;
        internal float Sharpness, PreExposure;       // FSR's own sharpening 0..1
        internal uint Flags;
        internal uint Mode;                          // XeSS: the chosen xess_quality_settings_t; Ray Reconstruction: NGX's quality level; otherwise 0
        internal ulong DiffuseAlbedo, SpecularAlbedo, NormalRoughness; // Ray Reconstruction's guides (TG Path Tracing's textures)
        // Ray Reconstruction's camera: row-major, row vectors multiplied on the left (NVIDIA's convention).
        internal fixed float WorldToView[16];
        internal fixed float ViewToClip[16];
        internal ulong SpecularHitDistance, ColorBeforeTransparency; // Ray Reconstruction's optional inputs (TG Path Tracing's)
    }
    // One quality mode (TGUP_Mode): the vendor's id and its per-axis upscale ratio.
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeMode
    {
        internal uint Id;
        internal float Ratio;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct UpscaleStatus
    {
        internal uint Size, Abi;
        internal uint Available, Disabled;           // bit (1 << native backend)
        internal uint FsrVersionCount, XessVersion;
        internal uint ActiveBackend, ActiveFsrVersion, XessPreset;
        internal uint RenderWidth, RenderHeight, OutputWidth, OutputHeight;
        internal uint GpuVendor, DeviceReady;
        internal int LastResult;
        internal ulong FramesUpscaled, FramesFailed, FramesFallback;
        internal bool Has(uint backend) => (Available & (1u << (int)backend)) != 0;
        internal bool Usable(uint backend) => DeviceReady != 0 && Has(backend) && (Disabled & (1u << (int)backend)) == 0;
        internal string XessVersionText => XessVersion == 0 ? "?" : (XessVersion >> 16) + "." + ((XessVersion >> 8) & 0xFF) + "." + (XessVersion & 0xFF);
    }
    // The DLSS overrides in the NVIDIA driver's profile for the game (TGUP_DlssOverride).
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct DlssOverride
    {
        internal uint Known, PresetOverride, OverrideEnabled, ModeOverride, DlaaOverride, ScalingRatio;
    }
    internal static class NativeFlags
    {
        internal const uint Reset = 1, DepthInverted = 2, FsrAutoExposure = 4, FsrDebugCheck = 8, FsrDebugView = 16;
        internal const uint BackendFsr = 1, BackendXess = 2, BackendRayReconstruction = 3;
        internal const int EventPrepareRayReconstruction = 3; // TGUP_EVENT_PREPARE_RAY_RECONSTRUCTION
    }
    internal sealed class NativeUpscaler
    {
        internal const uint Abi = 0x00010400;
        internal const int FrameSize = 280, StatusSize = 88, ModeSize = 8, DlssOverrideSize = 24;
        internal const string NativeName = "TGBetterUpscaling.Native.dll";
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate uint GetAbi();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int Read(ref UpscaleStatus status);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int VersionName(uint index, byte[] buffer, uint size);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int Submit(ref UpscaleFrame frame);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int Output(uint sequence, ulong output);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr GetEvent();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int GetModesFn(uint backend, [Out] NativeMode[] modes, uint capacity);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int GetDlssDllFn(out ulong loaded, out ulong requested, out ulong bundled);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int GetDlssOverrideFn(out DlssOverride dlssOverride);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int GetRrOptimalSettingsFn(uint width, uint height, uint mode, out uint renderWidth, out uint renderHeight);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int GetDlssSizeFn(out uint created, out uint render, out uint creations);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
        private static extern IntPtr GetModuleHandleW(string name);
        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true)]
        private static extern IntPtr GetProcAddress(IntPtr module, string name);
        private Read read;
        private VersionName versionName;
        private Submit submit;
        private Output setOutput;
        private GetModesFn getModes;
        private GetDlssDllFn getDlssDll;
        private GetDlssOverrideFn getDlssOverride;
        private GetRrOptimalSettingsFn getRrOptimalSettings;
        private GetDlssSizeFn getDlssSize;
        internal IntPtr RenderEvent { get; private set; }
        internal bool Connected => read != null && submit != null && RenderEvent != IntPtr.Zero;
        internal string Detail { get; private set; } = NativeName + " is not loaded. It starts through TG Graphics Bridge: check BepInEx/TGGraphicsBridge/Autoload.log " +
            "and BepInEx/TGBetterUpscaling/graphics-bridge-extension.json; start the game normally through Steam.";
        internal static void Validate()
        {
            if (Marshal.SizeOf(typeof(UpscaleFrame)) != FrameSize || Marshal.SizeOf(typeof(UpscaleStatus)) != StatusSize || Marshal.SizeOf(typeof(NativeMode)) != ModeSize ||
                Marshal.SizeOf(typeof(DlssOverride)) != DlssOverrideSize)
                throw new InvalidOperationException("TG Better Upscaling native ABI packing mismatch.");
        }
        private static T Bind<T>(IntPtr module, string symbol) where T : Delegate
        {
            IntPtr address = GetProcAddress(module, symbol);
            if (address == IntPtr.Zero) throw new EntryPointNotFoundException(symbol);
            return Marshal.GetDelegateForFunctionPointer<T>(address);
        }
        internal bool Discover()
        {
            if (Connected) return true;
            try
            {
                var module = GetModuleHandleW(NativeName);
                if (module == IntPtr.Zero) return false;
                uint abi = Bind<GetAbi>(module, "TGUP_GetAbiVersion")();
                if (abi != Abi) { Detail = "Native ABI 0x" + abi.ToString("X8") + " does not match this plugin's 0x" + Abi.ToString("X8") + ": reinstall TG Better Upscaling."; return false; }
                versionName = Bind<VersionName>(module, "TGUP_GetFsrVersionName");
                setOutput = Bind<Output>(module, "TGUP_SetOutput");
                getModes = Bind<GetModesFn>(module, "TGUP_GetModes");
                getDlssDll = Bind<GetDlssDllFn>(module, "TGUP_GetDlssDll");
                getDlssOverride = Bind<GetDlssOverrideFn>(module, "TGUP_GetDlssOverride");
                // Additive API: an older native DLL keeps SR/FSR/XeSS, but cannot host RR at unverified dimensions.
                if (GetProcAddress(module, "TGUP_GetRrOptimalSettings") != IntPtr.Zero)
                    getRrOptimalSettings = Bind<GetRrOptimalSettingsFn>(module, "TGUP_GetRrOptimalSettings");
                if (GetProcAddress(module, "TGUP_GetDlssSize") != IntPtr.Zero)
                    getDlssSize = Bind<GetDlssSizeFn>(module, "TGUP_GetDlssSize");
                var newSubmit = Bind<Submit>(module, "TGUP_SubmitFrame");
                var newRead = Bind<Read>(module, "TGUP_ReadStatus");
                RenderEvent = Bind<GetEvent>(module, "TGUP_GetRenderEvent")();
                if (RenderEvent == IntPtr.Zero) throw new InvalidOperationException("No render event callback.");
                submit = newSubmit; read = newRead;
                Detail = "Bound " + NativeName + ", TG Graphics Bridge's upscaling extension, ABI 1.2.";
                return true;
            }
            catch (Exception ex) { read = null; submit = null; Detail = "Native binding failed: " + ex.Message; return false; }
        }
        internal bool ReadStatus(out UpscaleStatus status)
        {
            status = new UpscaleStatus { Size = StatusSize, Abi = Abi };
            return Connected && read(ref status) == 0;
        }
        internal int SubmitFrame(ref UpscaleFrame frame) => Connected ? submit(ref frame) : -100;
        internal bool RrOptimalSettings(uint width, uint height, uint mode, out uint renderWidth, out uint renderHeight)
        {
            renderWidth = renderHeight = 0;
            return getRrOptimalSettings != null && getRrOptimalSettings(width, height, mode, out renderWidth, out renderHeight) == 0;
        }
        // Whether the game's DLSS feature was created larger than it renders; see TGUP_GetDlssSize.
        internal bool DlssOversized(out uint created, out uint render, out uint creations)
        {
            created = render = creations = 0;
            return getDlssSize != null && getDlssSize(out created, out render, out creations) == 1;
        }
        internal int SetOutput(uint sequence, IntPtr output) => Connected ? setOutput(sequence, unchecked((ulong)output.ToInt64())) : -100;
        internal const int DlssNone = 0, DlssBundled = 1, DlssKept = 2;
        // The DLSS DLL NVIDIA's runtime loaded for the game's DLSS: DlssNone, DlssBundled or DlssKept, with
        // the file versions (16 bits per part) of the loaded, the requested and the bundled DLL.
        internal int DlssDll(out ulong loaded, out ulong requested, out ulong bundled)
        {
            loaded = requested = bundled = 0;
            return getDlssDll != null ? getDlssDll(out loaded, out requested, out bundled) : DlssNone;
        }
        // The first call starts reading the NVIDIA driver's profile for the game.
        internal bool TryDlssOverride(out DlssOverride dlssOverride)
        {
            dlssOverride = default(DlssOverride);
            return getDlssOverride != null && getDlssOverride(out dlssOverride) == 0 && dlssOverride.Known != 0;
        }
        internal static string VersionText(ulong v) => (v >> 48) + "." + ((v >> 32) & 0xFFFF) + "." + ((v >> 16) & 0xFFFF) + "." + (v & 0xFFFF);
        // The quality modes AMD's or Intel's runtime reports for FSR or XeSS, highest render scale first.
        internal QualityMode[] Modes(uint backend)
        {
            if (getModes == null) return new QualityMode[0];
            var modes = new NativeMode[16];
            int count = getModes(backend, modes, (uint)modes.Length);
            var result = new List<QualityMode>();
            for (int i = 0; i < count && i < modes.Length; i++)
            {
                if (!(modes[i].Ratio >= 1f) || modes[i].Ratio > 8f) continue;
                int id = (int)modes[i].Id;
                string name = backend == NativeFlags.BackendFsr ? QualityModes.FsrName(id) : QualityModes.XessName(id);
                result.Add(new QualityMode(id, name, 100f / modes[i].Ratio));
            }
            return QualityModes.Sorted(result);
        }
        // The FSR versions AMD's runtime offers on this GPU, in the order it reports them.
        internal List<string> FsrVersions(uint count)
        {
            var names = new List<string>();
            if (versionName == null) return names;
            var buffer = new byte[128];
            for (uint i = 0; i < count && i < 32; i++)
            {
                int length = versionName(i, buffer, (uint)buffer.Length);
                names.Add(length > 0 ? Encoding.UTF8.GetString(buffer, 0, length) : "#" + i);
            }
            return names;
        }
    }
}
