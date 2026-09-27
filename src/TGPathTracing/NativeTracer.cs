// SPDX-License-Identifier: MIT
// TG Path Tracing's native ABI (native/pathtracing/include/tgpt_api.h), mirrored field for field.
using System;
using System.Runtime.InteropServices;
using System.Text;
namespace TGPathTracing
{
    internal static class Tgpt
    {
        internal const uint Abi = 0x00010F00;
        internal const uint FrameReset = 1, FrameGuides = 2;
        internal const uint OptionNoSpatial = 1, OptionNoTemporal = 2, OptionNoDenoiser = 4, OptionNoSpecular = 8, OptionRayReconstruction = 16;
        internal const uint OptionRayRegeneration = 64, OptionNoPassThrough = 128, OptionDiVisibility = 256, OptionSpatialFeedback = 512, OptionNrd = 1024, OptionNoGiBoiling = 32768;
        internal const uint OptionNoRadianceCache = 65536;
        internal const uint OptionGlossyReflections = 262144;
        internal const uint OptionGiVisibility = 2097152; // TGPT_OPTION_GI_VISIBILITY: the Crevice Light row
        internal const uint OptionEmissiveLights = 131072;
        internal const uint OptionNoHdrpShadows = 4194304, LightHdrpShadows = 0x10000;
        internal const uint OptionGiOwnRays = 524288, OptionDiNoReuse = 1048576; // the capture's comparison of what boils
        internal const uint LightNoSpecular = 0x20000;
        internal const uint FeatureDxr12 = 1, FeatureSer = 2, FeatureRayRegeneration = 4, FeatureRayRegenerationActive = 8, FeatureNrd = 16, FeatureNrdActive = 32;
        internal const int EventTrace = 1, EventComposite = 2, EventShadowDetail = 3, EventWater = 4;
        internal const uint MaterialDoubleSided = 1, MaterialAlphaTest = 2, MaterialVertexColor = 4, MaterialAlphaTile = 8, MaterialCameraOnly = 16, MaterialHdrpShaded = 32, MaterialGlass = 64;
        internal const uint MaterialMaskTile = 1024;
        internal const uint MaterialEmissiveAlbedo = 128;
        internal const uint MaterialTranslucent = 256;
        internal const uint MaterialThinCloth = 512;
        internal const uint MaterialStrands = 4096;
        internal const uint MaterialSways = 8192;
        internal const uint InstanceAlphaTest = 1, InstanceHidden = 2, InstanceStandIn = 4, InstanceMoving = 16;
        internal const uint NoStream = 0xFFFFFFFFu;
        internal const uint LightPoint = 0, LightSpot = 1, LightDirectional = 2;
    }
    // TGPT_VIEW_*: what the composite shows instead of the lit image.
    public enum DebugView { None = 0, Albedo = 1, Normals = 2, Coverage = 3, Materials = 4, Indirect = 5, IndirectRaw = 6, Direct = 7, Surfaces = 8, Specular = 9, Emissive = 10 }

    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeSubmesh { internal uint IndexStart, IndexCount, BaseVertex, Topology; }
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeMeshDesc
    {
        internal uint Size, Abi;
        internal ulong Key;
        internal ulong Vb0, Vb1, Vb2, Vb3;
        internal uint Stride0, Stride1, Stride2, Stride3;
        internal uint PositionStream, PositionOffset, PositionFormat, PositionDimension;
        internal uint UvStream, UvOffset, UvFormat, UvDimension;
        internal uint ColorStream, ColorOffset, ColorFormat, ColorDimension;
        internal ulong IndexBuffer;
        internal uint IndexFormat, VertexCount;
        internal uint SubmeshCount, Reserved;
        internal IntPtr Submeshes;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeCpuMeshDesc
    {
        internal uint Size, Abi;
        internal ulong Key;
        internal IntPtr Positions, Uvs, Colors, Indices;
        internal uint VertexCount, IndexCount;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeMaterial
    {
        internal float BaseR, BaseG, BaseB, BaseA;
        internal float ScaleX, ScaleY, OffsetX, OffsetY;
        internal float EmissiveR, EmissiveG, EmissiveB;
        internal float Metallic;
        internal int AlbedoTile, EmissiveTile;
        internal float Smoothness, AlphaCutoff;
        internal uint Flags;
        internal int AlphaTile;             // with MaterialAlphaTile: a RegisterAlphaTexture result (AlphaCutoff then compares its coverage)
        internal int MaskTile;              // with MaterialMaskTile: a RegisterMaskTexture result
        internal uint MaskRemap;            // metallic's low and high end, then smoothness's, 8 bits each, low first
    }
    // TGPT_SkinnedInstance: a character this frame.
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeSkinnedInstance
    {
        internal float T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11; // skinned space to world, row-major 3x4
        internal int Mesh;
        internal uint FirstVertex, VertexCount, SubmeshCount;
        internal uint M0, M1, M2, M3, M4, M5, M6, M7;
        internal void SetMaterial(int i, uint m)
        {
            switch (i) { case 0: M0 = m; break; case 1: M1 = m; break; case 2: M2 = m; break; case 3: M3 = m; break;
                         case 4: M4 = m; break; case 5: M5 = m; break; case 6: M6 = m; break; default: M7 = m; break; }
        }
    }
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeInstance
    {
        internal float M00, M01, M02, M03, M10, M11, M12, M13, M20, M21, M22, M23; // object to world, row-major 3x4
        internal int Mesh;
        internal uint MaterialBase, Flags, Mask;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeLight
    {
        internal float PositionX, PositionY, PositionZ; internal uint Type;
        internal float ColorR, ColorG, ColorB, Range;
        internal float DirectionX, DirectionY, DirectionZ, Radius;
        internal float AngleScale, AngleOffset; internal uint Id, Flags; // Flags: 1 + its cookie, 0 without one
        internal float UpX, UpY, UpZ, CookieScale;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal unsafe struct NativeFrame
    {
        internal uint Size, Abi, Sequence, Flags;
        internal ulong GBuffer0, Normal, GBuffer2, GBuffer3, Depth, Motion, Exposure;
        internal uint RenderWidth, RenderHeight, DebugView;
        internal fixed float InvViewProj[16];
        internal fixed float ViewProj[16];
        internal fixed float PrevViewProj[16];
        internal fixed float CameraPosition[4];
        internal fixed float PrevCameraPosition[4];
        internal fixed float SunDirection[4];
        internal fixed float SunColor[4];
        internal uint FrameIndex, Bounces, SpatialSamples, GiHistory;
        internal uint Options, RaysPerPixel;
        internal float ReflectionBoost;
        internal float TracedGain;
        internal float MaxRayDistance, SkyIntensity, IndirectColor;
        internal uint DiHistory, ThickProfiles;
        internal fixed float View[16];
        internal fixed float NonJitteredProj[16];
        internal fixed float Jitter[4];
        internal fixed float Fog[4];
        internal fixed float Forward[4];
    }
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeStatus
    {
        internal uint Size, Abi;
        internal uint DeviceReady, Supported;
        internal uint MeshesRegistered, MeshesReady, MeshesFailed, MeshesQueued;
        internal uint Instances, InstancesTraced, Materials, Textures;
        internal uint Lights, Disabled;
        internal ulong FramesTraced, FramesFailed;
        internal ulong Triangles, GeometryBytes, AccelerationBytes;
        internal float GpuMs, FrameMs;
        internal int LastError; internal uint RenderWidth, RenderHeight, Composited;
        internal float D3d11Ms, FlushMs, WaitMs, RecordMs;
        internal uint Features, MicromapMeshes, MicromapInstances, MicromapsWaiting;
        internal float CameraLight, CameraRadianceR, CameraRadianceG, CameraRadianceB;
        internal float TracedLog2Mid, TracedLog2High;
        internal uint TracedSamples, PickSurface;
        internal uint PickInstance, PickMaterial; internal float PickDistance; internal uint PickFound;
        internal uint WaterTraced, WaterApplied; // render event 4's frames, both wrapping
    }

    // DLSS Ray Reconstruction's guides (TGPT_Guides).
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeGuides
    {
        internal uint Size, Abi, Width, Height;
        internal ulong DiffuseAlbedo, SpecularAlbedo, NormalRoughness; // ID3D11Resource*
        internal uint FrameIndex, ExtrasFrameIndex;
        internal ulong SpecularHitDistance, ColorBeforeTransparency;   // ID3D11Resource*, written by the composite
    }

    // The native side, bound by GetProcAddress once TG Graphics Bridge has loaded it.
    internal sealed class NativeTracer
    {
        internal const string NativeName = "TGPathTracing.Native.dll";
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate uint GetAbiFn();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int ReadStatusFn(ref NativeStatus status);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int TextFn(byte[] buffer, uint size);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int RegisterMeshFn(ref NativeMeshDesc mesh);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int RegisterCpuMeshFn(ref NativeCpuMeshDesc mesh);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int RegisterSkinnedMeshFn(ref NativeCpuMeshDesc mesh, [In] uint[] submeshStarts, uint submeshCount);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SetSkinnedFn(ulong buffer, uint stride, [In] NativeSkinnedInstance[] instances, uint count);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int RegisterTextureFn(ulong key, ulong texture);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int RegisterAlphaTextureFn(ulong key, ulong texture, float cutoff);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int RegisterMaskTextureFn(ulong key, ulong texture, uint channels);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SetMaterialsFn(uint first, [In] NativeMaterial[] materials, uint count);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SetSceneFn([In] NativeInstance[] instances, uint count, [In] uint[] indices, uint indexCount);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SetLightsFn([In] NativeLight[] lights, uint count);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SetSkyFn(ulong texture);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int ClearFn();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SubmitFrameFn(ref NativeFrame frame);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SetCompositeFn(uint sequence, ulong color);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SetShadowDetailFn(uint request, ulong color, float strength);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SetWaterFn(uint sequence, ulong reflections, ulong depth, ulong normal, float reflectionMultiplier);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SetDiffusionProfilesFn(float[] profiles, uint count);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr GetEventFn();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int GetGuidesFn(ref NativeGuides guides);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)] private static extern IntPtr GetModuleHandleW(string name);
        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true)] private static extern IntPtr GetProcAddress(IntPtr module, string name);

        private ReadStatusFn readStatus; private TextFn unsupportedReason, stageTimes;
        private RegisterMeshFn registerMesh; private RegisterCpuMeshFn registerCpuMesh; private RegisterTextureFn registerTexture, registerCookie;
        private RegisterAlphaTextureFn registerAlphaTexture;
        private RegisterMaskTextureFn registerMaskTexture;
        private RegisterSkinnedMeshFn registerSkinnedMesh; private SetSkinnedFn setSkinned;
        private SetMaterialsFn setMaterials; private SetSceneFn setScene; private SetLightsFn setLights; private SetSkyFn setSky;
        private ClearFn clearScene; private SubmitFrameFn submitFrame; private SetCompositeFn setComposite; private GetGuidesFn getGuides;
        private SetShadowDetailFn setShadowDetail;
        private SetWaterFn setWater;
        private SetDiffusionProfilesFn setDiffusionProfiles, setSkinTransmission;
        internal IntPtr RenderEvent { get; private set; }
        internal bool Connected => readStatus != null && submitFrame != null && RenderEvent != IntPtr.Zero;
        internal string Detail { get; private set; } = NativeName + " is not loaded. It starts through TG Graphics Bridge: check BepInEx/TGGraphicsBridge/Autoload.log " +
            "and BepInEx/TGPathTracing/graphics-bridge-extension.json; start the game normally through Steam.";

        internal static unsafe void Validate()
        {
            if (Marshal.SizeOf(typeof(NativeSubmesh)) != 16 || Marshal.SizeOf(typeof(NativeMeshDesc)) != 144 || Marshal.SizeOf(typeof(NativeCpuMeshDesc)) != 56 ||
                Marshal.SizeOf(typeof(NativeMaterial)) != 80 || Marshal.SizeOf(typeof(NativeInstance)) != 64 || Marshal.SizeOf(typeof(NativeLight)) != 80 ||
                sizeof(NativeFrame) != 568 || Marshal.SizeOf(typeof(NativeStatus)) != 208 || Marshal.SizeOf(typeof(NativeGuides)) != 64 ||
                Marshal.SizeOf(typeof(NativeSkinnedInstance)) != 96)
                throw new InvalidOperationException("TG Path Tracing native ABI packing mismatch.");
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
                uint abi = Bind<GetAbiFn>(module, "TGPT_GetAbiVersion")();
                if (abi != Tgpt.Abi) { Detail = "Native ABI 0x" + abi.ToString("X8") + " does not match this plugin's 0x" + Tgpt.Abi.ToString("X8") + ": reinstall TG Path Tracing."; return false; }
                unsupportedReason = Bind<TextFn>(module, "TGPT_GetUnsupportedReason");
                stageTimes = Bind<TextFn>(module, "TGPT_GetStageTimes");
                registerMesh = Bind<RegisterMeshFn>(module, "TGPT_RegisterMesh");
                registerCpuMesh = Bind<RegisterCpuMeshFn>(module, "TGPT_RegisterCpuMesh");
                registerSkinnedMesh = Bind<RegisterSkinnedMeshFn>(module, "TGPT_RegisterSkinnedMesh");
                setSkinned = Bind<SetSkinnedFn>(module, "TGPT_SetSkinned");
                registerTexture = Bind<RegisterTextureFn>(module, "TGPT_RegisterTexture");
                registerAlphaTexture = Bind<RegisterAlphaTextureFn>(module, "TGPT_RegisterAlphaTexture");
                registerMaskTexture = Bind<RegisterMaskTextureFn>(module, "TGPT_RegisterMaskTexture");
                registerCookie = Bind<RegisterTextureFn>(module, "TGPT_RegisterCookie");
                setMaterials = Bind<SetMaterialsFn>(module, "TGPT_SetMaterials");
                setScene = Bind<SetSceneFn>(module, "TGPT_SetScene");
                setLights = Bind<SetLightsFn>(module, "TGPT_SetLights");
                setSky = Bind<SetSkyFn>(module, "TGPT_SetSky");
                clearScene = Bind<ClearFn>(module, "TGPT_ClearScene");
                setComposite = Bind<SetCompositeFn>(module, "TGPT_SetComposite");
                setShadowDetail = Bind<SetShadowDetailFn>(module, "TGPT_SetShadowDetail");
                setWater = Bind<SetWaterFn>(module, "TGPT_SetWater");
                setDiffusionProfiles = Bind<SetDiffusionProfilesFn>(module, "TGPT_SetDiffusionProfiles");
                setSkinTransmission = Bind<SetDiffusionProfilesFn>(module, "TGPT_SetSkinTransmission");
                getGuides = Bind<GetGuidesFn>(module, "TGPT_GetGuides");
                var submit = Bind<SubmitFrameFn>(module, "TGPT_SubmitFrame");
                var read = Bind<ReadStatusFn>(module, "TGPT_ReadStatus");
                RenderEvent = Bind<GetEventFn>(module, "TGPT_GetRenderEvent")();
                if (RenderEvent == IntPtr.Zero) throw new InvalidOperationException("No render event callback.");
                submitFrame = submit; readStatus = read;
                Detail = "Bound " + NativeName + ", TG Graphics Bridge's path tracing extension, ABI 1.3.";
                return true;
            }
            catch (Exception ex) { readStatus = null; submitFrame = null; Detail = "Native binding failed: " + ex.Message; return false; }
        }
        internal bool ReadStatus(out NativeStatus status)
        {
            status = new NativeStatus { Size = 208, Abi = Tgpt.Abi };
            return Connected && readStatus(ref status) == 0;
        }
        internal string UnsupportedReason() => Text(unsupportedReason);
        // What each stage of a frame costs the GPU, averaged over about the last second (the F8 report).
        internal string StageTimes() => Text(stageTimes);
        private static string Text(TextFn read)
        {
            if (read == null) return "";
            var buffer = new byte[1024];
            int length = read(buffer, (uint)buffer.Length);
            return length > 0 ? Encoding.UTF8.GetString(buffer, 0, Math.Min(length, buffer.Length - 1)) : "";
        }
        internal int RegisterMesh(ref NativeMeshDesc mesh) => Connected ? registerMesh(ref mesh) : -100;
        internal int RegisterCpuMesh(ref NativeCpuMeshDesc mesh) => Connected ? registerCpuMesh(ref mesh) : -100;
        // A character's triangles (indices into its own vertices; submeshes as consecutive ranges).
        // uvs: two floats a vertex (their textures: colors and alpha tests), or null.
        internal int RegisterSkinnedMesh(long key, uint[] indices, uint vertexCount, uint[] submeshStarts, float[] uvs)
        {
            if (!Connected) return -100;
            if (uvs != null && uvs.Length != vertexCount * 2) uvs = null;
            var handle = GCHandle.Alloc(indices, GCHandleType.Pinned);
            var uvHandle = uvs != null ? GCHandle.Alloc(uvs, GCHandleType.Pinned) : default;
            try
            {
                var d = new NativeCpuMeshDesc { Size = (uint)Marshal.SizeOf(typeof(NativeCpuMeshDesc)), Abi = Tgpt.Abi, Key = (ulong)key,
                    Indices = handle.AddrOfPinnedObject(), Uvs = uvs != null ? uvHandle.AddrOfPinnedObject() : IntPtr.Zero,
                    VertexCount = vertexCount, IndexCount = (uint)indices.Length };
                return registerSkinnedMesh(ref d, submeshStarts, (uint)submeshStarts.Length);
            }
            finally { handle.Free(); if (uvs != null) uvHandle.Free(); }
        }
        internal int SetSkinned(IntPtr buffer, uint stride, NativeSkinnedInstance[] instances, uint count) =>
            Connected ? setSkinned(unchecked((ulong)buffer.ToInt64()), stride, instances ?? new NativeSkinnedInstance[0], count) : -100;
        internal int RegisterTexture(ulong key, IntPtr texture) => Connected ? registerTexture(key, unchecked((ulong)texture.ToInt64())) : -100;
        internal int RegisterAlphaTexture(ulong key, IntPtr texture, float cutoff) => Connected ? registerAlphaTexture(key, unchecked((ulong)texture.ToInt64()), cutoff) : -100;
        // channels: the texture's channel (0 red to 3 alpha) holding metallic, and smoothness's two bits above it.
        internal int RegisterMaskTexture(ulong key, IntPtr texture, uint channels) => Connected ? registerMaskTexture(key, unchecked((ulong)texture.ToInt64()), channels) : -100;
        internal int RegisterCookie(ulong key, IntPtr texture) => Connected ? registerCookie(key, unchecked((ulong)texture.ToInt64())) : -100;
        internal int SetMaterials(uint first, NativeMaterial[] materials, uint count) => Connected ? setMaterials(first, materials, count) : -100;
        internal int SetScene(NativeInstance[] instances, uint count, uint[] indices, uint indexCount) => Connected ? setScene(instances, count, indices, indexCount) : -100;
        internal int SetLights(NativeLight[] lights, uint count) => Connected ? setLights(lights, count) : -100;
        internal int SetSky(IntPtr texture) => Connected ? setSky(unchecked((ulong)texture.ToInt64())) : -100;
        internal int ClearScene() => Connected ? clearScene() : -100;
        internal int SubmitFrame(ref NativeFrame frame) => Connected ? submitFrame(ref frame) : -100;
        internal int SetComposite(uint sequence, IntPtr color) => Connected ? setComposite(sequence, unchecked((ulong)color.ToInt64())) : -100;
        internal int SetShadowDetail(uint request, IntPtr color, float strength) =>
            Connected && setShadowDetail != null ? setShadowDetail(request, unchecked((ulong)color.ToInt64()), strength) : -100;
        // Traced reflections on water for render event 4: HDRP's transparent SSR texture, its depth-stencil and normal buffers
        // at that step, and its reflection lighting multiplier for the frame.
        internal int SetWater(uint sequence, IntPtr reflections, IntPtr depth, IntPtr normal, float reflectionMultiplier) =>
            Connected && setWater != null ? setWater(sequence, unchecked((ulong)reflections.ToInt64()), unchecked((ulong)depth.ToInt64()),
                unchecked((ulong)normal.ToInt64()), reflectionMultiplier) : -100;
        // Per diffusion profile: Burley's shape per channel (per metre) and the filter radius in metres (pt_sss.hlsl).
        internal int SetDiffusionProfiles(float[] profiles, uint count) =>
            Connected && setDiffusionProfiles != null ? setDiffusionProfiles(profiles, count) : -100;
        // Per diffusion profile: HDRP's transmission tint / 4, thickness remap start (mm), shape per mm, remap span (mm).
        internal int SetSkinTransmission(float[] profiles, uint count) =>
            Connected && setSkinTransmission != null ? setSkinTransmission(profiles, count) : -100;
        internal bool ReadGuides(out NativeGuides guides)
        {
            guides = new NativeGuides { Size = 64, Abi = Tgpt.Abi };
            return Connected && getGuides != null && getGuides(ref guides) == 0;
        }
    }
}
