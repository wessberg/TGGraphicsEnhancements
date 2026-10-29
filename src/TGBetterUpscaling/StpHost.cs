// SPDX-License-Identifier: MIT
// FSR and XeSS run where HDRP would run STP, the game's own upscaler. For them the game's upscaling
// controller configures the camera exactly as for STP: dynamic resolution on, DLSS off, the render
// scale of the chosen quality mode (or the game's slider, when the runtime reports no modes). HDRP
// then renders at that scale, jitters the
// projection, and calls DoStpPasses with the frame's color, depth and motion vectors at render
// resolution; that call is where FSR or XeSS takes over, and returns the upscaled image in the same
// output texture STP would have written, so everything after it (post-processing, frame generation,
// RenoDX, ReShade) sees an upscaled frame the same way.
//
// The textures behind HDRP's render-graph handles only exist while the graph executes, so the frame
// is handed to the native side from a render-graph pass. HDRP's CustomPass.ExecuteInternal records
// such a pass for a pass object and a set of targets; this mod's pass declares the output texture as
// its target (which also keeps the pass from being culled), reads color, depth and motion, and issues
// the native render event that copies them to D3D12, upscales, and writes the result back.
//
// DLSS Ray Reconstruction takes STP's pass the same way, in place of HDRP's own DLSS, while TG Path
// Tracing traces and DLSS is the upscaler: it denoises the path-traced frame and upscales it in one step,
// on the game's D3D11 device, at the chosen DLSS mode's render scale, with TG Path Tracing's guides.
//
// The camera's jitter stays HDRP's: for STP it is a Halton(2,3) sequence over HDRP's 1024-frame index
// (STP.Jit16, despite its name), longer than the 8 x (output / render width)^2 samples AMD and Intel
// ask as a minimum, and STP itself derives the jitter it removes from that index.
using System;
using Awaken.TG.Main.Settings;
using Awaken.TG.Main.Settings.Controllers;
using Awaken.TG.Main.Settings.Graphics;
using HarmonyLib;
using TGGraphicsBridge;
using TGGraphicsBridge.Internal;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.HighDefinition;
using UnityEngine.Rendering.RenderGraphModule;
#if BEPINEX_IL2CPP
using Il2CppInterop.Runtime;
using Il2CppInterop.Runtime.Injection;
#else
using System.Reflection;
#endif
namespace TGBetterUpscaling
{
    internal sealed class UpscalePass : CustomPass
    {
#if BEPINEX_IL2CPP
        public UpscalePass(IntPtr pointer) : base(pointer) { }
        internal UpscalePass() : base(ClassInjector.DerivedConstructorPointer<UpscalePass>())
        {
            ClassInjector.DerivedConstructorBody(this);
            Il2CppTypes.Construct(this, typeof(CustomPass), "NativeMethodInfoPtr__ctor_Protected_Void_0");
        }
        private new void Execute(CustomPassContext ctx) => StpHost.Execute(ctx); // see Il2CppTypes
#else
        protected override void Execute(CustomPassContext ctx) => StpHost.Execute(ctx);
#endif
    }
    internal static class StpHost
    {
        private const int UpscaleEvent = 1;
        private static Harmony harmony;
        private static GameObject host;
        private static CustomPassVolume volume;
        private static UpscalePass pass;
        private static bool failed;
        private static int memoryFull; // consecutive 10 s checks with video memory over 95% of the budget
        // What the game's controller applied last: FSR or XeSS while this mod hosts the camera.
        internal static UpscalerSetting Hosting { get; private set; } = UpscalerSetting.None;
        // DLSS hosted as Ray Reconstruction (Hosting is DLSS), at NGX's quality level rrMode.
        private static bool hostingRr;
        private static uint rrMode;
        private static Camera rrCamera;
        private static bool rrSizingCamera, rrSizingReady;
        private static uint rrOutputWidth, rrOutputHeight, rrRenderWidth, rrRenderHeight, rrSizeMode;
        internal static bool RayReconstruction => !failed && hostingRr && Hosting == UpscalerSetting.DLSS;
        internal static bool Failed => failed;
        private static float hostScale = 100f;
        private static PerformDynamicRes scaler;
        // The frame recorded in DoStpPasses, completed with the texture pointers when the pass executes.
        private static UpscaleFrame pending;
        private static TextureHandle pendingSource;
        private static bool armed;
        private static uint sequence;
        private static int lastWidth, lastHeight, lastRenderWidth, lastRenderHeight;
        private static uint lastBackend;
        // The last Unity frame this mod upscaled: a gap means stale FSR/XeSS history, and STP taking
        // over right after it means stale STP history.
        private static int lastHostedFrame = -100; // negative: none since STP last ran
        internal static ulong Frames { get; private set; }
        internal static ulong Fallbacks { get; private set; }
        internal static string Detail { get; private set; } = "Not installed.";
        internal static string LastFrame { get; private set; } = "none yet";
        internal static bool Active => !failed && (Hosting == UpscalerSetting.FSR || Hosting == UpscalerSetting.XeSS || RayReconstruction);
#if !BEPINEX_IL2CPP
        private static readonly FieldInfo CameraField = AccessTools.Field(typeof(UpScalingController), "_camera");
        private static readonly FieldInfo ProjectionParams = AccessTools.Field(typeof(HDCamera), "projectionParams");
        private static readonly FieldInfo FinalViewport = AccessTools.Field(typeof(HDCamera), "finalViewport");
        private static readonly FieldInfo RenderDelta = AccessTools.Field(typeof(HDCamera), "currentRenderDeltaTime");
        private static readonly FieldInfo ResetHistory = AccessTools.Field(typeof(HDCamera), "resetPostProcessingHistory");
        private static readonly PropertyInfo FirstFrame = AccessTools.Property(typeof(HDCamera), "isFirstFrame");
        private static readonly Func<HDRenderPipeline, HDCamera, RenderGraph, string, TextureHandle> OutputHandle =
            AccessTools.Method(typeof(HDRenderPipeline), "GetPostprocessUpsampledOutputHandle") is MethodInfo m
                ? (Func<HDRenderPipeline, HDCamera, RenderGraph, string, TextureHandle>)Delegate.CreateDelegate(typeof(Func<HDRenderPipeline, HDCamera, RenderGraph, string, TextureHandle>), m)
                : null;
        private static HDAdditionalCameraData CameraOf(UpScalingController controller) => CameraField?.GetValue(controller) as HDAdditionalCameraData;
        private static float Near(HDCamera hd) => ProjectionParams != null ? ((Vector4)ProjectionParams.GetValue(hd)).y : hd.camera.nearClipPlane;
        private static float Far(HDCamera hd) => ProjectionParams != null ? ((Vector4)ProjectionParams.GetValue(hd)).z : hd.camera.farClipPlane;
        private static Rect Viewport(HDCamera hd) => FinalViewport != null ? (Rect)FinalViewport.GetValue(hd) : hd.camera.pixelRect;
        private static float DeltaSeconds(HDCamera hd) => RenderDelta != null ? (float)RenderDelta.GetValue(hd) : Time.deltaTime;
        private static bool HistoryReset(HDCamera hd) =>
            (ResetHistory != null && (bool)ResetHistory.GetValue(hd)) || (FirstFrame != null && (bool)FirstFrame.GetValue(hd, null));
        private static void ResetStpHistory(HDCamera hd) => ResetHistory?.SetValue(hd, true);
        private static void SetScaler() => DynamicResolutionHandler.SetDynamicResScaler(scaler ?? (scaler = () => hostScale), DynamicResScalePolicyType.ReturnsPercentage);
        private static bool Ready => CameraField != null && OutputHandle != null && CustomPassRecorder.Ready;
        // HDRP's exposure members are internal here; the IL2CPP interop exposes them.
        private static readonly Func<HDRenderPipeline, HDCamera, RTHandle> ExposureTexture =
            AccessTools.Method(typeof(HDRenderPipeline), "GetExposureTexture", new[] { typeof(HDCamera) }) is MethodInfo exposureTexture
                ? (Func<HDRenderPipeline, HDCamera, RTHandle>)Delegate.CreateDelegate(typeof(Func<HDRenderPipeline, HDCamera, RTHandle>), exposureTexture) : null;
        private static readonly Action<HDCamera, RTHandle> RequestExposure =
            AccessTools.Method(typeof(HDCamera), "RequestGpuExposureValue", new[] { typeof(RTHandle) }) is MethodInfo request
                ? (Action<HDCamera, RTHandle>)Delegate.CreateDelegate(typeof(Action<HDCamera, RTHandle>), request) : null;
        private static readonly Func<HDCamera, float> ExposureValue =
            AccessTools.Method(typeof(HDCamera), "GpuExposureValue", Type.EmptyTypes) is MethodInfo value
                ? (Func<HDCamera, float>)Delegate.CreateDelegate(typeof(Func<HDCamera, float>), value) : null;
        private static float GpuExposure(HDRenderPipeline pipeline, HDCamera hd)
        {
            if (ExposureTexture == null || RequestExposure == null || ExposureValue == null) return 1f;
            var texture = ExposureTexture(pipeline, hd);
            if (texture != null) RequestExposure(hd, texture);
            return ExposureValue(hd);
        }
#else
        private static HDAdditionalCameraData CameraOf(UpScalingController controller) => controller._camera;
        private static float Near(HDCamera hd) => hd.projectionParams.y;
        private static float Far(HDCamera hd) => hd.projectionParams.z;
        private static Rect Viewport(HDCamera hd) => hd.finalViewport;
        private static float DeltaSeconds(HDCamera hd) => hd.currentRenderDeltaTime;
        private static bool HistoryReset(HDCamera hd) => hd.resetPostProcessingHistory || hd.isFirstFrame;
        private static void ResetStpHistory(HDCamera hd) => hd.resetPostProcessingHistory = true;
        private static TextureHandle OutputHandle(HDRenderPipeline pipeline, HDCamera hd, RenderGraph graph, string name) =>
            pipeline.GetPostprocessUpsampledOutputHandle(hd, graph, name);
        private static void SetScaler() => DynamicResolutionHandler.SetDynamicResScaler(
            scaler ?? (scaler = DelegateSupport.ConvertDelegate<PerformDynamicRes>(new Func<float>(() => hostScale))), DynamicResScalePolicyType.ReturnsPercentage);
        private static bool Ready => CustomPassRecorder.Ready;
        private static float GpuExposure(HDRenderPipeline pipeline, HDCamera hd)
        {
            var texture = pipeline.GetExposureTexture(hd);
            if (texture != null) hd.RequestGpuExposureValue(texture);
            return hd.GpuExposureValue();
        }
#endif
        internal static void Initialize()
        {
            if (harmony != null) return;
            try
            {
                if (!Ready) throw new MissingMemberException("HDRP's STP pass, custom pass recording or the game's upscaling controller changed in this game build.");
#if BEPINEX_IL2CPP
                Il2CppTypes.Register<UpscalePass>();
#endif
                // Never enabled: the volume only gives the pass its injection point; HDRP never runs it itself.
                host = new GameObject("TG Better Upscaling pass") { hideFlags = HideFlags.HideAndDontSave };
                host.SetActive(false);
                UnityEngine.Object.DontDestroyOnLoad(host);
                volume = host.AddComponent<CustomPassVolume>();
                volume.isGlobal = false;
                volume.injectionPoint = CustomPassInjectionPoint.BeforePostProcess;
                pass = new UpscalePass
                {
                    name = "TG Better Upscaling",
                    targetColorBuffer = CustomPass.TargetBuffer.None,
                    targetDepthBuffer = CustomPass.TargetBuffer.None
                };
                var candidate = new Harmony(UpscalingPlugin.Id);
                try
                {
                    var controllerMethod = AccessTools.Method(typeof(UpScalingController), "OnSettingChanged", new[] { typeof(Setting) });
                    var stp = AccessTools.Method(typeof(HDRenderPipeline), "DoStpPasses");
                    if (controllerMethod == null || stp == null || stp.ReturnType != typeof(TextureHandle))
                        throw new MissingMethodException("UpScalingController.OnSettingChanged or HDRenderPipeline.DoStpPasses");
                    candidate.Patch(controllerMethod, prefix: new HarmonyMethod(typeof(StpHost), nameof(BeforeSettingChanged)));
                    candidate.Patch(stp, prefix: new HarmonyMethod(typeof(StpHost), nameof(BeforeStp)));
#if !BEPINEX_IL2CPP
                    // TG Path Tracing is Mono-only. A missing RR sizing hook must not disable FSR/XeSS hosting.
                    try
                    {
                        candidate.Patch(AccessTools.Method(typeof(DynamicResolutionHandler), "UpdateAndUseCamera"),
                            prefix: new HarmonyMethod(typeof(StpHost), nameof(BeforeResolutionCamera)));
                        candidate.Patch(AccessTools.Method(typeof(DynamicResolutionHandler), "ApplyScalesOnSize", new[] { typeof(Vector2Int), typeof(Vector2) }),
                            postfix: new HarmonyMethod(typeof(StpHost), nameof(AfterResolutionSize)));
                        rrSizingReady = true;
                    }
                    catch (Exception ex) { Session.Log.LogWarning("Ray Reconstruction sizing could not be hooked: " + ex.Message); }
#endif
                    harmony = candidate;
                }
                catch { candidate.UnpatchSelf(); throw; }
                Detail = "Hooked the game's upscaling controller and HDRP's STP pass. " + InjectionPoint();
                Session.Log.LogInfo(Detail);
            }
            catch (Exception ex)
            {
                failed = true;
                Detail = "Could not hook HDRP's STP pass, so FSR and XeSS cannot run: " + ex.Message;
                Session.Log.LogError(Detail + "\n" + ex);
                Dispose();
            }
        }
        // Where HDRP runs STP, and so FSR and XeSS in its place, as the game's HDRP asset sets it. At
        // BeforePost (HDRP's default) upscaling comes before depth of field, motion blur, bloom, lens flares
        // and the uber pass (chromatic aberration, vignette, lens distortion, grading). Film grain and
        // dithering are in HDRP's final pass, after every upscaler.
        private static string InjectionPoint()
        {
            try
            {
#if BEPINEX_IL2CPP
                var asset = HDRenderPipeline.currentAsset;
#else
                var asset = GraphicsSettings.currentRenderPipeline as HDRenderPipelineAsset; // HDRenderPipeline.currentAsset is internal
#endif
                if (asset == null) return "HDRP's STP injection point is unknown (no HDRP asset).";
                var point = asset.currentPlatformRenderPipelineSettings.dynamicResolutionSettings.STPInjectionPoint;
                string where = point == DynamicResolutionHandler.UpsamplerScheduleType.BeforePost ? "before post-processing"
                    : point == DynamicResolutionHandler.UpsamplerScheduleType.AfterDepthOfField ? "after depth of field, before the rest of post-processing"
                    : "after post-processing";
                return "HDRP runs STP, and FSR or XeSS in its place, " + where + " (" + point + "); film grain comes after it. " + UpscalerOrder(asset);
            }
            catch (Exception ex) { return "HDRP's STP injection point is unknown (" + ex.GetType().Name + ")."; }
        }
        // The order in which HDRP tries its upscalers for a camera, as the game's HDRP asset lists them: the first one
        // the camera allows runs. Game builds from October 2026 have HDRP's own FSR 2 in it.
        private static string UpscalerOrder(HDRenderPipelineAsset asset)
        {
            try
            {
                var order = asset.currentPlatformRenderPipelineSettings.dynamicResolutionSettings.advancedUpscalersByPriority;
                string names = "";
                if (order != null) for (int i = 0; i < order.Count; i++) names += (i > 0 ? ", " : "") + order[i];
                return "HDRP's upscaler order: " + (names.Length > 0 ? names : "none") + ".";
            }
            catch (Exception ex) { return "HDRP's upscaler order is unknown (" + ex.GetType().Name + ")."; }
        }
        // The game's graphics presets set the render-scale slider; the quality modes follow them. Hooked only
        // once the game has created its Upscaling setting: patching compiles UpScaling.SetValueForPreset,
        // and Mono then runs UpScaling's static constructor, which looks up localized labels. At plugin
        // load the game hasn't loaded its localization, and that lookup waits for it forever.
        private static bool presetsHooked;
        internal static void HookPresets()
        {
            if (presetsHooked || harmony == null) return;
            presetsHooked = true;
            try
            {
                var preset = AccessTools.Method(typeof(UpScaling), "SetValueForPreset");
                if (preset == null) throw new MissingMethodException("UpScaling.SetValueForPreset");
                harmony.Patch(preset, postfix: new HarmonyMethod(typeof(StpHost), nameof(AfterPreset)));
            }
            catch (Exception ex) { Session.Log.LogWarning("The game's graphics presets won't set the quality modes: " + ex.Message); }
        }
        private static void AfterPreset(UpScaling __instance)
        {
            try { UpscalerRow.OnPreset(__instance); }
            catch (Exception ex) { Session.Log.LogWarning("The graphics preset could not set the quality modes: " + ex.Message); }
        }
        // The game's controller acts on its Upscaling setting. For FSR and XeSS it would log an error and
        // turn upscaling off; instead the camera is set up as for STP, at the render scale of the chosen
        // quality mode (or the game's slider when the runtime reports none). DLSS with a quality mode is
        // set up as the game does it, with NVIDIA's mode and render size instead of the slider's.
        private static bool BeforeSettingChanged(UpScalingController __instance)
        {
            try
            {
                var up = __instance.Target;
                if (up == null) return true;
                UpscalerRow.OnGameApplied(up);
                int index = up.ActiveIndex;
                // DLSS as Ray Reconstruction while TG Path Tracing traces: STP's pass, taken over like FSR's.
                if (index == (int)UpscalerSetting.DLSS && UpScaling.IsDLSSAvailable && UpScaling.IsSTPAvailable && !failed &&
                    Session.Settings.Mode(UpscalerSetting.DLSS) is QualityMode rrDlss && PathTracingLink.Wanted &&
                    Session.HaveStatus && Session.Status.Usable(NativeFlags.BackendRayReconstruction) && ConfigureRayReconstruction(__instance, rrDlss))
                    return false;
                hostingRr = false;
                // With the row left as the game has it (UpscalerRow.Failed), the quality modes have no rows: the game
                // sets DLSS up itself, at its own slider.
                if (index == (int)UpscalerSetting.DLSS && UpScaling.IsDLSSAvailable && !UpscalerRow.Failed && Session.Settings.Mode(UpscalerSetting.DLSS) is QualityMode dlss &&
                    ConfigureDlss(__instance, dlss))
                {
                    Hosting = UpscalerSetting.None;
                    SetMipBias(CameraOf(__instance), UpscalerSetting.None);
                    return false;
                }
                // FSR is this mod's where AMD's runtime reported it. Otherwise index 3 is the game's own FSR 2 (game
                // builds from October 2026), which the game's controller sets up itself; so it is until the runtimes
                // have reported, and with the row left as the game has it. XeSS is always this mod's.
                // After a failure FSR and XeSS no longer take STP's pass over (Active is false), but FSR or XeSS stays
                // configured below, so HDRP's own STP runs at its render scale. Handed to the game instead, a XeSS
                // index would switch upscaling off altogether: the game's controller knows no such type.
                bool hosted = !UpscalerRow.Failed && (index == UpscalerRow.XessIndex || (index == UpscalerRow.FsrIndex && Session.Settings.Capabilities?.Fsr == true));
                if (!hosted || !UpScaling.IsSTPAvailable)
                {
                    Hosting = UpscalerSetting.None;
                    var reported = Session.Settings.Capabilities;
                    string game = index == UpscalerRow.FsrIndex && (reported == null || reported.GameFsr != null) ? "game FSR" : "";
                    if (game.Length > 0 && game != configured)
                        Session.Log.LogInfo("The game sets up its own FSR 2, at its own render scale: " + (reported == null ? "the runtimes have not reported to this mod yet."
                            : UpscalerRow.Failed ? "the Upscaling row is left as the game has it." : "AMD's runtime reported no FSR to this mod."));
                    configured = game;
                    SetMipBias(CameraOf(__instance), UpscalerSetting.None);
                    return true;
                }
                var chosen = (UpscalerSetting)index;
                var mode = Session.Settings.Mode(chosen);
                hostMode = chosen == UpscalerSetting.XeSS && mode != null ? (uint)mode.Id : 0u;
                if (!Configure(__instance, chosen, mode != null ? QualityModes.RenderScale(mode) : up.QualityScaling, mode))
                    { Hosting = UpscalerSetting.None; SetMipBias(null, UpscalerSetting.None); return true; }
                return false;
            }
            catch (Exception ex)
            {
                Session.Log.LogError("Upscaling controller hook failed; the game's own upscaling continues: " + ex);
                Hosting = UpscalerSetting.None;
                try { SetMipBias(null, UpscalerSetting.None); } catch { }
                return true;
            }
        }
        // The camera as the game's controller sets it up for DLSS (UpScalingController.EnableDLSS), with the
        // chosen mode as NGX's quality level, so that NVIDIA's driver picks that mode's model, and NVIDIA's
        // render size for it.
        private static bool ConfigureDlss(UpScalingController owner, QualityMode mode)
        {
            var camera = CameraOf(owner);
            if (camera == null) return false;
            camera.allowFidelityFX2SuperResolution = false;
            camera.deepLearningSuperSamplingQuality = (uint)mode.Id;
            camera.allowDynamicResolution = true;
            camera.allowDeepLearningSuperSampling = true;
            camera.deepLearningSuperSamplingUseCustomQualitySettings = true;
            hostScale = QualityModes.RenderScale(mode);
            SetScaler();
            DynamicResolutionHandler.SetActiveDynamicScalerSlot(DynamicResScalerSlot.User);
            string now = "DLSS " + mode.Name;
            if (now != configured)
                Session.Log.LogInfo("Upscaling with DLSS " + mode.Name + " at " + mode.Scale.ToString("0.#") + "% render scale (NGX quality level " + mode.Id +
                    "; NVIDIA's driver picks the model).");
            configured = now;
            return true;
        }
        private static string configured = "";
        private static uint hostMode;
        // The camera as for STP, at the DLSS mode's render scale, with Ray Reconstruction in STP's place. HDRP
        // biases texture sampling for the lower render resolution wherever STP runs, as NVIDIA asks for DLSS.
        private static bool ConfigureRayReconstruction(UpScalingController owner, QualityMode mode)
        {
            if (!rrSizingReady) return false;
            var camera = CameraOf(owner);
            if (camera == null) return false;
            rrCamera = camera.GetComponent<Camera>();
            uint width = (uint)Mathf.Max(1, rrCamera.pixelWidth), height = (uint)Mathf.Max(1, rrCamera.pixelHeight);
            if (!RrSize(width, height, (uint)mode.Id)) return false;
            camera.allowDynamicResolution = true;
            camera.allowDeepLearningSuperSampling = false;
            camera.deepLearningSuperSamplingUseCustomQualitySettings = false;
            camera.allowFidelityFX2SuperResolution = false;
            hostScale = 100f * rrRenderWidth / rrOutputWidth;
            SetScaler();
            DynamicResolutionHandler.SetActiveDynamicScalerSlot(DynamicResScalerSlot.User);
            SetMipBias(camera, UpscalerSetting.None);
            string now = "RR " + mode.Name + " " + hostScale.ToString("0.#");
            if (now != configured)
                Session.Log.LogInfo("Upscaling with DLSS Ray Reconstruction " + mode.Name + " at " + hostScale.ToString("0.#") +
                    "% render scale (HDRP's STP pass is taken over): it denoises TG Path Tracing's light and upscales in one step.");
            configured = now;
            Hosting = UpscalerSetting.DLSS; hostingRr = true; rrMode = (uint)mode.Id;
            return true;
        }
        private static bool RrSize(uint width, uint height, uint mode)
        {
            if (rrOutputWidth == width && rrOutputHeight == height && rrSizeMode == mode && rrRenderWidth != 0) return true;
            rrRenderWidth = rrRenderHeight = 0;
            if (!Session.Upscaler.RrOptimalSettings(width, height, mode, out rrRenderWidth, out rrRenderHeight)) return false;
            rrOutputWidth = width; rrOutputHeight = height; rrSizeMode = mode;
            return true;
        }
        private static void BeforeResolutionCamera(Camera __0) => rrSizingCamera = __0 != null && __0 == rrCamera;
        // HDRP normally rounds software scaling up to even pixels. RR does not support dynamic resolution: use its
        // exact two-dimensional answer before HDCamera sizes its targets, depth, motion, jitter and screen constants.
        // GetScaledSize still updates HDRP's cached size; other cameras and reduced-size auxiliary targets keep theirs.
        private static void AfterResolutionSize(DynamicResolutionHandler __instance, Vector2Int size, ref Vector2Int __result)
        {
            if (!RayReconstruction || !rrSizingCamera || size != __instance.finalViewport || size.x <= 0 || size.y <= 0) return;
            if (!RrSize((uint)size.x, (uint)size.y, rrMode)) return;
            hostScale = 100f * rrRenderWidth / rrOutputWidth;
            __result = new Vector2Int((int)rrRenderWidth, (int)rrRenderHeight);
        }
        // The camera as the game's controller sets it up for STP, for FSR or XeSS in STP's place. HDRP runs one
        // upscaler for a camera, the first in its asset's list that the camera allows: the game's own FSR 2 (game
        // builds from October 2026) is switched off here as the game switches it off for STP, or it could run in
        // STP's place, and this mod's pass would never be asked for.
        private static bool Configure(UpScalingController owner, UpscalerSetting upscaler, float scale, QualityMode mode)
        {
            var camera = CameraOf(owner);
            if (camera == null) return false;
            camera.allowDynamicResolution = true;
            camera.allowDeepLearningSuperSampling = false;
            camera.deepLearningSuperSamplingUseCustomQualitySettings = false;
            camera.allowFidelityFX2SuperResolution = false;
            hostingRr = false;
            hostScale = Mathf.Clamp(scale, 10f, 100f);
            SetScaler();
            DynamicResolutionHandler.SetActiveDynamicScalerSlot(DynamicResScalerSlot.User);
            SetMipBias(camera, failed ? UpscalerSetting.None : upscaler); // STP after a failure: HDRP's own bias only
            string now = upscaler + (mode != null ? " " + mode.Name : "") + " " + hostScale.ToString("0.#") + (failed ? " stp" : "");
            if (now != configured)
                Session.Log.LogInfo(failed
                    ? "STP upscales at " + hostScale.ToString("0.#") + "% render scale in place of " + upscaler + ", which stopped after an error this session."
                    : "Upscaling with " + upscaler + (mode != null ? " " + mode.Name : "") + " at " + hostScale.ToString("0.#") + "% render scale (HDRP's STP pass is taken over)" +
                      (upscaler == UpscalerSetting.FSR ? "; textures get AMD's extra mip bias of " + FsrMipBias.ToString("0.#") + "." : "."));
            configured = now;
            Hosting = upscaler;
            return true;
        }
        // HDRP biases texture sampling by log2(render / output width) wherever STP runs, which is what
        // Intel asks for XeSS. AMD asks for one mip more with FSR: log2(render / output width) - 1. The
        // camera's material bias is added to HDRP's; the game never sets it, and it goes back when FSR is
        // no longer selected.
        internal const float FsrMipBias = -1f;
        private static HDAdditionalCameraData biasedCamera;
        private static float biasBefore;
        private static void SetMipBias(HDAdditionalCameraData camera, UpscalerSetting upscaler)
        {
            if (biasedCamera != null && (upscaler != UpscalerSetting.FSR || biasedCamera != camera))
            {
                biasedCamera.materialMipBias = biasBefore;
                biasedCamera = null;
            }
            if (upscaler != UpscalerSetting.FSR || camera == null || biasedCamera != null) return;
            biasBefore = camera.materialMipBias;
            camera.materialMipBias = biasBefore + FsrMipBias;
            biasedCamera = camera;
        }
        // The exposure HDRP baked into this frame's color, as HDRP's own DLSS pass takes it: a GPU readback
        // requested every frame (so a frame or two old), limited to 0.2..2. FSR divides it out, so its
        // history stays valid while the exposure adapts, and the limit keeps that history within half
        // precision in scenes whose exposure is far from 1. The first frames, before a readback arrives, get 1.
        private static float PreExposure(HDRenderPipeline pipeline, HDCamera hd)
        {
            try
            {
                float value = GpuExposure(pipeline, hd);
                return float.IsNaN(value) || float.IsInfinity(value) || value <= 0f ? 1f : Mathf.Clamp(value, 0.2f, 2f);
            }
            catch { return 1f; }
        }
        private static bool IsGameplayCamera(HDCamera hd)
        {
            var camera = hd?.camera;
            var main = Camera.main;
            return camera != null && main != null && camera.GetInstanceID() == main.GetInstanceID() && camera.cameraType == CameraType.Game && camera.targetTexture == null;
        }
        // HDRP is about to run STP for a camera. For the gameplay camera with FSR or XeSS selected and
        // usable, record this mod's pass instead and return its output as STP's.
        // Where HDRP's STP calls went, logged every 10 s while any arrive: a frame that never reaches the
        // native side shows here as recorded without executed, or as STP kept and why.
        private static long hookCalls, keptInactive, keptCamera, keptRuntime, keptSize, recorded, executed, submitted;
        private static int unexecuted; // recorded passes in a row that HDRP never ran
        private static float nextHookReport;
        private static void ReportHook()
        {
            float now = Time.unscaledTime;
            if (now < nextHookReport) return;
            bool first = nextHookReport == 0f;
            nextHookReport = now + 10f;
            if (first) return;
            Session.Log.LogInfo("Upscaling hook 10s: HDRP ran its STP step " + hookCalls + " times; FSR/XeSS recorded " + recorded + ", executed " + executed +
                ", handed to the native side " + submitted + "; STP kept: not hosting " + keptInactive + ", other camera " + keptCamera +
                ", runtime not ready " + keptRuntime + ", sizes " + keptSize + " | hosting " + Hosting + ", fallbacks " + Fallbacks + ", last: " + LastFrame);
            hookCalls = keptInactive = keptCamera = keptRuntime = keptSize = recorded = executed = submitted = 0;
            // Out of video memory with FSR or XeSS running, on two of these 10 s checks in a row: STP takes over for
            // the session, in case they are what fills it (the bridge's video memory lines then show whether the
            // growth stops). One high sample is normal on smaller GPUs and when Windows lowers the budget.
            BridgePerformance p = default;
            bool full = Active && GraphicsBridge.TryReadPerformance(out p) && (p.Valid & 4) != 0 && p.VramBudget != 0 && p.VramProcess > p.VramBudget / 100 * 95;
            memoryFull = full ? memoryFull + 1 : 0;
            if (memoryFull >= 2)
                Fail("Video memory is nearly exhausted (" + (p.VramProcess / 1073741824.0).ToString("0.00") + " of " + (p.VramBudget / 1073741824.0).ToString("0.00") +
                    " GB); FSR and XeSS stop and STP upscales for the rest of the session.");
        }
        private static bool BeforeStp(HDRenderPipeline __instance, RenderGraph renderGraph, HDCamera hdCamera,
            TextureHandle inputColor, TextureHandle inputDepth, TextureHandle inputMotion, ref TextureHandle __result)
        {
            // HDRP executes a camera's render graph before the next camera starts, so a pass still armed here never
            // ran. A graph that keeps dropping it is broken, and can pile up memory each frame: STP takes over.
            if (armed && ++unexecuted >= 3)
                Fail("HDRP's render graph did not run the upscaling pass it recorded, three frames in a row; STP upscales for the rest of the session.");
            armed = false;
            if (hdCamera == null) return true;
            ++hookCalls;
            ReportHook();
            if (!Active) { ++keptInactive; return StpRuns(hdCamera); }
            try
            {
                var technology = Hosting;
                bool rr = RayReconstruction;
                uint backend = rr ? NativeFlags.BackendRayReconstruction : technology == UpscalerSetting.FSR ? NativeFlags.BackendFsr : NativeFlags.BackendXess;
                if (!IsGameplayCamera(hdCamera)) { ++keptCamera; return true; }
                if (!Session.Upscaler.Connected || !Session.HaveStatus || !Session.Status.Usable(backend)) { ++keptRuntime; return StpRuns(hdCamera); }
                var camera = hdCamera.camera;
                var viewport = Viewport(hdCamera);
                uint outputWidth = (uint)Mathf.RoundToInt(viewport.width), outputHeight = (uint)Mathf.RoundToInt(viewport.height);
                uint renderWidth = (uint)Mathf.Max(0, hdCamera.actualWidth), renderHeight = (uint)Mathf.Max(0, hdCamera.actualHeight);
                if (rr && (!RrSize(outputWidth, outputHeight, rrMode) || renderWidth != rrRenderWidth || renderHeight != rrRenderHeight))
                {
                    LastFrame = "Ray Reconstruction skipped: HDRP's render dimensions do not match NGX's RR optimal settings";
                    ++keptSize;
                    return StpRuns(hdCamera);
                }
                float near = Near(hdCamera), far = Far(hdCamera), fov = camera.fieldOfView * Mathf.Deg2Rad;
                var jitter = hdCamera.taaJitter;
                if (renderWidth == 0 || renderHeight == 0 || renderWidth > outputWidth || renderHeight > outputHeight || outputWidth > 16384 || outputHeight > 16384 ||
                    !(near > 0) || !(far > near) || !(fov > 0 && fov < Mathf.PI) || Mathf.Abs(jitter.x) > 1 || Mathf.Abs(jitter.y) > 1)
                {
                    LastFrame = "skipped: render " + renderWidth + "x" + renderHeight + ", output " + outputWidth + "x" + outputHeight + ", near/far " + near + "/" + far;
                    ++keptSize;
                    return StpRuns(hdCamera);
                }
                var settings = Session.Settings;
                bool fsr = technology == UpscalerSetting.FSR;
                uint flags = SystemInfo.usesReversedZBuffer ? NativeFlags.DepthInverted : 0u;
                // A gap since the last upscaled frame (STP or None in between) leaves FSR's or XeSS's history stale.
                if (HistoryReset(hdCamera) || outputWidth != lastWidth || outputHeight != lastHeight || backend != lastBackend ||
                    Time.frameCount - lastHostedFrame > 1) flags |= NativeFlags.Reset;
                lastHostedFrame = Time.frameCount;
                if (fsr && settings.AutoExposure.Value) flags |= NativeFlags.FsrAutoExposure;
                if (fsr && settings.FsrInputChecker.Value) flags |= NativeFlags.FsrDebugCheck;
                if (fsr && settings.FsrDebugView.Value) flags |= NativeFlags.FsrDebugView;
                lastWidth = (int)outputWidth; lastHeight = (int)outputHeight; lastBackend = backend;
                lastRenderWidth = (int)renderWidth; lastRenderHeight = (int)renderHeight;
                var request = new UpscaleFrame
                {
                    Size = NativeUpscaler.FrameSize, Abi = NativeUpscaler.Abi, Sequence = ++sequence, Backend = backend,
                    RenderWidth = renderWidth, RenderHeight = renderHeight, OutputWidth = outputWidth, OutputHeight = outputHeight,
                    JitterX = jitter.x, JitterY = jitter.y,
                    // HDRP's motion vectors are UV offsets to the previous frame; the SDKs take render pixels.
                    MotionScaleX = -(float)renderWidth, MotionScaleY = -(float)renderHeight,
                    CameraNear = near, CameraFar = far, FovY = fov,
                    FrameTimeMs = Mathf.Clamp(DeltaSeconds(hdCamera) * 1000f, 1f, 200f),
                    Sharpness = fsr ? settings.Sharpness : 0f, PreExposure = PreExposure(__instance, hdCamera),
                    Flags = flags, Mode = rr ? rrMode : technology == UpscalerSetting.XeSS ? hostMode : 0u
                };
                if (rr) SetCamera(ref request, hdCamera);
                pending = request;
                var output = OutputHandle(__instance, hdCamera, renderGraph, "TG Better Upscaling Output");
                // The output is the pass's color target; color is declared as a second color target so the
                // graph keeps it alive (it is not handed to the pass's context, which is fine: this mod reads
                // it itself from the handle).
                var targets = new CustomPass.RenderTargets
                {
                    colorBufferRG = output, nonMSAAColorBufferRG = inputColor,
                    depthBufferRG = inputDepth, motionVectorBufferRG = inputMotion
                };
                pendingSource = inputColor;
                armed = true;
                CustomPassRecorder.Record(pass, volume, __instance, renderGraph, hdCamera, targets);
                ++recorded;
                __result = output;
                return false;
            }
            catch (Exception ex)
            {
                armed = false;
                Fail("Recording the upscaling pass failed; STP upscales instead: " + ex);
                return StpRuns(hdCamera);
            }
        }
        // Ray Reconstruction's camera as NVIDIA takes it (row-major, row vectors on the left): the camera's
        // world-to-view matrix and HDRP's unjittered projection in its GPU form (reversed Z, the render
        // target's orientation), the clip space HDRP's depth and motion vectors are in.
        private static unsafe void SetCamera(ref UpscaleFrame frame, HDCamera hd)
        {
            Matrix4x4 view = hd.camera.worldToCameraMatrix, projection = hd.mainViewConstants.nonJitteredProjMatrix;
            fixed (float* w = frame.WorldToView, c = frame.ViewToClip)
                for (int r = 0; r < 4; r++)
                    for (int k = 0; k < 4; k++) { w[r * 4 + k] = view[k, r]; c[r * 4 + k] = projection[k, r]; }
        }
        // STP upscales this frame. Right after a frame this mod upscaled, STP's history is from before
        // FSR or XeSS took over, so it starts afresh (HDRP reads this in PopulateStpConfig, next).
        private static bool StpRuns(HDCamera hdCamera)
        {
            try
            {
                if (lastHostedFrame >= 0 && Time.frameCount - lastHostedFrame <= 1 && lastHostedFrame != Time.frameCount && IsGameplayCamera(hdCamera))
                {
                    ResetStpHistory(hdCamera);
                    lastHostedFrame = -100;
                }
            }
            catch { } // STP runs regardless
            return true;
        }
        // Render-graph execution of this mod's pass, main thread: the texture pointers, then the native request.
        internal static void Execute(CustomPassContext ctx)
        {
            RTHandle source = null;
            ++executed; unexecuted = 0;
            try
            {
                source = pendingSource; // implicit conversion: valid while the graph executes
                var destination = ctx.cameraColorBuffer;
                if (!armed || source == null || destination == null) { Fallback(ctx, source, "no recorded frame"); return; }
                armed = false;
                var color = NativeTextures.Pointer(source);
                var depth = NativeTextures.Pointer(ctx.cameraDepthBuffer);
                var motion = NativeTextures.Pointer(ctx.cameraMotionVectorsBuffer);
                var output = NativeTextures.Pointer(destination);
                if (color == IntPtr.Zero || depth == IntPtr.Zero || motion == IntPtr.Zero || output == IntPtr.Zero)
                {
                    Fallback(ctx, source, "a texture had no native resource"); return;
                }
                var frame = pending;
                frame.Color = unchecked((ulong)color.ToInt64()); frame.Depth = unchecked((ulong)depth.ToInt64());
                frame.Motion = unchecked((ulong)motion.ToInt64()); frame.Output = unchecked((ulong)output.ToInt64());
                bool rr = frame.Backend == NativeFlags.BackendRayReconstruction;
                // Ray Reconstruction's guides are TG Path Tracing's, written earlier in this frame.
                if (rr && !PathTracingLink.TryGuides(frame.RenderWidth, frame.RenderHeight, out frame.DiffuseAlbedo, out frame.SpecularAlbedo, out frame.NormalRoughness))
                {
                    Fallback(ctx, source, "TG Path Tracing wrote no Ray Reconstruction guides this frame"); return;
                }
                if (rr && !PathTracingLink.TryExtraGuides(frame.RenderWidth, frame.RenderHeight, out frame.SpecularHitDistance, out frame.ColorBeforeTransparency))
                {
                    Fallback(ctx, source, "TG Path Tracing wrote no reflection distance for Ray Reconstruction this frame"); return;
                }
                int result = Session.Upscaler.SubmitFrame(ref frame);
                if (result != 0) { Fallback(ctx, source, "the native side refused the frame (" + result + ")"); return; }
                // One event: the request carries its destination, so the native side upscales and writes it.
                ctx.cmd.IssuePluginEventAndData(Session.Upscaler.RenderEvent, UpscaleEvent, new IntPtr((long)frame.Sequence));
                ++Frames; ++submitted;
                if (rr && Session.RayReconstructionSucceeding()) PathTracingLink.Ran(Time.frameCount);
                LastFrame = (rr ? "DLSS Ray Reconstruction" : frame.Backend == NativeFlags.BackendFsr ? "FSR" : "XeSS") + " " + frame.RenderWidth + "x" + frame.RenderHeight +
                    " -> " + frame.OutputWidth + "x" + frame.OutputHeight;
            }
            catch (Exception ex)
            {
                Fallback(ctx, source, ex.GetType().Name);
                Fail("The upscaling pass failed; STP upscales from the next frame on: " + ex);
            }
        }
        // HDRP reads the output next: it is never left unwritten. A bilinear enlargement, for this frame only.
        private static void Fallback(CustomPassContext ctx, RTHandle source, string why)
        {
            ++Fallbacks;
            LastFrame = "enlarged without upscaling: " + why;
            try { if (source != null && ctx.cameraColorBuffer != null) HDUtils.BlitCameraTexture(ctx.cmd, source, ctx.cameraColorBuffer, 0f, true); }
            catch { } // the output keeps whatever it held
            if (Fallbacks == 1 || Fallbacks % 600 == 0) Session.Log.LogWarning("Upscaling frame not handed to the native side (" + why + "); " + Fallbacks + " so far.");
        }
        private static void Fail(string message)
        {
            if (failed) return;
            failed = true;
            Detail = message;
            Session.Log.LogError(message);
        }
        internal static string Describe() =>
            Detail + " | hosting " + (RayReconstruction ? "DLSS Ray Reconstruction" : Hosting.ToString()) + (failed ? " (disabled after an error)" : "") + " at " + hostScale.ToString("0.#") + "% | frames " + Frames +
            ", managed fallbacks " + Fallbacks + " | last: " + LastFrame;
        internal static void Dispose()
        {
            rrSizingReady = rrSizingCamera = false;
            var installed = harmony;
            harmony = null;
            try { installed?.UnpatchSelf(); } catch (Exception ex) { Session.Log?.LogWarning("Upscaling unpatch failed: " + ex.Message); }
            if (host != null) UnityEngine.Object.Destroy(host);
            host = null; volume = null; pass = null; armed = false; presetsHooked = false;
            Hosting = UpscalerSetting.None; hostingRr = false;
            try { SetMipBias(null, UpscalerSetting.None); } catch (Exception ex) { Session.Log?.LogWarning("Could not restore the camera's mip bias: " + ex.Message); }
        }
    }
}
