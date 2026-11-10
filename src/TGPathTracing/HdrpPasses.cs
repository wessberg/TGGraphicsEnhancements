// SPDX-License-Identifier: MIT
// Where the path tracer meets HDRP's frame.
//
// Capture: HDRP renders the GBuffer in its prepass, then (before shadows and lighting) runs its opaque
// screen-space reflections step, RenderSSR, which receives the whole prepass output. A Harmony prefix on
// RenderSSR records this mod's capture pass into HDRP's render graph right there, through
// CustomPass.ExecuteInternal, declaring GBuffer0 (as the pass's color buffer), the normal buffer, depth and
// motion vectors as its targets and GBuffers 2 and 3 as extra reads (SetupBeforeExecute). The render graph
// keeps them alive and in place for the pass, which hands their native textures to the tracer and issues
// render event 1: the D3D12 path tracing then runs alongside HDRP's shadows and lighting. The camera's matrices
// handed over are the ones HDRP holds right there, while it records (armedView): they hold the frame's
// sub-pixel shift, which HDRP takes out of its camera again before the capture pass executes (PT-84).
//
// Composite: a global custom pass after opaque lighting and the sky (before fog, transparents and post)
// issues render event 2, which waits on the GPU for this frame's own traced light and lays it into HDRP's
// color buffer, each pixel its own texel (until 2026-10-08: the previous frame's, moved by motion vectors).
//
// Indirect light: while tracing, a global volume zeroes HDRP's indirect diffuse lighting multiplier, so
// probes, lightmaps and screen-space GI are not added on top of the traced indirect light.
//
// Water: HDRP draws its water (and transparent surfaces) after the composite and takes their reflections from its
// transparent SSR step, then reflection probes and the sky. A Harmony postfix on that step (RenderSSR, transparent)
// records a pass right behind it whose render event 4 fills the part of the reflections its SSR found nothing for from
// the traced scene, and hands the frame's water surfaces over to be traced for the next frame.
using System;
using System.Collections.Generic;
using System.Reflection;
using HarmonyLib;
using TGGraphicsBridge;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.HighDefinition;
using UnityEngine.Rendering.RenderGraphModule;
namespace TGPathTracing
{
    internal sealed class CapturePass : CustomPass
    {
        protected override void SetupBeforeExecute(RenderGraph renderGraph, IUnsafeRenderGraphBuilder builder, RenderTargets currentRenderTargets) =>
            HdrpPasses.DeclareExtraReads(builder);
        protected override void Execute(CustomPassContext ctx) => HdrpPasses.Capture(ctx);
    }
    internal sealed class CompositePass : CustomPass
    {
        protected override void Execute(CustomPassContext ctx) => HdrpPasses.Composite(ctx);
    }
    // Right after HDRP's transparent SSR step: its SSR texture as the color buffer, its depth-stencil and normal buffers.

    // The image as it is seen, HDRP's fog and transparent surfaces over the traced light, before post-processing: a small
    // copy read back for the traced light's exposure (HdrpPasses.MeasureSeen).
    // PT-112's measurement (only with the measure-waits file): render event 8 at one of HDRP's injection points, so that
    // the native side times HDRP's pass groups on the game's device.
    internal sealed class TimingPass : CustomPass
    {
        internal int Mark;
        protected override void Execute(CustomPassContext ctx) => HdrpPasses.MarkTiming(ctx, Mark);
    }

    internal sealed class SeenMeterPass : CustomPass
    {
        protected override void Execute(CustomPassContext ctx) => HdrpPasses.MeasureSeen(ctx);
    }

    internal static class HdrpPasses
    {
        private static Harmony harmony;
        private static GameObject captureHost, compositeHost, indirectHost, scaledHost;
        private static CustomPassVolume captureVolume;
        private static CapturePass capturePass;
        private static Volume indirectVolume, scaledVolume;
        // The light at the camera against the open sky's (pt_probe.hlsl; 1 in the open, low indoors), eased over half a
        // second: it scales what HDRP still lights with the sky's light as if no walls stood in between (the ambient light
        // and reflections of the surfaces HDRP shades itself: glass, eyes, hair), which made glass glow in a dark room.
        // Not the fog: that is the game's own, at its own exposure (the art of a place, blue mist over a castle at night).
        private static float cameraLight = 1f;
        internal static float CameraLight => cameraLight;
        // Whether the gameplay camera renders HDRP's shadow maps (the game's Shadows toggle), as of its last traced frame.
        internal static bool ShadowMaps { get; private set; } = true;
        private static bool failed;
        internal static string Detail { get; private set; } = "Not installed.";
        // The frame recorded in RenderSSR, completed when the capture pass executes.
        private static TextureHandle pendingGBuffer2, pendingGBuffer3, pendingPyramid;
        private static bool armed, hasGBuffer3;
        // The camera's matrices as they are while HDRP records its opaque passes: WITH the frame's sub-pixel shift (PT-84).
        // HDRP takes the shift out of HDCamera.mainViewConstants again while it is still RECORDING the frame (its upscaling
        // or TAA step ends with RestoreNonjitteredMatrices, which runs at record time), and the capture pass executes after
        // all recording: read there, as until 2026-10-08, the matrices had no shift, every camera ray of the tracer went
        // through the plain pixel centre, and the upscaler, told HDRP's shift, laid the same unshifted picture down at another
        // place each frame (2-pixel stairs on every edge, thin things as dots).
        private static HDCamera.ViewConstants armedView;
        private static bool shiftSaid;
        private static int unexecuted;
        private static uint sequence, pendingComposite;
        private static int pendingCompositeFrame = -1;
        private static Vector3 lastCameraPosition, lastCameraForward; private static bool haveLastCamera, lastHdrpReset;
        private static int lastCameraId;
        // HDRP's own "start the history over" for a camera (HDCamera.Reset: a new or re-created camera), which its
        // post-processing clears once it ran.
        private static readonly FieldInfo HdrpHistoryReset = AccessTools.Field(typeof(HDCamera), "resetPostProcessingHistory");
        internal static ulong Captured { get; private set; }
        internal static ulong Composited { get; private set; }
        internal static string LastFrame { get; private set; } = "none yet";
        private static long recorded, executed, submitted, skippedCamera, skippedMode, skippedLit;
        private static float nextReport;

        private static readonly FieldInfo CustomColor = AccessTools.Field(typeof(HDRenderPipeline), "m_CustomPassColorBuffer");
        private static readonly FieldInfo CustomDepth = AccessTools.Field(typeof(HDRenderPipeline), "m_CustomPassDepthBuffer");
        private static readonly MethodInfo ExecuteInternal = AccessTools.Method(typeof(CustomPass), "ExecuteInternal");
        private static readonly MethodInfo RenderSsr = AccessTools.Method(typeof(HDRenderPipeline), "RenderSSR");
        // HDRP's image after its upscaler or TAA, before its post-processing (depth of field, bloom, the tonemapper).
        private static readonly MethodInfo BeforePostProcess = AccessTools.Method(typeof(HDRenderPipeline), "BeforeCustomPostProcessPass");
        private static SeenMeterPass seenPass;
        private static RTHandle seenCopy;
        private static bool seenFailed, seenPending;
        private static int nextSeenFrame, seenRequestFrame;
        // The seen image's median (log2) and the traced light's gain (log2) it was shown with; NaN until measured.
        private static float seenLog2Mid = float.NaN, seenLog2Gain, seenAt = -10f;
        private static float[] seenLuminance;
        private static int captureRecordedFrame = -1;
        private static readonly Type PrepassOutputType = AccessTools.Inner(typeof(HDRenderPipeline), "PrepassOutput");
        private static readonly Type GBufferOutputType = AccessTools.Inner(typeof(HDRenderPipeline), "GBufferOutput");
        private static FieldInfo PrepassField(string name) => PrepassOutputType != null ? AccessTools.Field(PrepassOutputType, name) : null;
        private static readonly FieldInfo PrepassGBuffer = PrepassField("gbuffer"), PrepassDepth = PrepassField("depthBuffer"),
            PrepassNormal = PrepassField("normalBuffer"), PrepassMotion = PrepassField("resolvedMotionVectorsBuffer"),
            PrepassPyramid = PrepassField("depthPyramidTexture"); // (PT-112: may be null in another HDRP; then not written)
        private static readonly FieldInfo GBufferMrt = GBufferOutputType != null ? AccessTools.Field(GBufferOutputType, "mrt") : null;
        private static readonly FieldInfo GBufferCount = GBufferOutputType != null ? AccessTools.Field(GBufferOutputType, "gBufferCount") : null;
        private static readonly Func<HDRenderPipeline, HDCamera, RTHandle> ExposureTexture =
            AccessTools.Method(typeof(HDRenderPipeline), "GetExposureTexture", new[] { typeof(HDCamera) }) is MethodInfo exposure
                ? (Func<HDRenderPipeline, HDCamera, RTHandle>)Delegate.CreateDelegate(typeof(Func<HDRenderPipeline, HDCamera, RTHandle>), exposure) : null;
        private static readonly PropertyInfo SkyManagerProperty = AccessTools.Property(typeof(HDRenderPipeline), "skyManager");
        private static MethodInfo skyReflection;
        private static bool Ready => CustomColor != null && CustomDepth != null && ExecuteInternal != null && RenderSsr != null && PrepassGBuffer != null &&
            PrepassDepth != null && PrepassNormal != null && PrepassMotion != null && GBufferMrt != null && GBufferCount != null;

        internal static void Initialize()
        {
            if (harmony != null || failed) return;
            try
            {
                if (!Ready) throw new MissingMemberException("HDRP's RenderSSR, prepass output or custom pass recording changed in this game build.");
                // Keep this volume disabled: it retains the capture pass, which is invoked explicitly rather
                // than executed by HDRP.
                captureHost = new GameObject("TG Path Tracing capture") { hideFlags = HideFlags.HideAndDontSave };
                captureHost.SetActive(false);
                UnityEngine.Object.DontDestroyOnLoad(captureHost);
                captureVolume = captureHost.AddComponent<CustomPassVolume>();
                captureVolume.isGlobal = false;
                captureVolume.injectionPoint = CustomPassInjectionPoint.BeforePostProcess;
                capturePass = new CapturePass { name = "TG Path Tracing capture", targetColorBuffer = CustomPass.TargetBuffer.None, targetDepthBuffer = CustomPass.TargetBuffer.None };
                // The composite: a global pass HDRP runs for every camera after opaque lighting and the sky.
                compositeHost = new GameObject("TG Path Tracing composite") { hideFlags = HideFlags.HideAndDontSave };
                compositeHost.SetActive(false);
                UnityEngine.Object.DontDestroyOnLoad(compositeHost);
                var composite = compositeHost.AddComponent<CustomPassVolume>();
                composite.isGlobal = true;
                composite.injectionPoint = CustomPassInjectionPoint.AfterOpaqueAndSky;
                composite.customPasses.Add(new CompositePass { name = "TG Path Tracing composite", targetColorBuffer = CustomPass.TargetBuffer.None, targetDepthBuffer = CustomPass.TargetBuffer.None });
                compositeHost.SetActive(true);
                // While tracing, HDRP's own indirect diffuse light and screen-space GI are off, and the
                // screen-space effects whose results the traced light replaces (a volume above the game's and
                // other mods', which use priorities up to 10000). Each component overrides only the parameter it
                // names: Add<T>(true) overrides all of them with HDRP's defaults, and that took the game's fog away
                // while tracing (its Fog component, there for the fog's ambient light, turned the fog off: HDRP's
                // default), put transparent surfaces' SSR at HDRP's default quality and reset reflection probes.
                indirectHost = new GameObject("TG Path Tracing indirect light") { hideFlags = HideFlags.HideAndDontSave };
                indirectHost.SetActive(false);
                UnityEngine.Object.DontDestroyOnLoad(indirectHost);
                indirectVolume = indirectHost.AddComponent<Volume>();
                indirectVolume.isGlobal = true;
                indirectVolume.priority = 1000000f;
                indirectVolume.weight = 1f;
                var profile = ScriptableObject.CreateInstance<VolumeProfile>();
                profile.hideFlags = HideFlags.HideAndDontSave;
                var indirectControl = profile.Add<IndirectLightingController>();
                indirectControl.indirectDiffuseLightingMultiplier.Override(0f);
                profile.Add<GlobalIllumination>().enable.Override(false);
                profile.Add<ScreenSpaceAmbientOcclusion>().intensity.Override(0f);
                profile.Add<ContactShadows>().enable.Override(false);
                profile.Add<ScreenSpaceReflection>().enabled.Override(false); // opaque surfaces': transparent ones keep the game's
                // HDRP's exposure stays the game's own (UpdateTracedGain gives the traced light its own instead), and so does
                // its fog: a Fog override here once dimmed the fog's ambient light where auto exposure brightened a tavern,
                // which HDRP no longer does.
                indirectVolume.sharedProfile = profile;
                // Reflections on what HDRP shades itself, as much as the sky's light reaches the camera (cameraLight): a
                // volume above that one, weighted 1 - cameraLight toward 0, leaves the game's own multiplier times cameraLight.
                scaledHost = new GameObject("TG Path Tracing scaled reflections") { hideFlags = HideFlags.HideAndDontSave };
                scaledHost.SetActive(false);
                UnityEngine.Object.DontDestroyOnLoad(scaledHost);
                scaledVolume = scaledHost.AddComponent<Volume>();
                scaledVolume.isGlobal = true;
                scaledVolume.priority = 1000001f;
                scaledVolume.weight = 0f;
                var scaled = ScriptableObject.CreateInstance<VolumeProfile>();
                scaled.hideFlags = HideFlags.HideAndDontSave;
                scaled.Add<IndirectLightingController>().reflectionLightingMultiplier.Override(0f);
                scaledVolume.sharedProfile = scaled;
                var candidate = new Harmony(PathTracingPlugin.Id);
                try { candidate.Patch(RenderSsr, prefix: new HarmonyMethod(typeof(HdrpPasses), nameof(BeforeSsr))); harmony = candidate; }
                catch { candidate.UnpatchSelf(); throw; }
                // Hair held opaque while tracing (CharacterCapture.HoldOpaque), optional: without it hair keeps the game's state.
                try { if (!CharacterCapture.PatchHair(harmony)) Session.Log.LogWarning("Hair keeps the game's transparency up close in this game build (its HairController.Refresh was not found)."); }
                catch (Exception ex) { Session.Log.LogWarning("Hair keeps the game's transparency up close: " + ex.Message); }
                WaterSurfaceRendering.Patch(harmony);
                // PT-107 on trial: HDRP passes left out of the game's camera while tracing (GamePasses), optional.
                try { GamePasses.Patch(harmony); }
                catch (Exception ex) { Session.Log.LogWarning("The game's passes stay as they are while tracing in this game build: " + ex.Message); }
                // PT-112, the sky, on trial (the hidden "Sky trial"): HDRP's sky passes left out while the tracer draws the sky.
                try { SkyModel.Patch(harmony); }
                catch (Exception ex) { Session.Log.LogWarning("HDRP's sky stays as it is while tracing in this game build (the Sky trial does nothing): " + ex.Message); }
                // Caustics under water: HDRP's own off while tracing, optional (WaterCaustics).
                try { if (!WaterCaustics.Patch(harmony)) Session.Log.LogWarning("HDRP's own water caustics stay while tracing in this game build (its water constants step was not found)."); }
                catch (Exception ex) { Session.Log.LogWarning("HDRP's own water caustics stay while tracing: " + ex.Message); }
                // The seen meter reads the image after fog, before post-processing.
                try
                {
                    if (BeforePostProcess == null) throw new MissingMethodException("HDRenderPipeline.BeforeCustomPostProcessPass");
                    seenPass = new SeenMeterPass { name = "TG Path Tracing seen meter", targetColorBuffer = CustomPass.TargetBuffer.None, targetDepthBuffer = CustomPass.TargetBuffer.None };
                    harmony.Patch(BeforePostProcess, postfix: new HarmonyMethod(typeof(HdrpPasses), nameof(AfterBeforePostProcess)));
                }
                catch (Exception ex) { seenPass = null; Session.Log.LogWarning("The seen meter is unavailable in this game build (HDRP's post-processing step was not found): " + ex.Message); }
                Detail = "Hooked HDRP's RenderSSR (capture, before shadows and lighting) and added the composite after opaque lighting and the sky.";
                Session.Log.LogInfo(Detail);
            }
            catch (Exception ex)
            {
                failed = true;
                Detail = "Could not hook HDRP, so nothing is traced: " + ex.Message;
                Session.Log.LogError(Detail + "\n" + ex);
                Dispose();
            }
        }

        // The traced light replaces the lit color of deferred surfaces but keeps GBuffer3 as their emission, and HDRP
        // writes its baked indirect light (light probes, lightmaps) there too: left on, it was added on top of the
        // traced light, a flat ambient that filled every shadow and lit skin (stored before its color is applied)
        // like a lamp. HDRP's ambient occlusion, contact shadows and screen-space reflections go too, whose results
        // the traced light replaces (ReplacedRows greys their Video rows out).
        internal static void SetIndirectOverride(bool on)
        {
            if (indirectHost != null && indirectHost.activeSelf != on) indirectHost.SetActive(on);
            if (scaledHost != null && scaledHost.activeSelf != on) scaledHost.SetActive(on);
            var settings = Session.Settings;
            if (on)
            {
                // Indoors there is no sky to see (SceneLights.Interior); during a capture it holds (its sky-off shot).
                float measured = SceneLights.Interior ? 0.01f : Session.HaveStatus ? Session.Status.CameraLight : -1f;
                if (!DebugCapture.Running && measured >= 0f && !float.IsNaN(measured) && !float.IsInfinity(measured))
                {
                    float target = Mathf.Clamp(measured, 0.01f, 1f);
                    float rate = 1f - Mathf.Exp(-Mathf.Max(Time.unscaledDeltaTime, 0f) / 0.5f);
                    cameraLight = Mathf.Exp(Mathf.Lerp(Mathf.Log(Mathf.Max(cameraLight, 0.01f)), Mathf.Log(target), rate));
                }
                if (scaledVolume != null) scaledVolume.weight = 1f - cameraLight;
            }
            // Lights left out as placed light nothing HDRP shades itself either (hair, eyes, transparent surfaces) while
            // the traced light replaces HDRP's, and light no fog (SceneLights.Darken): the fog itself stays.
            SceneLights.DarkenPlaced(on);
            GamePasses.Active = on; // PT-112 (A): HDRP draws no opaque object and no decal while the traced light is on screen
            CharacterCapture.HideHairLayers(on);
            CharacterCapture.HoldOpaque(on);
            GlassFinish.HideSeeThrough(on);
            EffectPasses.Hold(on);
        }

        private static int distortionAuditCamera; private static bool distortionAuditEnabled;

        // Forward-drawn surfaces (eyes, hair: HDRP's Eye and Hair shaders draw forward only, outside the composite) take
        // HDRP's baked ambient light from the indirect diffuse multiplier when they are shaded, which the volume above
        // sets to 0 for the GBuffer's sake: they were black in shade. While HDRP records the camera's
        // passes after the GBuffer (RenderSSR, where the capture is recorded), its global constants get the game's own
        // multiplier (its day and night curve): the push RenderShadows records next carries it to the deferred
        // lighting (whose result the composite replaces) and the forward passes. The GBuffer was drawn with the
        // camera's first push, with 0; HDRP fills its constants from the volumes again at the next camera. (A push of
        // our own from the capture pass was overwritten by RenderShadows' before the forward passes drew anything.)
        private static FieldInfo globalConstantsField, multiplierField;
        // The share of the game's ambient light the forward passes keep this frame (1 until it is lowered).
        internal static float ForwardAmbientKept = 1f;
        private static readonly int GlobalSaturation = Shader.PropertyToID("_GlobalSaturationAdjustment");
        private static bool forwardAmbientFailed, forwardAmbientLogged;
        private static IndirectLightingController gameIndirect;
        private static float nextIndirectLookup;
        private static readonly FieldInfo DayNightIndirect = typeof(Awaken.TG.Graphics.DayNightSystem.DayNightSystem)
            .GetField("_indirectLightingController", BindingFlags.NonPublic | BindingFlags.Instance);
        private static void RaiseForwardAmbient(HDRenderPipeline pipeline)
        {
            if (forwardAmbientFailed || pipeline == null) return;
            try
            {
                if (globalConstantsField == null)
                {
                    globalConstantsField = typeof(HDRenderPipeline).GetField("m_ShaderVariablesGlobalCB", BindingFlags.NonPublic | BindingFlags.Instance);
                    if (globalConstantsField == null) throw new MissingFieldException("HDRenderPipeline.m_ShaderVariablesGlobalCB");
                    multiplierField = globalConstantsField.FieldType.GetField("_IndirectDiffuseLightingMultiplier", BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance);
                    if (multiplierField == null) throw new MissingFieldException("ShaderVariablesGlobal._IndirectDiffuseLightingMultiplier");
                }
                float value = GameIndirectMultiplier() * cameraLight; // as much of the sky's light as reaches the camera
                ForwardAmbientKept = cameraLight;
                object constants = globalConstantsField.GetValue(pipeline); // boxed
                multiplierField.SetValue(constants, value);
                globalConstantsField.SetValue(pipeline, constants);
                if (!forwardAmbientLogged)
                {
                    forwardAmbientLogged = true;
                    Session.Log.LogInfo("Forward-drawn surfaces (eyes, hair) take the game's ambient light (indirect multiplier " + value.ToString("0.00") + (gameIndirect != null ? ", its day and night curve" : ", the default") + ")");
                }
            }
            catch (Exception ex)
            {
                forwardAmbientFailed = true;
                Session.Log.LogWarning("Forward-drawn surfaces (eyes, hair) keep no ambient light while tracing: " + (ex.InnerException ?? ex).Message);
            }
        }
        private static float GameIndirectMultiplier()
        {
            if (gameIndirect == null && Time.unscaledTime >= nextIndirectLookup)
            {
                nextIndirectLookup = Time.unscaledTime + 5f;
                var system = UnityEngine.Object.FindFirstObjectByType<Awaken.TG.Graphics.DayNightSystem.DayNightSystem>();
                gameIndirect = system != null ? DayNightIndirect?.GetValue(system) as IndirectLightingController : null;
            }
            return gameIndirect != null ? Mathf.Max(0f, gameIndirect.indirectDiffuseLightingMultiplier.value) : 1f;
        }

        // HDRP's height fog for the camera (its Fog volume, which this plugin never overrides), as HDRP lays it out: extinction
        // 1 / the attenuation distance below the base height, falling off above it with a scale height of the layer's depth
        // times 0.144765 (a thousandth of it left at the maximum height). The native side fades the traced light's automatic
        // gain with it (traced_gain_at); the player's Exposure goes along as a factor. And the distance HDRP lays its fog over
        // the sky at (the volume's Max Fog Distance), for Fog Adaptation on the fog in front of the sky. And its color as
        // HDRP's GetFogColor takes it (PT-37: the fog along reflected and refracted rays): what it scatters (_HeightFogBaseScattering
        // over its extinction: the volume's albedo, as it is, while its volumetric fog is on, else 1) times its color (the tint
        // in the Sky colour mode, which HDRP multiplies by its sky, else the constant color), and its mip fog.
        private static bool fogFailed, fogColorFailed;
        internal static float FogExtinction, FogBaseHeight, FogScaleHeight;
        private static unsafe void FillFog(HDCamera hd, Settings settings, ref NativeFrame frame)
        {
            frame.Fog[3] = Mathf.Pow(2f, Mathf.Clamp(settings.Exposure.Value, -3f, 3f));
            frame.Forward[2] = 0f;
            FogExtinction = 0f;
            if (fogFailed) return;
            try
            {
                var fog = hd.volumeStack?.GetComponent<Fog>();
                if (fog == null || !fog.enabled.value || !(fog.meanFreePath.value > 0f)) return;
                float layer = Mathf.Max(0.01f, fog.maximumHeight.value - fog.baseHeight.value);
                FogExtinction = 1f / fog.meanFreePath.value; FogBaseHeight = fog.baseHeight.value; FogScaleHeight = layer * 0.144765f;
                frame.Fog[0] = FogExtinction; frame.Fog[1] = FogBaseHeight; frame.Fog[2] = 1f / FogScaleHeight;
                frame.Forward[2] = Mathf.Max(0f, fog.maxFogDistance.value);
                if (!fogColorFailed)
                {
                    try
                    {
                        bool skyColor = fog.colorMode.value == FogColorMode.SkyColor;
                        bool volumetric = fog.enableVolumetricFog.value && GamePasses.GameVolumetrics; // (the game's own setting, whatever HDRP draws)
                        Color color = skyColor ? fog.tint.value : fog.color.value;
                        Color albedo = volumetric ? fog.albedo.value : Color.white;
                        frame.FogColor[0] = color.r * albedo.r; frame.FogColor[1] = color.g * albedo.g; frame.FogColor[2] = color.b * albedo.b;
                        frame.FogColor[3] = skyColor ? 1f : 0f;
                        frame.FogMip[0] = fog.mipFogNear.value; frame.FogMip[1] = fog.mipFogFar.value; frame.FogMip[2] = fog.mipFogMaxMip.value;
                        // PT-112 B: the fog as a medium for the traced fog: what it scatters (no tint: that is HDRP's far fog's colour)
                        // and its anisotropy, as HDRP's volumetric fog lights it.
                        frame.FogMedium[0] = albedo.r; frame.FogMedium[1] = albedo.g; frame.FogMedium[2] = albedo.b;
                        frame.FogMedium[3] = Mathf.Clamp(fog.anisotropy.value, -0.99f, 0.99f);
                    }
                    catch (Exception ex)
                    {
                        fogColorFailed = true;
                        frame.FogColor[0] = frame.FogColor[1] = frame.FogColor[2] = frame.FogColor[3] = 0f;
                        Session.Log.LogWarning("HDRP's fog color could not be read, so reflections show no fog's light: " + ex.Message);
                    }
                }
            }
            catch (Exception ex)
            {
                fogFailed = true;
                Session.Log.LogWarning("HDRP's fog could not be read, so the traced light's exposure ignores it: " + ex.Message);
            }
        }

        internal static bool IsGameplayCamera(HDCamera hd)
        {
            var camera = hd?.camera;
            var main = Camera.main;
            return camera != null && main != null && camera.GetInstanceID() == main.GetInstanceID() && camera.cameraType == CameraType.Game && camera.targetTexture == null;
        }

        private static void Report()
        {
            float now = Time.unscaledTime;
            if (now < nextReport) return;
            bool first = nextReport == 0f;
            nextReport = now + 10f;
            if (first) return;
            Session.Log.LogInfo("Path tracing hook 10s: recorded " + recorded + ", executed " + executed + ", handed to the native side " + submitted +
                "; skipped: other camera " + skippedCamera + ", mode off or not ready " + skippedMode + ", not deferred " + skippedLit +
                " | composited " + Composited + " | last: " + LastFrame + " | " + WaterText);
            // The yardstick (PT-59): where the tracer's own first hit is not what the game drew, with the materials' names.
            // (Not while HDRP draws nothing under the traced picture, PT-112 A: its G-buffer is empty, the numbers say nothing.)
            string yardstick = GamePasses.OwnDepth ? "" : SceneCapture.YardstickText(Session.Tracer);
            if (yardstick.Length > 0) Session.Log.LogInfo("Yardstick 10s (the tracer's own first hit against the game's G-buffer; the picture from " +
                (Session.Settings.OwnCameraRays.Value ? "the tracer's own camera rays" : "the game's G-buffer") + "): " + yardstick);
            recorded = executed = submitted = skippedCamera = skippedMode = skippedLit = 0;
        }

        // HDRP is about to run its opaque SSR step for a camera; the GBuffer is complete. Records the capture
        // pass for the gameplay camera. Always lets RenderSSR run. The transparent call (from RenderTransparency,
        // every frame) comes later in the same camera's recording, before the graph executes: it must leave the
        // recorded capture armed.
        private static void BeforeSsr(HDRenderPipeline __instance, RenderGraph renderGraph, HDCamera hdCamera, bool transparent, object[] __args)
        {
            if (transparent || hdCamera == null || failed) return;
            if (armed && ++unexecuted >= 5) Fail("HDRP's render graph did not run the capture pass it recorded, five frames in a row.");
            armed = false;
            try
            {
                Report();
                if (!IsGameplayCamera(hdCamera)) { ++skippedCamera; return; }
                if (!Session.WantsFrames) { ++skippedMode; return; }
                if (hdCamera.frameSettings.litShaderMode != LitShaderMode.Deferred) { ++skippedLit; return; }
                bool distortionOn = hdCamera.frameSettings.IsEnabled(FrameSettingsField.Distortion);
                int distortionId = hdCamera.camera.GetInstanceID();
                if (distortionAuditCamera != distortionId || distortionAuditEnabled != distortionOn)
                {
                    distortionAuditCamera = distortionId; distortionAuditEnabled = distortionOn;
                    Session.Log.LogInfo("Traced camera frame settings: Distortion=" + distortionOn);
                }
                object prepass = __args[2];
                object gbuffer = PrepassGBuffer.GetValue(prepass);
                var mrt = (TextureHandle[])GBufferMrt.GetValue(gbuffer);
                int count = (int)GBufferCount.GetValue(gbuffer);
                if (mrt == null || count < 3) { ++skippedLit; return; }
                pendingGBuffer2 = mrt[2];
                hasGBuffer3 = count >= 4 && mrt[3].IsValid();
                pendingGBuffer3 = hasGBuffer3 ? mrt[3] : TextureHandle.nullHandle;
                pendingPyramid = PrepassPyramid != null ? (TextureHandle)PrepassPyramid.GetValue(prepass) : TextureHandle.nullHandle;
                var targets = new CustomPass.RenderTargets
                {
                    colorBufferRG = mrt[0],
                    depthBufferRG = (TextureHandle)PrepassDepth.GetValue(prepass),
                    normalBufferRG = (TextureHandle)PrepassNormal.GetValue(prepass),
                    motionVectorBufferRG = (TextureHandle)PrepassMotion.GetValue(prepass),
                    customColorBuffer = CustomColor.GetValue(__instance) as Lazy<RTHandle>,
                    customDepthBuffer = CustomDepth.GetValue(__instance) as Lazy<RTHandle>
                };
                armed = true;
                armedView = hdCamera.mainViewConstants; // now, while they hold the frame's sub-pixel shift (see armedView)
                ExecuteInternal.Invoke(capturePass, new object[] { renderGraph, hdCamera, default(CullingResults), default(CullingResults), targets, captureVolume });
                ++recorded; captureRecordedFrame = Time.frameCount;
                RaiseForwardAmbient(__instance);
            }
            catch (Exception ex)
            {
                armed = false;
                Fail("Recording the path tracing capture failed: " + ex);
            }
        }

        // HDRP's exposure (a GPU readback a frame or two old), for the log only: the tracer's radiance clamps are measured
        // against the scene's lights, never against the exposure (pt_restir.hlsli).
        private static readonly Action<HDCamera, RTHandle> RequestExposure =
            AccessTools.Method(typeof(HDCamera), "RequestGpuExposureValue", new[] { typeof(RTHandle) }) is MethodInfo request
                ? (Action<HDCamera, RTHandle>)Delegate.CreateDelegate(typeof(Action<HDCamera, RTHandle>), request) : null;
        private static readonly Func<HDCamera, float> ExposureValue =
            AccessTools.Method(typeof(HDCamera), "GpuExposureValue", Type.EmptyTypes) is MethodInfo value
                ? (Func<HDCamera, float>)Delegate.CreateDelegate(typeof(Func<HDCamera, float>), value) : null;
        // For the log: HDRP's current exposure, as log2 of the exposure multiplier (higher is brighter: a darker view), and
        // the game's exposure setup whenever it changes (auto exposure follows the view).
        internal static float LiveExposureLog = float.NaN;

        // The traced light's own exposure. The game's exposure (a fixed EV 14 in a tavern, a physical camera outdoors) is
        // set for its fill lights and baked light, which the tracer leaves out: with only the real sources lit, interiors
        // came out nearly black. HDRP's auto exposure metering the traced image brightened them (to EV 2, then 5, then
        // 7 at the least), but everything HDRP draws itself was made for the game's exposure and blew out with it:
        // flames, fire, glows, fog lit by lamps, hair in a dialogue light, the sky through a door. HDRP keeps the game's
        // exposure now, and the traced light alone gets a gain on top of it, whose automatic part is never below 1 (only the
        // player's Exposure lowers it): metering sunny exteriors down to a mid tone left them underexposed (at half to four
        // fifths of the game's exposure). Where the traced light is
        // darker than a mid tone (its median pixel, pt_meter.hlsl: centre-weighted, everything the tracer lights, the unlit
        // included), it is brightened part of the way there, as far as Dark Adaptation sets (the eye adapts to the dark
        // only in part: fully adapted, a candle-lit tavern looked nearly as bright as day, and at 70 % still a little too
        // bright), never so far that the brightest fiftieth passes 1.4 times white (the 90th percentile did not hold: in a
        // tavern lit by candles most of the frame is dark, and a lit table past it went white; white itself left a shaded
        // stall black beside a sunlit floor), and at most as far as the darkest scene EV the config sets (7 by default)
        // below the game's exposure. Exposure adds the player's own offset. It adapts as an eye does: darker quickly when
        // the view brightens, brighter over a few seconds in the dark.
        // The automatic part is the gain of what is dark. Each pixel takes it by how bright its own traced light already is
        // (pt_common.hlsli highlight_hold, PT-73 e): all of it in the dark, none from the brightness the game's tone mapping
        // shows at 0.4 of white, where the picture keeps the game's exposure. One gain for the whole picture burned the wall
        // beside a lantern out to white while it lifted the room (x5.3 in a tavern). The meter takes that back out too
        // (traced_ungained), so the numbers below are the light at the game's exposure, as before.
        private const float MidTone = -3.06f;   // log2 0.12: HDRP's auto exposure puts a scene's average near there
        private const float Highlight = 0.5f;   // log2 1.4: the brightest fiftieth (the 98th percentile) at most 1.4 times white
        private static float tracedLog2Gain;
        internal static float TracedGain => Mathf.Pow(2f, tracedLog2Gain);
        // The traced light's automatic brightening taken back out (the player's Exposure left in), for a light a mod places
        // for a look it set at the game's exposure (SceneLights: the dialogue light). At most 1.
        // The brightening holds back where a pixel is bright (pt_common.hlsli highlight_hold), so no one factor takes it out
        // of all such a light lights. This is the factor for a mid tone (0.18, a third of HIGHLIGHT_KEPT: a face lit for a
        // look): the curve solved backwards there (traced_ungained). What the light leaves darker than that comes out a
        // little brighter than the mod set it, what it lights brighter a little darker (about a fifth at half and at twice
        // the mid tone, at x3.1). 1 / the gain, as it was until 2026-10-08, would now leave a lit face a tenth to a half
        // too dark, and no factor at all a third to twice too bright. (PT-17: not yet seen in a dialogue.)
        internal static float AutomaticGainInverse
        {
            get
            {
                var settings = Session.Settings;
                float player = settings != null ? Mathf.Clamp(settings.Exposure.Value, -3f, 3f) : 0f;
                float automatic = Mathf.Pow(2f, tracedLog2Gain - player);
                if (!(automatic > 1f)) return 1f;
                const float midTone = 1f / 3f; // of HIGHLIGHT_KEPT
                float a = (1f - midTone) * (automatic - 1f);
                return 2f / (a + 1f + Mathf.Sqrt(Mathf.Max((a + 1f) * (a + 1f) - 4f * a * midTone, 0f)));
            }
        }
        // The gain's automatic part in EV (without the player's Exposure); NaN until the meter has reported.
        private static float AutomaticGain(Settings settings)
        {
            if (!Session.HaveStatus || Session.Status.TracedSamples == 0) return float.NaN;
            float mid = Session.Status.TracedLog2Mid, high = Session.Status.TracedLog2High;
            if (float.IsNaN(mid) || float.IsInfinity(mid) || float.IsNaN(high) || float.IsInfinity(high)) return float.NaN;
            // What is seen counts too: HDRP's fog over the traced light and what HDRP draws itself. A view the mist fills is
            // not a dark one: brightened as one, the rocks and walls the game's fog hides at night came out four times as
            // bright against the fog, which did not change, and showed through it. The seen median with the traced light's
            // gain taken out, as if all of it were traced light (so at most the view's own), where that is brighter than
            // the traced light's median: without fog the two are the same.
            if (!float.IsNaN(seenLog2Mid) && Time.unscaledTime - seenAt < 1f) mid = Mathf.Max(mid, seenLog2Mid - seenLog2Gain);
            float adaptation = Mathf.Clamp(settings.DarkAdaptation.Value, 0, 100) / 100f;
            float brighten = Mathf.Max(0f, Mathf.Min((MidTone - mid) * adaptation, Highlight - high));
            // As far as the darkest scene EV below the game's exposure (HDRP's exposure is 1 / (1.2 * 2^EV)).
            float gameEv = float.IsNaN(LiveExposureLog) ? 13f : -LiveExposureLog - Mathf.Log(1.2f, 2f);
            float most = Mathf.Clamp(gameEv - settings.ExposureLowest.Value, 0f, 12f);
            return Mathf.Min(brighten, most);
        }
        private static float TracedGainTarget(Settings settings)
        {
            float automatic = AutomaticGain(settings);
            return float.IsNaN(automatic) ? tracedLog2Gain : automatic + Mathf.Clamp(settings.Exposure.Value, -3f, 3f);
        }

        private static void AfterBeforePostProcess(HDRenderPipeline __instance, RenderGraph renderGraph, HDCamera hdCamera, ref TextureHandle __result)
        {
            bool meter = !seenFailed && seenPass != null;
            if (failed || !meter || hdCamera == null || !__result.IsValid()) return;
            if (captureRecordedFrame != Time.frameCount || !IsGameplayCamera(hdCamera) || !Session.WantsFrames) return;
            var targets = new CustomPass.RenderTargets
            {
                colorBufferRG = __result,
                customColorBuffer = CustomColor.GetValue(__instance) as Lazy<RTHandle>,
                customDepthBuffer = CustomDepth.GetValue(__instance) as Lazy<RTHandle>
            };
            // Measure the traced image before post-processing. Every fourth frame, one
            // readback at a time (a lost one is given up after two seconds).
            if (meter && seenPending && Time.frameCount - seenRequestFrame > 120) seenPending = false;
            if (meter && !seenPending && Time.frameCount >= nextSeenFrame)
            {
                try { ExecuteInternal.Invoke(seenPass, new object[] { renderGraph, hdCamera, default(CullingResults), default(CullingResults), targets, captureVolume }); nextSeenFrame = Time.frameCount + 4; }
                catch (Exception ex) { seenFailed = true; Session.Log.LogWarning("The traced light's exposure no longer measures HDRP's fog this session: " + (ex.InnerException ?? ex).Message); }
            }
        }
        // A 128x72 copy of the image (HDRP's own copy, which knows its render target's scale), read back without waiting.
        internal static void MeasureSeen(CustomPassContext ctx)
        {
            try
            {
                if (seenCopy == null)
                {
                    var texture = new RenderTexture(128, 72, 0, RenderTextureFormat.ARGBHalf, RenderTextureReadWrite.Linear)
                        { name = "TG Path Tracing seen meter", filterMode = FilterMode.Bilinear, hideFlags = HideFlags.HideAndDontSave };
                    texture.Create();
                    seenCopy = RTHandles.Alloc(texture, transferOwnership: true);
                }
                CustomPassUtils.Copy(ctx, ctx.cameraColorBuffer, seenCopy);
                float gain = tracedLog2Gain;
                seenPending = true; seenRequestFrame = Time.frameCount;
                ctx.cmd.RequestAsyncReadback(seenCopy.rt, request => OnSeen(request, gain));
            }
            catch (Exception ex) { seenFailed = true; seenPending = false; Session.Log.LogWarning("The traced light's exposure no longer measures HDRP's fog this session: " + ex.Message); }
        }
        // The median luminance of the copy, black counted as the traced light's meter counts it (its lowest bin, 2^-16).
        private static void OnSeen(AsyncGPUReadbackRequest request, float gain)
        {
            seenPending = false;
            try
            {
                if (request.hasError || !request.done) return;
                var data = request.GetData<ushort>();
                int pixels = data.Length / 4, count = 0;
                if (seenLuminance == null || seenLuminance.Length != pixels) seenLuminance = new float[pixels];
                for (int i = 0; i < pixels; i++)
                {
                    float y = 0.2126f * Mathf.HalfToFloat(data[4 * i]) + 0.7152f * Mathf.HalfToFloat(data[4 * i + 1]) + 0.0722f * Mathf.HalfToFloat(data[4 * i + 2]);
                    if (float.IsNaN(y) || float.IsInfinity(y)) continue;
                    seenLuminance[count++] = Mathf.Max(y, 1.52587891e-5f);
                }
                if (count < pixels / 2) return;
                Array.Sort(seenLuminance, 0, count);
                seenLog2Mid = Mathf.Log(seenLuminance[count / 2], 2f);
                seenLog2Gain = gain; seenAt = Time.unscaledTime;
            }
            catch (Exception ex) { seenFailed = true; Session.Log.LogWarning("The traced light's exposure no longer measures HDRP's fog this session: " + ex.Message); }
        }
        private static void UpdateTracedGain(Settings settings)
        {
            // A debug view shows other things than the traced light, and a capture compares images: the gain holds.
            if (settings.CurrentView != DebugView.None || DebugCapture.Running) return;
            float target = TracedGainTarget(settings);
            float dt = Mathf.Clamp(Time.unscaledDeltaTime, 0f, 0.25f);
            tracedLog2Gain = Mathf.MoveTowards(tracedLog2Gain, target, (target > tracedLog2Gain ? 1.5f : 3f) * dt);
        }
        private static float exposureMin = float.PositiveInfinity, exposureMax = float.NegativeInfinity;
        // Read by the 10 s report, which starts the next window: the range the exposure moved through in it.
        internal static string ExposureText
        {
            get
            {
                if (float.IsNaN(LiveExposureLog)) return "exposure unknown";
                string text = "exposure log2 " + LiveExposureLog.ToString("0.00") + " now, " + exposureMin.ToString("0.00") + " to " + exposureMax.ToString("0.00") +
                    " in the last 10 s, traced light x" + TracedGain.ToString("0.0") + " where it is dark, less where bright (" + tracedLog2Gain.ToString("0.0") + " EV" +
                    (Session.HaveStatus && Session.Status.TracedSamples > 0 ? "; its median log2 " + Session.Status.TracedLog2Mid.ToString("0.0") +
                        ", 98th percentile " + Session.Status.TracedLog2High.ToString("0.0") : "") +
                    (!float.IsNaN(seenLog2Mid) && Time.unscaledTime - seenAt < 1f ? "; seen with the fog: median log2 " + seenLog2Mid.ToString("0.0") +
                        ", " + (seenLog2Mid - seenLog2Gain).ToString("0.0") + " without the gain" : "; seen with the fog: not measured") +
                    "), light at the camera " + cameraLight.ToString("0.00") + " of the open sky's";
                exposureMin = float.PositiveInfinity; exposureMax = float.NegativeInfinity;
                return text;
            }
        }
        // HDRP's diffusion profiles for solid surfaces, a bit per profile index (the one in its GBuffer): the tracer lights
        // their surfaces as solid, where transmission otherwise means a thin surface lit through from behind, as leaves and
        // paper are. A profile is solid when HDRP transmits through it as a thick object, or when it is skin: this game
        // sets DiffusionProfile_Human_Skin to Thin Object, as it does its foliage. The index is the profile's place in the
        // volume stack's DiffusionProfileList, HDRP's default profile at 0 (HDRenderPipeline sets profile i from its
        // value[i]); the array is refilled each frame, so the answer is kept per profile and the list logged when it changes.
        private static readonly Dictionary<DiffusionProfileSettings, bool> thickProfile = new Dictionary<DiffusionProfileSettings, bool>();
        private static FieldInfo profileField, transmissionModeField;
        private static uint thickProfiles = uint.MaxValue;
        internal static string DiffusionProfilesText { get; private set; } = "not read yet";
        private static bool IsThick(DiffusionProfileSettings settings)
        {
            if (thickProfile.TryGetValue(settings, out bool thick)) return thick;
            // DiffusionProfileSettings.profile and its DiffusionProfile are internal; transmissionMode is 0 for Regular (thick).
            profileField = profileField ?? typeof(DiffusionProfileSettings).GetField("profile", BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public);
            object profile = profileField?.GetValue(settings);
            transmissionModeField = transmissionModeField ?? profile?.GetType().GetField("transmissionMode", BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
            object mode = profile != null ? transmissionModeField?.GetValue(profile) : null;
            thick = (mode != null && Convert.ToUInt32(mode) == 0u) || settings.name.IndexOf("skin", StringComparison.OrdinalIgnoreCase) >= 0;
            thickProfile[settings] = thick;
            return thick;
        }
        // How far light scatters inside a profile's material, from HDRP's own values for its subsurface pass: Burley's shape
        // per channel (per millimetre) and the filter radius (millimetres), in the world's metres by the profile's world scale
        // (metres per unit). Kept per profile: the medium of the walk inside a material that names it (ReadSubsurface).
        private static readonly Dictionary<DiffusionProfileSettings, Vector4> scattering = new Dictionary<DiffusionProfileSettings, Vector4>();
        private static PropertyInfo shapeProperty, radiusProperty;
        private static FieldInfo worldScaleField;
        private static bool scatterFailed;
        private static Vector4 ScatteringOf(DiffusionProfileSettings settings)
        {
            if (scattering.TryGetValue(settings, out var v)) return v;
            v = Vector4.zero;
            try
            {
                profileField = profileField ?? typeof(DiffusionProfileSettings).GetField("profile", BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public);
                object profile = profileField?.GetValue(settings);
                if (profile != null)
                {
                    var type = profile.GetType();
                    const BindingFlags any = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
                    shapeProperty = shapeProperty ?? type.GetProperty("shapeParam", any);
                    radiusProperty = radiusProperty ?? type.GetProperty("filterRadius", any);
                    worldScaleField = worldScaleField ?? type.GetField("worldScale", any);
                    var shape = (Vector3)shapeProperty.GetValue(profile);
                    float radius = (float)radiusProperty.GetValue(profile);
                    float mmPerUnit = 1000f * (worldScaleField != null ? Mathf.Max((float)worldScaleField.GetValue(profile), 1e-3f) : 1f);
                    if (radius > 0f && shape.x > 0f && shape.y > 0f && shape.z > 0f)
                        v = new Vector4(shape.x * mmPerUnit, shape.y * mmPerUnit, shape.z * mmPerUnit, radius / mmPerUnit);
                }
            }
            catch (Exception ex)
            {
                if (!scatterFailed) { scatterFailed = true; Session.Log.LogWarning("HDRP's diffusion profiles' scattering could not be read (skin is lit without it): " + (ex.InnerException ?? ex).Message); }
            }
            scattering[settings] = v;
            return v;
        }
        // The constants HDRP's own shaders transmit light through a profile with, as its DiffusionProfileSettings keeps them for
        // them (UpdateCache): the tint / 4 (and the profile's Fresnel0), Burley's shape per millimetre and the thickness remap.
        // What a leaf's material lets through from behind is worked out from them (ReadSubsurface).
        private static readonly Dictionary<DiffusionProfileSettings, (Vector4 tint, Vector4 shape, Vector4 remap)> transmission =
            new Dictionary<DiffusionProfileSettings, (Vector4, Vector4, Vector4)>();
        private static FieldInfo tintField, shapeField, remapField;
        private static bool transmissionFailed;
        // A mirror reflectance per profile (TGPT_SetProfileReflectance), sent when it changes.
        private static readonly float[] reflectanceTable = new float[16], reflectanceSent = new float[16];
        private static bool reflectanceKnown;
        private static (Vector4 tint, Vector4 shape, Vector4 remap) TransmissionOf(DiffusionProfileSettings settings)
        {
            if (transmission.TryGetValue(settings, out var v)) return v;
            v = default;
            try
            {
                const BindingFlags any = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
                tintField = tintField ?? typeof(DiffusionProfileSettings).GetField("transmissionTintAndFresnel0", any);
                shapeField = shapeField ?? typeof(DiffusionProfileSettings).GetField("shapeParamAndMaxScatterDist", any);
                remapField = remapField ?? typeof(DiffusionProfileSettings).GetField("worldScaleAndFilterRadiusAndThicknessRemap", any);
                v = ((Vector4)tintField.GetValue(settings), (Vector4)shapeField.GetValue(settings), (Vector4)remapField.GetValue(settings));
            }
            catch (Exception ex)
            {
                if (!transmissionFailed) { transmissionFailed = true; Session.Log.LogWarning("HDRP's diffusion profiles' transmission could not be read (no light passes through skin): " + (ex.InnerException ?? ex).Message); }
            }
            transmission[settings] = v;
            return v;
        }
        // PT-40: the diffusion profile a material names, for the light inside it where a path meets it. HDRP's materials keep
        // the profile's hash in _DiffusionProfileHash (a float holding the hash's bits); a Shader Graph names the property itself,
        // so any float of the shader whose bits are a loaded profile's hash is taken for it (remembered per shader). The profiles:
        // every DiffusionProfileSettings loaded, listed again when a material names one not seen yet (at most every ten seconds).
        private static readonly Dictionary<uint, DiffusionProfileSettings> profilesByHash = new Dictionary<uint, DiffusionProfileSettings>();
        private static readonly Dictionary<Shader, int> profileProperty = new Dictionary<Shader, int>();
        private static FieldInfo hashField;
        private static float profilesListedAt = -1f;
        private static bool subsurfaceFailed;
        private static void ListProfiles()
        {
            profilesByHash.Clear();
            const BindingFlags any = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
            foreach (var settings in Resources.FindObjectsOfTypeAll<DiffusionProfileSettings>())
            {
                if (settings == null) continue;
                profileField = profileField ?? typeof(DiffusionProfileSettings).GetField("profile", any);
                object profile = profileField?.GetValue(settings);
                if (profile == null) continue;
                hashField = hashField ?? profile.GetType().GetField("hash", any);
                if (hashField == null) continue;
                uint hash = Convert.ToUInt32(hashField.GetValue(profile));
                if (hash != 0u) profilesByHash[hash] = settings;
            }
            profilesListedAt = Time.realtimeSinceStartup;
        }
        private static uint FloatBits(float v) => BitConverter.ToUInt32(BitConverter.GetBytes(v), 0);
        internal static DiffusionProfileSettings ProfileOf(Material m)
        {
            if (m == null || m.shader == null) return null;
            if (profilesListedAt < 0f) ListProfiles();
            for (int attempt = 0; attempt < 2; attempt++)
            {
                if (profileProperty.TryGetValue(m.shader, out int known))
                {
                    if (m.HasProperty(known) && profilesByHash.TryGetValue(FloatBits(m.GetFloat(known)), out var named)) return named;
                }
                else if (m.HasProperty("_DiffusionProfileHash"))
                {
                    if (profilesByHash.TryGetValue(FloatBits(m.GetFloat("_DiffusionProfileHash")), out var named))
                    {
                        profileProperty[m.shader] = Shader.PropertyToID("_DiffusionProfileHash");
                        return named;
                    }
                }
                else
                {
                    int count = m.shader.GetPropertyCount();
                    for (int i = 0; i < count; i++)
                    {
                        var type = m.shader.GetPropertyType(i);
                        if (type != UnityEngine.Rendering.ShaderPropertyType.Float && type != UnityEngine.Rendering.ShaderPropertyType.Range) continue;
                        int id = m.shader.GetPropertyNameId(i);
                        if (!profilesByHash.TryGetValue(FloatBits(m.GetFloat(id)), out var named)) continue;
                        profileProperty[m.shader] = id;
                        return named;
                    }
                }
                if (Time.realtimeSinceStartup - profilesListedAt < 10f) break;
                ListProfiles();
            }
            return null;
        }
        // Whether a material has one of its shader's keywords on: declared by the shader (a keyword a material carries that
        // its shader does not know picks no variant) and enabled on the material.
        private static bool KeywordOn(Material m, string name)
        {
            try
            {
                var keyword = m.shader.keywordSpace.FindKeyword(name);
                return keyword.isValid && m.IsKeywordEnabled(keyword);
            }
            catch (Exception) { return m.IsKeywordEnabled(name); }
        }
        // PT-40: the game's subsurface mask for a material whose subsurface feature is on, as its shader computes it (each
        // read from the compiled GBuffer pass of its subsurface variant; docs/agents/who-walks.md): the share of the surface's
        // diffuse light that scatters under it. from: where the number comes from, for the log. unread: its shader computes
        // the mask per point from things the tracer does not read yet (the snow shaders' snow amount).
        private static float OwnMask(Material m, string shaderName, out string from, out bool unread)
        {
            unread = false;
            switch (shaderName)
            {
                case "HDRP/Lit":
                {
                    float mask = m.HasProperty("_SubsurfaceMask") ? m.GetFloat("_SubsurfaceMask") : 1f;
                    bool map = KeywordOn(m, "_SUBSURFACE_MASK_MAP") && m.HasProperty("_SubsurfaceMaskMap") && m.GetTexture("_SubsurfaceMaskMap") != null;
                    from = "its _SubsurfaceMask" + (map ? "; its subsurface mask map is NOT read (none of the game's scattering materials had one: a gap if this one is seen)" : "");
                    return mask;
                }
                case "TG/Character/Kandra_Lit":
                    from = "its shader's constant"; return 1f;
                case "TG/Environment/MeshTerrain":
                    from = "its shader's constant: the game's ground does not scatter"; return 0f;
                case "TG/Environment/IceLake":
                    from = "its shader's constant"; return 1f;
                case "TG/Character/RealSkin":
                {
                    float scale = (m.HasProperty("_SubsurfaceScale") ? m.GetFloat("_SubsurfaceScale") : 1f) * (m.HasProperty("_UnmaskedScatterScale") ? m.GetFloat("_UnmaskedScatterScale") : 1f);
                    from = "its _SubsurfaceScale, until its program is open: then per point, times its subsurface map and its regions' scatter scales";
                    return scale;
                }
                case "TG/Character/RealSkin_Morph":
                {
                    float scale = (m.HasProperty("_SubsurfaceScale") ? m.GetFloat("_SubsurfaceScale") : 1f) * (m.HasProperty("_UnmaskedScatterScale") ? m.GetFloat("_UnmaskedScatterScale") : 1f);
                    from = "its _SubsurfaceScale; its subsurface map and regions are NOT read (this shader has no program in the tracer: a gap)";
                    return scale;
                }
                case "TG/Environment/Snow/Snow":
                case "TG/Environment/Cliffs/Cliff_Snow":
                case "TG/Dissolve/DissolveWithSnow":
                    unread = true;
                    from = "its shader computes the mask per point (where its snow lies), which the tracer does not read";
                    return 0f;
                default:
                    from = "a shader the tracer has not read the mask of: 1, a Shader Graph's default";
                    return 1f;
            }
        }
        // One line a material (by its name and shader): why light walks inside it, or why not.
        private static readonly HashSet<string> walkLogged = new HashSet<string>();
        private static void WalkLog(Material m, string shaderName, string why)
        {
            if (!walkLogged.Add(m.name + "|" + shaderName + "|" + why)) return;
            Session.Log.LogInfo("Subsurface material \"" + m.name + "\" (" + shaderName + "): " + why);
        }
        // A traced material's light inside it (TGPT_Material.subsurface), from the diffusion profile it names.
        //   A leaf or a blade (MaterialTranslucent, set before this): the share of the light from behind HDRP lets through it per
        //   channel, its profile's transmittance at the material's thickness times its transmission mask (Lit's
        //   FillMaterialTransmission: tint / 4 x (e^-St + 3 e^-St/3)), with the material's _Thickness (the middle of its
        //   _ThicknessRemap where it has a thickness map, which a path can't read) and _TransmissionMask, 1 where it has none.
        //   A thing the game's own shader scatters light under the surface of (its subsurface feature on; not cloth, glass or
        //   hair): MaterialSubsurface, Burley's shape per metre of the world (the walk's medium) and its subsurface mask (how
        //   much of its light walks). Drawn from both sides, it walks where its mesh closes a volume: the tracer finds that.
        internal static void ReadSubsurface(Material m, string shaderName, ref NativeMaterial mat)
        {
            try
            {
                var settings = ProfileOf(m);
                if (settings == null)
                {
                    if ((mat.Flags & Tgpt.MaterialTranslucent) == 0 && KeywordOn(m, "_MATERIAL_FEATURE_SUBSURFACE_SCATTERING"))
                        WalkLog(m, shaderName, "its subsurface feature is on, but it names no diffusion profile that is loaded (HDRP's default one would serve, which is not read): NOT walked, lit as a plain surface (a gap)");
                    return;
                }
                if ((mat.Flags & Tgpt.MaterialTranslucent) != 0)
                {
                    var t = TransmissionOf(settings);
                    float thickness = m.HasProperty("_Thickness") ? Mathf.Clamp01(m.GetFloat("_Thickness")) : 1f;
                    if (m.HasProperty("_ThicknessMap") && m.GetTexture("_ThicknessMap") != null && m.HasProperty("_ThicknessRemap"))
                    {
                        var remap = m.GetVector("_ThicknessRemap");
                        thickness = Mathf.Clamp01(0.5f * (remap.x + remap.y));
                    }
                    float mask = m.HasProperty("_TransmissionMask") ? Mathf.Clamp01(m.GetFloat("_TransmissionMask")) : 1f;
                    float mm = t.remap.z + t.remap.w * thickness;
                    var share = Vector3.zero;
                    for (int c = 0; c < 3; c++)
                    {
                        float third = Mathf.Exp(-t.shape[c] * mm / 3f);
                        share[c] = Mathf.Max(t.tint[c], 0f) * third * (third * third + 3f) * mask;
                    }
                    if (!(share.x >= 0f && share.y >= 0f && share.z >= 0f) || float.IsInfinity(share.x + share.y + share.z)) return;
                    mat.SubsurfaceR = share.x; mat.SubsurfaceG = share.y; mat.SubsurfaceB = share.z;
                    // Once a material (it stopped after 24 lines until 2026-10-08: the
                    // shares above 1 were found from a log that held a quarter of them).
                    // Above 1 is no share: the profile's transmission tint is an HDR
                    // colour, and a sheet whose colour x (1 + share) is above 1 sends on
                    // more light than fell on it.
                    float most = Mathf.Max(share.x, Mathf.Max(share.y, share.z));
                    if (walkLogged.Add("thin|" + m.name + "|" + shaderName))
                        Session.Log.LogInfo("Thin material \"" + m.name + "\" (" + shaderName + ", " + settings.name + "): where a path meets it, it passes " +
                            share.x.ToString("0.00") + ", " + share.y.ToString("0.00") + ", " + share.z.ToString("0.00") + " of the light from behind (" +
                            mm.ToString("0.#") + " mm thick, mask " + mask.ToString("0.##") + (m.HasProperty("_Thickness") ? "" : ", no _Thickness: 1") + ")" +
                            (most > 1f ? "; above 1: with a colour over " + (1f / (1f + most)).ToString("0.00") + " it sends on more light than falls on it" : ""));
                    return;
                }
                // Who walks is the game's own rule (PT-40, PT-73 c; docs/agents/who-walks.md has the census and the shaders'
                // decodes): the material's subsurface feature is on (the keyword its shader's variant is chosen by), and its
                // mask says how much. A profile alone is no sign: 688 of the game's clothes, armour and weapon materials and
                // 170 of its terrain's name one with the feature off, and walked for it until 2026-10-08.
                bool feature = KeywordOn(m, "_MATERIAL_FEATURE_SUBSURFACE_SCATTERING");
                if (!feature)
                {
                    WalkLog(m, shaderName, "names " + settings.name + " but its subsurface feature is off" +
                        (m.HasProperty("_MaterialID") ? " (material type " + Mathf.RoundToInt(m.GetFloat("_MaterialID")) + ")" : "") + ": a plain surface, as the game draws it");
                    return;
                }
                const uint noInside = Tgpt.MaterialThinCloth | Tgpt.MaterialGlass | Tgpt.MaterialCameraOnly | Tgpt.MaterialStrands | Tgpt.MaterialHairLayer;
                if ((mat.Flags & noInside) != 0)
                {
                    WalkLog(m, shaderName, "its subsurface feature is on (" + settings.name + "), but it is traced as " +
                        ((mat.Flags & Tgpt.MaterialGlass) != 0 ? "glass" : (mat.Flags & Tgpt.MaterialThinCloth) != 0 ? "thin cloth" :
                         (mat.Flags & (Tgpt.MaterialStrands | Tgpt.MaterialHairLayer)) != 0 ? "hair" : "a surface for camera rays only") + ": no walk");
                    return;
                }
                var v = ScatteringOf(settings);
                if (!(v.x > 0f && v.y > 0f && v.z > 0f))
                {
                    WalkLog(m, shaderName, "its subsurface feature is on, but " + settings.name + " has no scattering distance: no walk");
                    return;
                }
                float ownMask = OwnMask(m, shaderName, out string from, out bool unread);
                if (unread)
                {
                    WalkLog(m, shaderName, "its subsurface feature is on (" + settings.name + "), but " + from + ": NOT walked yet, lit as a plain surface (a gap: docs/agents/who-walks.md)");
                    return;
                }
                if (!(ownMask > 0f))
                {
                    WalkLog(m, shaderName, "its subsurface feature is on (" + settings.name + "), with a mask of 0 (" + from + "): a plain surface, as the game draws it");
                    return;
                }
                mat.Flags |= Tgpt.MaterialSubsurface;
                mat.SubsurfaceR = v.x; mat.SubsurfaceG = v.y; mat.SubsurfaceB = v.z;
                mat.SubsurfaceMask = Mathf.Clamp01(ownMask);
                WalkLog(m, shaderName, "light walks inside it (" + settings.name + ", scattering " +
                    (1000f / v.x).ToString("0.##") + ", " + (1000f / v.y).ToString("0.##") + ", " + (1000f / v.z).ToString("0.##") + " mm), as often as its mask says: " +
                    mat.SubsurfaceMask.ToString("0.##") + " (" + from + "); " +
                    ((mat.Flags & Tgpt.MaterialDoubleSided) != 0 ? "drawn from both sides: only where its mesh closes a volume (the tracer's log says, by material number)" : "drawn one-sided: a solid's outside"));
            }
            catch (Exception ex)
            {
                if (!subsurfaceFailed) { subsurfaceFailed = true; Session.Log.LogWarning("A material's diffusion profile could not be read (lit as a plain surface where paths meet it): " + (ex.InnerException ?? ex).Message); }
            }
        }
        // ON TRIAL (PT-40, docs/agents/flame-near.md): the two hidden "Highlight shoulder" settings as the one number the
        // tracer takes them as (TGPT_Frame.frameReserved; pt_common.hlsli highlight_shoulder): the knee in 64ths in bits
        // 0-11 (0: where the hold stops), the most in 64ths in bits 12-23 (0, the default: off, the picture as before).
        internal static float HighlightShoulder(Settings s)
        {
            float knee = Mathf.Round(Mathf.Clamp(s.HighlightShoulderKnee.Value, 0f, 63f) * 64f);
            float most = Mathf.Round(Mathf.Clamp(s.HighlightShoulderMost.Value, 0f, 63f) * 64f);
            return most > 0f ? knee + 4096f * most : 0f;
        }
        private static uint ThickProfiles(HDCamera hd)
        {
            try
            {
                var profiles = hd?.volumeStack?.GetComponent<DiffusionProfileList>()?.diffusionProfiles.value;
                uint mask = 0;
                if (profiles != null)
                    for (int i = 0; i < profiles.Length && i < 16; i++)
                        if (profiles[i] != null && IsThick(profiles[i])) mask |= 1u << i;
                // (No table of the profiles' scattering or transmission by HDRP's G-buffer index goes to the tracer since PT-58: the
                // material its camera ray met says both, ReadSubsurface.)
                // Every profile's mirror reflectance (its Fresnel0, transmissionTintAndFresnel0's w, which HDRP keeps whether
                // or not transmission is on), which HDRP gives the pixels of its transmission features: the game's foliage
                // mirrors 0.0023 (an index of refraction of 1.1), where the tracer's 0.04 made wet grass glint white.
                for (int i = 0; i < 16; i++)
                    reflectanceTable[i] = profiles != null && i < profiles.Length && profiles[i] != null ? TransmissionOf(profiles[i]).tint.w : 0f;
                bool sameReflectance = reflectanceKnown;
                for (int i = 0; i < 16 && sameReflectance; i++) sameReflectance = reflectanceTable[i] == reflectanceSent[i];
                if (!sameReflectance && Session.Tracer != null && Session.Tracer.SetProfileReflectance(reflectanceTable, 16) == 0)
                {
                    Array.Copy(reflectanceTable, reflectanceSent, 16); reflectanceKnown = true;
                    var text = new System.Text.StringBuilder();
                    for (int i = 0; profiles != null && i < profiles.Length && i < 16; i++)
                        if (profiles[i] != null && reflectanceTable[i] > 0f)
                            text.Append(text.Length > 0 ? ", " : "").Append(profiles[i].name).Append(' ').Append(reflectanceTable[i].ToString("0.0000"));
                    Session.Log.LogInfo("HDRP diffusion profiles' mirror reflectance (a leaf's, in place of 0.04): " + (text.Length > 0 ? text.ToString() : "none"));
                }
                if (mask != thickProfiles)
                {
                    thickProfiles = mask;
                    var names = new System.Text.StringBuilder();
                    for (int i = 0; profiles != null && i < profiles.Length && i < 16; i++)
                        if (profiles[i] != null)
                        {
                            names.Append(names.Length > 0 ? ", " : "").Append(i).Append(' ').Append(profiles[i].name).Append(((mask >> i) & 1u) != 0 ? " (thick" : " (thin");
                            var scatters = ScatteringOf(profiles[i]);
                            if (scatters.x > 0f && scatters.y > 0f && scatters.z > 0f)
                                names.Append(", scatters ").Append((1000f / scatters.x).ToString("0.##")).Append(", ")
                                    .Append((1000f / scatters.y).ToString("0.##")).Append(", ").Append((1000f / scatters.z).ToString("0.##")).Append(" mm");
                            names.Append(')');
                        }
                    DiffusionProfilesText = names.Length > 0 ? names.ToString() : "none";
                    Session.Log.LogInfo("HDRP diffusion profiles: " + DiffusionProfilesText);
                }
                return mask;
            }
            catch (Exception ex)
            {
                if (thickProfiles != 0) { thickProfiles = 0; Session.Log.LogWarning("HDRP's diffusion profiles could not be read (skin is lit as a thin surface): " + ex.Message); }
                return 0;
            }
        }
        private static string exposureSetup = "";
        internal static string ExposureSetupText => exposureSetup;
        private static void LogExposureSetup(HDCamera hd)
        {
            try
            {
                var e = hd.volumeStack?.GetComponent<Exposure>();
                if (e == null) return;
                string text = "mode " + e.mode.value + ", metering " + e.meteringMode.value + ", adaptation " + e.adaptationMode.value +
                    " (dark to light " + e.adaptationSpeedDarkToLight.value.ToString("0.##") + ", light to dark " + e.adaptationSpeedLightToDark.value.ToString("0.##") +
                    "), limits EV " + e.limitMin.value.ToString("0.#") + " to " + e.limitMax.value.ToString("0.#") + ", compensation " + e.compensation.value.ToString("0.##") +
                    ", histogram " + e.histogramPercentages.value.x.ToString("0") + "-" + e.histogramPercentages.value.y.ToString("0") + "%, fixed EV " + e.fixedExposure.value.ToString("0.#");
                // The tonemapper and color adjustments after it: how deep the darks sit on screen depends on its toe.
                var t = hd.volumeStack.GetComponent<Tonemapping>();
                if (t != null)
                {
                    var mode = t.mode.value;
                    text += "; tonemapping " + mode + (mode == TonemappingMode.ACES ? (t.useFullACES.value ? " (full ACES)" : " (approximation)") : "") +
                        (mode == TonemappingMode.Custom ? " (toe " + t.toeStrength.value.ToString("0.##") + " over " + t.toeLength.value.ToString("0.##") + ", shoulder " +
                            t.shoulderStrength.value.ToString("0.##") + " over " + t.shoulderLength.value.ToString("0.##") + ", angle " + t.shoulderAngle.value.ToString("0.##") +
                            ", gamma " + t.gamma.value.ToString("0.##") + ")" : "") +
                        (mode == TonemappingMode.External ? " (LUT " + (t.lutTexture.value != null ? t.lutTexture.value.name : "none") + ", contribution " + t.lutContribution.value.ToString("0.##") + ")" : "");
                }
                var c = hd.volumeStack.GetComponent<ColorAdjustments>();
                if (c != null) text += "; color adjustments: post exposure " + c.postExposure.value.ToString("0.##") + ", contrast " + c.contrast.value.ToString("0.#") + ", saturation " + c.saturation.value.ToString("0.#");
                if (text == exposureSetup) return;
                exposureSetup = text;
                Session.Log.LogInfo("HDRP exposure for this camera: " + text);
            }
            catch (Exception ex) { if (exposureSetup != "?") { exposureSetup = "?"; Session.Log.LogWarning("HDRP's exposure settings could not be read: " + ex.Message); } }
        }
        private static void TrackExposure(HDRenderPipeline pipeline, HDCamera hd)
        {
            LogExposureSetup(hd);
            try
            {
                if (ExposureTexture == null || RequestExposure == null || ExposureValue == null) return;
                var texture = ExposureTexture(pipeline, hd);
                if (texture != null) RequestExposure(hd, texture);
                float e = ExposureValue(hd);
                if (!(e > 0f) || float.IsInfinity(e)) return;
                float log = Mathf.Log(e, 2f);
                LiveExposureLog = log;
                exposureMin = Mathf.Min(exposureMin, log); exposureMax = Mathf.Max(exposureMax, log);
            }
            catch { }
        }

        // Inside the capture pass's recording: GBuffers 2 and 3 are read too, which keeps them alive and in place.
        internal static void DeclareExtraReads(IUnsafeRenderGraphBuilder builder)
        {
            if (pendingGBuffer2.IsValid()) builder.UseTexture(in pendingGBuffer2, AccessFlags.Read);
            if (hasGBuffer3) builder.UseTexture(in pendingGBuffer3, AccessFlags.Read);
            if (pendingPyramid.IsValid()) builder.UseTexture(in pendingPyramid, AccessFlags.Read); // (PT-112: its pointer, for the frame's own depth)
        }

        // Said once a session, on the first frame HDRP shifts: whether the matrices handed to the tracer hold that shift (the
        // jittered projection differs from the plain one in its third column by 2 * shift / size). A warning if they do not:
        // the traced picture would then come out of the upscaler as stair-steps again (PT-84).
        private static void SayShift(HDCamera hd, in HDCamera.ViewConstants view)
        {
            if (shiftSaid || (hd.taaJitter.x == 0f && hd.taaJitter.y == 0f)) return;
            shiftSaid = true;
            bool shifted = view.projMatrix.m02 != view.nonJitteredProjMatrix.m02 || view.projMatrix.m12 != view.nonJitteredProjMatrix.m12;
            string at = hd.taaJitter.x.ToString("0.###", System.Globalization.CultureInfo.InvariantCulture) + ", " +
                hd.taaJitter.y.ToString("0.###", System.Globalization.CultureInfo.InvariantCulture);
            if (shifted) Session.Log.LogInfo("The tracer's camera rays carry HDRP's sub-pixel shift (this frame's: " + at + " render pixels), as its depth and its upscaler do.");
            else Session.Log.LogWarning("HDRP's camera matrices reached the tracer without the frame's sub-pixel shift (" + at + " render pixels): the traced picture's edges will be stair-steps after the upscaler.");
        }

        // PT-107 (R19), logging only: the main thread's frame on the CPU's clock (QueryPerformanceCounter, as the native side's),
        // against the trace's (the native 10-second line's "the next frame after the trace's end"): [0] this frame's Update,
        // [1] HDRP's rendering begun (beginContextRendering), [2] and [3] the capture pass's start and end, [4] HDRP's rendering
        // done (endContextRendering), sent there (TGPT_MainClock); and render event 8 at [0] and [1], for when the render thread
        // reaches those points; and [5] the frame's LateUpdate, with render event 8 there (2) and one queued at once when HDRP's
        // rendering begins (3).
        [System.Runtime.InteropServices.DllImport("kernel32.dll")] private static extern bool QueryPerformanceCounter(out long value);
        private static readonly long[] frameClocks = new long[6];
        private static int clocksFrame = -1, clocksRendering = -1, clocksCaptured = -1;
        private static uint clocksSequence;
        private static CommandBuffer markCommands, renderingMark, lateMark, renderingAtOnce;
        private static bool clocksHooked;
        private static IntPtr MarkData(int which) => new IntPtr(((long)Time.frameCount << 7) | (uint)which);
        // PT-112 (measure-waits only): marks 4 to 8 at HDRP's injection points, for the gameplay camera of a traced frame.
        internal static void MarkTiming(CustomPassContext ctx, int which)
        {
            var tracer = Session.Tracer;
            if (clocksFrame != Time.frameCount || tracer == null || tracer.RenderEvent == IntPtr.Zero || !IsGameplayCamera(ctx.hdCamera)) return;
            ctx.cmd.IssuePluginEventAndData(tracer.RenderEvent, Tgpt.EventMark, MarkData(which));
        }
        private static void InsertTimingVolumes()
        {
            var points = new[] { CustomPassInjectionPoint.BeforeRendering, CustomPassInjectionPoint.AfterOpaqueDepthAndNormal, CustomPassInjectionPoint.BeforeTransparent,
                                 CustomPassInjectionPoint.BeforePostProcess, CustomPassInjectionPoint.AfterPostProcess };
            for (int i = 0; i < points.Length; ++i)
            {
                var host = new GameObject("TG Path Tracing timing " + points[i]) { hideFlags = HideFlags.HideAndDontSave };
                host.SetActive(false);
                UnityEngine.Object.DontDestroyOnLoad(host);
                var volume = host.AddComponent<CustomPassVolume>();
                volume.isGlobal = true;
                volume.injectionPoint = points[i];
                volume.customPasses.Add(new TimingPass { Mark = 4 + i, name = "TG Path Tracing timing", targetColorBuffer = CustomPass.TargetBuffer.None, targetDepthBuffer = CustomPass.TargetBuffer.None });
                host.SetActive(true);
            }
            Session.Log.LogInfo("PT-112 measurement (measure-waits): timing marks at HDRP's BeforeRendering, AfterOpaqueDepthAndNormal, BeforeTransparent, BeforePostProcess and AfterPostProcess.");
        }
        // With the measure-waits file (PT-107's measurement, never for players): a render event 8 after each step of Unity's
        // PostLateUpdate, marks 8 and up, so that the native side says after which step the game's slow calls come.
        private static CommandBuffer loopMark;
        private static void LoopMark(int which)
        {
            var tracer = Session.Tracer;
            if (clocksFrame != Time.frameCount || tracer == null || tracer.RenderEvent == IntPtr.Zero) return;
            if (loopMark == null) loopMark = new CommandBuffer { name = "TGPT clock" };
            loopMark.Clear();
            loopMark.IssuePluginEventAndData(tracer.RenderEvent, Tgpt.EventMark, MarkData(which));
            Graphics.ExecuteCommandBuffer(loopMark);
        }
        private struct TgptLoopMark { }
        private static void InsertLoopMarks()
        {
            string file = System.IO.Path.Combine(BepInEx.Paths.BepInExRootPath, "TGPathTracing", "Native", "measure-waits");
            if (!System.IO.File.Exists(file)) return;
            var loop = UnityEngine.LowLevel.PlayerLoop.GetCurrentPlayerLoop();
            var names = new List<string>();
            for (int i = 0; i < loop.subSystemList.Length; ++i)
            {
                var phase = loop.subSystemList[i];
                if (phase.type != typeof(UnityEngine.PlayerLoop.PostLateUpdate) || phase.subSystemList == null) continue;
                var steps = new List<UnityEngine.LowLevel.PlayerLoopSystem>();
                foreach (var step in phase.subSystemList)
                {
                    steps.Add(step);
                    int which = 16 + names.Count;
                    if (which > 127) continue;
                    names.Add(which + " after " + (step.type != null ? step.type.Name : "?"));
                    steps.Add(new UnityEngine.LowLevel.PlayerLoopSystem { type = typeof(TgptLoopMark), updateDelegate = () => LoopMark(which) });
                }
                phase.subSystemList = steps.ToArray(); loop.subSystemList[i] = phase;
            }
            UnityEngine.LowLevel.PlayerLoop.SetPlayerLoop(loop);
            InsertTimingVolumes();
            Session.Log.LogInfo("PT-107 measurement (measure-waits): render marks in Unity's PostLateUpdate: " + string.Join(", ", names.ToArray()));
        }
        internal static void MarkUpdate()
        {
            var tracer = Session.Tracer;
            if (tracer == null || !tracer.Connected || tracer.RenderEvent == IntPtr.Zero || Session.Settings == null || Session.Settings.CurrentMode == TracingMode.Off) return;
            if (!clocksHooked) { clocksHooked = true; RenderPipelineManager.beginContextRendering += RenderingBegun; RenderPipelineManager.endContextRendering += RenderingDone; InsertLoopMarks(); }
            Array.Clear(frameClocks, 0, frameClocks.Length);
            QueryPerformanceCounter(out frameClocks[0]); clocksFrame = Time.frameCount;
            if (markCommands == null) markCommands = new CommandBuffer { name = "TGPT clock" };
            markCommands.Clear();
            markCommands.IssuePluginEventAndData(tracer.RenderEvent, Tgpt.EventMark, MarkData(0));
            Graphics.ExecuteCommandBuffer(markCommands);
        }
        internal static void MarkLateUpdate()
        {
            var tracer = Session.Tracer;
            if (clocksFrame != Time.frameCount || tracer == null || tracer.RenderEvent == IntPtr.Zero) return;
            QueryPerformanceCounter(out frameClocks[5]);
            if (lateMark == null) lateMark = new CommandBuffer { name = "TGPT clock" };
            lateMark.Clear();
            lateMark.IssuePluginEventAndData(tracer.RenderEvent, Tgpt.EventMark, MarkData(2));
            Graphics.ExecuteCommandBuffer(lateMark);
        }
        private static void RenderingBegun(ScriptableRenderContext context, List<Camera> cameras)
        {
            var tracer = Session.Tracer;
            if (clocksFrame != Time.frameCount || clocksRendering == Time.frameCount || tracer == null || tracer.RenderEvent == IntPtr.Zero) return;
            clocksRendering = Time.frameCount;
            QueryPerformanceCounter(out frameClocks[1]);
            if (renderingAtOnce == null) renderingAtOnce = new CommandBuffer { name = "TGPT clock" };
            renderingAtOnce.Clear();
            renderingAtOnce.IssuePluginEventAndData(tracer.RenderEvent, Tgpt.EventMark, MarkData(3));
            Graphics.ExecuteCommandBuffer(renderingAtOnce);
            if (renderingMark == null) renderingMark = new CommandBuffer { name = "TGPT clock" };
            renderingMark.Clear();
            renderingMark.IssuePluginEventAndData(tracer.RenderEvent, Tgpt.EventMark, MarkData(1));
            context.ExecuteCommandBuffer(renderingMark);
        }
        private static void RenderingDone(ScriptableRenderContext context, List<Camera> cameras)
        {
            if (clocksCaptured != Time.frameCount || frameClocks[4] != 0 || frameClocks[5] == 0) return;
            QueryPerformanceCounter(out frameClocks[4]);
            Session.Tracer?.MainClock(clocksSequence, Time.frameCount, frameClocks);
        }

        // Render-graph execution of the capture pass, main thread: the frame to the native side, then render event 1.
        internal static unsafe void Capture(CustomPassContext ctx)
        {
            ++executed; unexecuted = 0;
            if (!armed) return;
            armed = false;
            QueryPerformanceCounter(out long captureClock); // (PT-107)
            try
            {
                var hd = ctx.hdCamera;
                var tracer = Session.Tracer;
                if (hd == null || tracer == null || !tracer.Connected) return;
                var pipeline = RenderPipelineManager.currentPipeline as HDRenderPipeline;
                RTHandle gb2 = pendingGBuffer2, gb3 = hasGBuffer3 ? (RTHandle)pendingGBuffer3 : null; // valid while the graph executes
                IntPtr gbuffer0 = NativeTextures.Pointer(ctx.cameraColorBuffer), normal = NativeTextures.Pointer(ctx.cameraNormalBuffer);
                IntPtr gbuffer2 = NativeTextures.Pointer(gb2), gbuffer3 = NativeTextures.Pointer(gb3);
                IntPtr depth = NativeTextures.Pointer(ctx.cameraDepthBuffer), motion = NativeTextures.Pointer(ctx.cameraMotionVectorsBuffer);
                IntPtr exposure = pipeline != null && ExposureTexture != null ? NativeTextures.Pointer(ExposureTexture(pipeline, hd)) : IntPtr.Zero;
                if (gbuffer0 == IntPtr.Zero || normal == IntPtr.Zero || depth == IntPtr.Zero) { LastFrame = "a GBuffer texture had no native resource"; return; }
                var settings = Session.Settings;
                // The matrices of this frame as BeforeSsr kept them, with the sub-pixel shift HDRP drew its depth and G-buffer
                // under and tells its upscaler: never hd.mainViewConstants here (no shift in them by now: armedView).
                var view = armedView;
                // (Advanced / Camera shift off, for finding causes: the matrices as they are by now, without the shift, and none told.)
                if (!settings.CameraShift.Value) view = hd.mainViewConstants;
                SayShift(hd, in view);
                var camera = view.worldSpaceCameraPos;
                var frame = new NativeFrame
                {
                    Size = 624, Abi = Tgpt.Abi, Sequence = ++sequence,
                    GBuffer0 = Ptr(gbuffer0), Normal = Ptr(normal), GBuffer2 = Ptr(gbuffer2), GBuffer3 = Ptr(gbuffer3),
                    Depth = Ptr(depth), Motion = Ptr(motion), Exposure = Ptr(exposure),
                    RenderWidth = (uint)Mathf.Max(0, hd.actualWidth), RenderHeight = (uint)Mathf.Max(0, hd.actualHeight),
                    DebugView = (uint)settings.CurrentView,
                    FrameIndex = (uint)Time.frameCount, Bounces = (uint)settings.Bounces.Value, SpatialSamples = (uint)settings.SpatialSamples.Value,
                    RaysPerPixel = DebugCapture.Rays != 0 ? DebugCapture.Rays : (uint)settings.RaysPerPixel.Value,
                    // ReSTIR's histories in frames, which NVIDIA RTXDI's reuse decorrelates for the denoisers (permutation
                    // sampling, the boiling filter): 4 for direct light, as NVIDIA's RTX Remix keeps it with Ray
                    // Reconstruction, and RTXDI's 8 for bounced light, unless the config's Direct light history or Bounced
                    // light history sets another. RTXDI's 20 for direct light boiled on thin geometry, where its temporal
                    // reuse isn't permuted: the F8 capture's frame-to-frame change on a cage's bars 0.40 at 20, 0.14 at 4,
                    // 0.11 without the reuse. (Before RTXDI a history of 1 was the least grainy: the tracer's own reuse
                    // permuted too little, and longer reuse made noise stand still.)
                    DiHistory = DebugCapture.DiHistory != 0 ? DebugCapture.DiHistory : settings.DirectLightHistory.Value > 0 ? (uint)settings.DirectLightHistory.Value : 4u,
                    GiHistory = DebugCapture.GiHistory != 0 ? DebugCapture.GiHistory : settings.BouncedLightHistory.Value > 0 ? (uint)settings.BouncedLightHistory.Value : 8u,
                    TracedGain = TracedGain,
                    HighlightKept = settings.HighlightsKept.Value,
                    FrameReserved = HighlightShoulder(settings),
                    ThickProfiles = ThickProfiles(hd),
                    Options = settings.Options | DebugCapture.ExtraOptions | (RayReconstruction.Active ? Tgpt.OptionRayReconstruction : Session.WantsRayRegeneration ? Tgpt.OptionRayRegeneration : 0u),
                    // Indoors rays see no sky (SceneLights.Interior: interiors float in the sky above the world, and
                    // a ray through a seam between wall and floor found it: thin blue-white lines along the seams).
                    MaxRayDistance = Tgpt.NoRayEnd, SkyIntensity = DebugCapture.SkyOff || SceneLights.Interior ? 1e-6f : Mathf.Max(settings.SkyIntensity.Value, 1e-6f),
                    IndirectColor = settings.IndirectColor.Value, ReflectionBoost = settings.ReflectionBoost.Value
                };
                // A camera cut starts every history over (ReSTIR's, the denoisers', NRD's, the traced light's reprojection):
                // another camera, HDRP's own reset for this one (as it rises: only post-processing clears it), a jump of more
                // than 20 m, or a turn of more than 60 degrees in one frame (a dialogue cutting from one speaker to the other
                // with the same camera; a flick that fast with the mouse costs a frame of history, no more).
                int cameraId = hd.camera != null ? hd.camera.GetInstanceID() : 0;
                var forward = hd.camera != null ? hd.camera.transform.forward : Vector3.forward;
                bool hdrpReset = HdrpHistoryReset != null && HdrpHistoryReset.GetValue(hd) is bool flag && flag;
                if (!haveLastCamera || cameraId != lastCameraId || (hdrpReset && !lastHdrpReset) || (camera - lastCameraPosition).sqrMagnitude > 400f ||
                    Vector3.Dot(forward, lastCameraForward) < 0.5f) frame.Flags |= Tgpt.FrameReset;
                lastCameraId = cameraId; lastCameraForward = forward; lastHdrpReset = hdrpReset;
                if (pipeline != null) TrackExposure(pipeline, hd);
                UpdateTracedGain(settings);
                // PT-114 on trial (Advanced / Caustics grid trial; 0 unless set): the caustics' grid in three steps, each a bit
                // of the flags (tgpt_api.h TGPT_FRAME_CAUSTICS_GRID; no option bit is free).
                { int grid = Mathf.Clamp(settings.CausticsGrid.Value, 0, 3);
                  if (grid >= 1) frame.Flags |= Tgpt.FrameCausticsGrid;
                  if (grid >= 2) frame.Flags |= Tgpt.FrameCausticsGridWater;
                  if (grid >= 3) frame.Flags |= Tgpt.FrameCausticsGridView; }
                // DLSS Ray Reconstruction's guides, which TG Better Upscaling fetches later in this frame.
                if (Session.WantsRayReconstruction) { frame.Flags |= Tgpt.FrameGuides; RayReconstruction.GuidesFrame = Time.frameCount; }
                // PT-109 on trial (Advanced / Mega Geometry trial; 0 unless set): NVIDIA RTX Mega Geometry for the tracer's
                // acceleration structures, in the flags' bits 2 to 5 (tgpt_api.h TGPT_FRAME_MEGA_SHIFT; no option bit).
                frame.Flags |= (uint)Mathf.Clamp(settings.MegaGeometryTrial.Value, 0, (int)Tgpt.FrameMegaMask) << Tgpt.FrameMegaShift;
                // PT-110, for measuring (Advanced / Micromaps trial; 0 unless set): a kind of cut-out made solid, or the
                // micromaps left unused, for a measuring step, in the flags' bits 6 to 13 (tgpt_api.h TGPT_FRAME_OMM_SHIFT).
                frame.Flags |= (uint)Mathf.Clamp(settings.MicromapsTrial.Value, 0, (int)Tgpt.FrameOmmMask) << Tgpt.FrameOmmShift;
                // PT-127 on trial (Advanced / NRD trial; 0 unless set): NRD as its documentation sets it up, RELAX (1) or REBLUR
                // (2), in the flags' bits 17 and 18 (tgpt_api.h TGPT_FRAME_NRD_SHIFT).
                frame.Flags |= (uint)Mathf.Clamp(settings.NrdTrial.Value, 0, 2) << Tgpt.FrameNrdShift;
                // PT-112 (A): with HDRP's opaque drawing left out (GamePasses), the traced frame's own depth and motion go into
                // HDRP's buffers (what its fog, transparents, the upscaler and post-processing read).
                if (GamePasses.OwnDepth) frame.Flags |= Tgpt.FrameOwnDepth;
                // PT-112 (B), the rule since 2026-10-11: the game's fog traced along the camera's rays (in cells: PT-118); HDRP's opaque
                // fog pass is left out for the frame (GamePasses.BeforeOpaqueFog). Its local fog boxes with it (FogBoxes).
                if (GamePasses.TracedFog) frame.Flags |= Tgpt.FrameTracedFog | (settings.FogScatteredOnce.Value ? 0u : Tgpt.FrameFogWalk);
                FogBoxes.Send(GamePasses.TracedFog);
                SkyModel.Send(hd); // PT-112, the sky, on trial (the hidden "Sky trial"; HDRP's sky as before unless it asks)
                Copy(view.invViewProjMatrix, frame.InvViewProj);
                Copy(view.viewProjMatrix, frame.ViewProj);
                Copy(view.prevViewProjMatrix, frame.PrevViewProj);
                Copy(view.viewMatrix, frame.View);
                Copy(view.nonJitteredProjMatrix, frame.NonJitteredProj);
                frame.Jitter[0] = settings.CameraShift.Value ? hd.taaJitter.x : 0f; frame.Jitter[1] = settings.CameraShift.Value ? hd.taaJitter.y : 0f;
                FillFog(hd, settings, ref frame);
                // The share of the game's ambient light HDRP leaves hair and eyes (RaiseForwardAmbient): the tracer adds its
                // bounced light for the rest (pt_compose).
                frame.Forward[0] = ForwardAmbientKept;
                // Hair drawn as a layer reads its strands' alpha with an upscaler's mip bias, as HDRP's textures take it: log2 of
                // the render width over the output width (0 at native resolution).
                float outputWidth = hd.camera != null ? hd.camera.pixelWidth : 0f;
                frame.Forward[1] = outputWidth > 0f && hd.actualWidth > 0 ? Mathf.Min(0f, Mathf.Log(hd.actualWidth / outputWidth, 2f)) : 0f;
                // PT-64: the game's global saturation, which every one of its own shaders scales its color's distance from grey by
                // (one plus it; 0 as a rule): the tracer's programs for those shaders apply it as they do.
                frame.Forward[3] = Shader.GetGlobalFloat(GlobalSaturation);
                // PT-85 on trial (Advanced / Reuse follows camera shift; 0 unless set): where the tracer fetches what it reuses
                // of the frame before (TGPT_Frame.jitter's z: tgpt_api.h).
                frame.Jitter[2] = Mathf.Clamp(settings.ReuseFollowsShift.Value, 0, 2);
                // PT-85 and PT-40 on trial (Advanced / Lamp trial ...; 0 unless set): which use of a small lamp's size makes
                // the specks (TGPT_Frame.jitter's w: tgpt_api.h).
                frame.Jitter[3] = SceneLights.LampTrial(settings);
                // PT-93 on trial (Advanced / Grass shadow trial; 0 unless set): a light's ray through grass and leaves takes
                // the product of what each blade lets through, in place of a coin at each (TGPT_Frame.fogMip's w: tgpt_api.h).
                frame.FogMip[3] = settings.GrassShadowTrial.Value == 1 ? 1f : 0f;
                var prev = haveLastCamera ? lastCameraPosition : camera;
                frame.CameraPosition[0] = camera.x; frame.CameraPosition[1] = camera.y; frame.CameraPosition[2] = camera.z;
                // On trial, in TGPT_Frame.cameraPosition's w (tgpt_api.h; no option bit): PT-13 and PT-35, why the jars are
                // cloudy (Advanced / Glass trial; 0 unless set) in its low 4 bits; times 16, the water's three (PT-42, PT-81,
                // PT-94: shore foam, lamps in the water's body, its light without the view).
                frame.CameraPosition[3] = Mathf.Clamp(settings.GlassTrial.Value, 0, 15) + 16 * ((settings.WaterShoreFoam.Value ? 1 : 0) |
                    (settings.WaterBodyLamps.Value ? 2 : 0) | (settings.WaterLightWithoutView.Value ? 4 : 0));
                frame.PrevCameraPosition[0] = prev.x; frame.PrevCameraPosition[1] = prev.y; frame.PrevCameraPosition[2] = prev.z;
                // On trial, two things in TGPT_Frame.prevCameraPosition's w (tgpt_api.h; no option bit). Its bits 0 to 2,
                // PT-95 and PT-19 (Advanced / Reflection relief trial; 0 unless set): the reflections' own filter by the
                // lobe (1), by the reflectance (2) and without its feedback (4). The rest, times 8, PT-99 (Advanced /
                // Speed trial; 0 unless set): speed that leaves the picture as it is, a bit each.
                frame.PrevCameraPosition[3] = Mathf.Clamp(settings.ReflectionReliefTrial.Value, 0, 7) + 8 * Mathf.Clamp(settings.SpeedTrial.Value, 0, 65535);
                lastCameraPosition = camera; haveLastCamera = true;
                var sun = SceneLights.Sun;
                // Shadow maps are the camera's (the game's Shadows toggle): what HDRP shadows itself takes no traced shadow twice.
                ShadowMaps = GamePasses.GameShadowMaps;
                if (sun.Valid)
                {
                    frame.SunDirection[0] = sun.Direction.x; frame.SunDirection[1] = sun.Direction.y; frame.SunDirection[2] = sun.Direction.z; frame.SunDirection[3] = sun.AngularRadius;
                    frame.SunColor[0] = sun.Color.x; frame.SunColor[1] = sun.Color.y; frame.SunColor[2] = sun.Color.z;
                    frame.SunColor[3] = sun.HdrpShadows && ShadowMaps ? 2f : 1f; // 2: HDRP shadows it itself (its cascades)
                }
                if (frame.RenderWidth == 0 || frame.RenderHeight == 0) return;
                // This frame's characters, posed by the skinning that ran before this camera; they go with the frame.
                CharacterCapture.Capture(camera);
                // And what else the game moves (a weapon in a hand, a door, a turning windmill: PT-71 item 4), where HDRP draws it
                // now, with where it was the frame before.
                SceneCapture.Follow(tracer, camera);
                // PT-108 on trial (Advanced / Rain wetness; nothing unless set): the game's moisture as its own wetness pass
                // applies it this frame (TGPT_SetRain: a record of its own, no field of the frame; it goes with the frame).
                RainWetness.Send(tracer, settings);
                // And the wind on the cloth the game sways, at the time HDRP draws it with (pt_sway: traced in that pose, PT-49).
                WaterCaustics.Tick(); // Copy the constants after this camera updated the real water simulation.
                SceneCapture.SendClothWind(tracer);
                DecalCapture.Capture(hd); // after the game animates projector fades; all rays read the same records
                int result = tracer.SubmitFrame(ref frame);
                if (result != 0) { LastFrame = "the native side refused the frame (" + result + ")"; return; }
                ctx.cmd.IssuePluginEventAndData(tracer.RenderEvent, Tgpt.EventTrace, new IntPtr((long)frame.Sequence));
                // PT-112 (A), on trial: HDRP's depth pyramid, which the composite writes the frame's own depth into as well.
                if ((frame.Flags & Tgpt.FrameOwnDepth) != 0 && pendingPyramid.IsValid())
                {
                    IntPtr pyramid = NativeTextures.Pointer((RTHandle)pendingPyramid);
                    if (pyramid != IntPtr.Zero) tracer.SetDepthPyramid(frame.Sequence, pyramid);
                }
                if (clocksFrame == Time.frameCount) { frameClocks[2] = captureClock; QueryPerformanceCounter(out frameClocks[3]); clocksSequence = frame.Sequence; clocksCaptured = Time.frameCount; } // (PT-107)
                pendingComposite = frame.Sequence; pendingCompositeFrame = Time.frameCount;
                ++Captured; ++submitted;
                LastFrame = frame.RenderWidth + "x" + frame.RenderHeight + (frame.DebugView != 0 ? " view " + settings.View.Value : "") +
                    ((frame.Options & Tgpt.OptionRayReconstruction) != 0 ? " for Ray Reconstruction" : (frame.Options & Tgpt.OptionRayRegeneration) != 0 ? " for Ray Regeneration" : "");
            }
            catch (Exception ex)
            {
                Fail("The path tracing capture failed: " + ex);
            }
        }

        // The global composite pass, every camera: only the gameplay camera's frame with a capture this frame.
        internal static void Composite(CustomPassContext ctx)
        {
            if (pendingComposite == 0 || pendingCompositeFrame != Time.frameCount || !IsGameplayCamera(ctx.hdCamera)) return;
            uint seq = pendingComposite; pendingComposite = 0;
            try
            {
                var tracer = Session.Tracer;
                IntPtr color = NativeTextures.Pointer(ctx.cameraColorBuffer);
                if (tracer == null || color == IntPtr.Zero || tracer.SetComposite(seq, color) != 0) return;
                ctx.cmd.IssuePluginEventAndData(tracer.RenderEvent, Tgpt.EventComposite, new IntPtr((long)seq));
                ++Composited;
            }
            catch (Exception ex) { Fail("The path tracing composite failed: " + ex); }
        }

        // HDRP's transparent SSR step for a camera (RenderTransparency, after its water's GBuffer and the water's preparation
        // for SSR, before the water is lit): the water pass records right behind it, for the gameplay camera of a traced
        // frame, with HDRP's SSR texture (what the step returns) as its color buffer, and the depth-stencil and normal buffers,
        // which hold the water's depth, stencil bits and normals by then.

        // Render-graph execution of the water pass: the frame the capture handed over, then render event 4.

        // For the 10 s line and the capture's report.
        internal static string WaterText => "water is in the camera rays";

        // HDRP's sky reflection cubemap (array), physical radiance, for rays that leave the scene.
        internal static Texture SkyTexture(HDCamera hd)
        {
            try
            {
                var pipeline = RenderPipelineManager.currentPipeline as HDRenderPipeline;
                var manager = pipeline != null ? SkyManagerProperty?.GetValue(pipeline, null) : null;
                if (manager == null || hd == null) return null;
                if (skyReflection == null) skyReflection = AccessTools.Method(manager.GetType(), "GetSkyReflection", new[] { typeof(HDCamera) });
                return skyReflection?.Invoke(manager, new object[] { hd }) as Texture;
            }
            catch { return null; }
        }

        private static ulong Ptr(IntPtr p) => unchecked((ulong)p.ToInt64());
        private static unsafe void Copy(Matrix4x4 m, float* to)
        {
            // Unity's memory order (column-major): what the native side and its shaders take.
            for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) to[c * 4 + r] = m[r, c];
        }

        private static void Fail(string message)
        {
            if (failed) return;
            failed = true;
            Detail = message;
            Session.Log.LogError(message + " Path tracing stays off for the rest of the session.");
            try { SetIndirectOverride(false); } catch { }
        }
        internal static bool Failed => failed;

        internal static void Dispose()
        {
            EffectPasses.Hold(false);
            var installed = harmony;
            harmony = null;
            try { installed?.UnpatchSelf(); } catch (Exception ex) { Session.Log?.LogWarning("Path tracing unpatch failed: " + ex.Message); }
            foreach (var host in new[] { captureHost, compositeHost, indirectHost, scaledHost }) if (host != null) UnityEngine.Object.Destroy(host);
            if (seenCopy != null) { RTHandles.Release(seenCopy); seenCopy = null; }
            captureHost = compositeHost = indirectHost = scaledHost = null; captureVolume = null; capturePass = null; indirectVolume = scaledVolume = null; armed = false;
        }
    }
}
