// SPDX-License-Identifier: MIT
// Where the path tracer meets HDRP's frame.
//
// Capture: HDRP renders the GBuffer in its prepass, then (before shadows and lighting) runs its opaque
// screen-space reflections step, RenderSSR, which receives the whole prepass output. A Harmony prefix on
// RenderSSR records this mod's capture pass into HDRP's render graph right there, through
// CustomPass.ExecuteInternal, declaring GBuffer0 (as the pass's color buffer), the normal buffer, depth and
// motion vectors as its targets and GBuffers 2 and 3 as extra reads (SetupBeforeExecute). The render graph
// keeps them alive and in place for the pass, which hands their native textures to the tracer and issues
// render event 1: the D3D12 path tracing then runs alongside HDRP's shadows and lighting.
//
// Composite: a global custom pass after opaque lighting and the sky (before fog, transparents and post)
// issues render event 2, which applies the previous frame's traced light to HDRP's color buffer.
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
        protected override void SetupBeforeExecute(RenderGraph renderGraph, RenderGraphBuilder builder, RenderTargets currentRenderTargets) =>
            HdrpPasses.DeclareExtraReads(builder);
        protected override void Execute(CustomPassContext ctx) => HdrpPasses.Capture(ctx);
    }
    internal sealed class CompositePass : CustomPass
    {
        protected override void Execute(CustomPassContext ctx) => HdrpPasses.Composite(ctx);
    }
    internal sealed class ShadowDetailPass : CustomPass
    {
        protected override void Execute(CustomPassContext ctx) => HdrpPasses.ApplyShadowDetail(ctx);
    }
    // Right after HDRP's transparent SSR step: its SSR texture as the color buffer, its depth-stencil and normal buffers.
    internal sealed class WaterPass : CustomPass
    {
        protected override void Execute(CustomPassContext ctx) => HdrpPasses.Water(ctx);
    }
    // The image as it is seen, HDRP's fog and transparent surfaces over the traced light, before post-processing: a small
    // copy read back for the traced light's exposure (HdrpPasses.MeasureSeen).
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
        private static TextureHandle pendingGBuffer2, pendingGBuffer3;
        private static bool armed, hasGBuffer3;
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
        private static ShadowDetailPass shadowPass;
        private static bool shadowFailed;
        private static SeenMeterPass seenPass;
        private static RTHandle seenCopy;
        private static bool seenFailed, seenPending;
        private static int nextSeenFrame, seenRequestFrame;
        // The seen image's median (log2) and the traced light's gain (log2) it was shown with; NaN until measured.
        private static float seenLog2Mid = float.NaN, seenLog2Gain, seenAt = -10f;
        private static float[] seenLuminance;
        private static int captureRecordedFrame = -1;
        private static uint shadowRequest;
        // Traced reflections on water (render event 4): the pass, this frame's sequence for it (from the capture) and HDRP's
        // reflection lighting multiplier when it was recorded; counts for the 10 s line.
        private static WaterPass waterPass;
        private static bool waterFailed;
        private static uint pendingWater;
        private static int pendingWaterFrame = -1;
        private static float waterMultiplier = 1f;
        private static long waterRecorded, waterIssued, waterNoSsr;
        // HDRP's shared default textures (its black one stands for SSR where there is none this frame) are marked built in.
        private static readonly FieldInfo TextureBuiltin = AccessTools.Field(typeof(TextureHandle), "builtin");
        private static readonly Type PrepassOutputType = AccessTools.Inner(typeof(HDRenderPipeline), "PrepassOutput");
        private static readonly Type GBufferOutputType = AccessTools.Inner(typeof(HDRenderPipeline), "GBufferOutput");
        private static FieldInfo PrepassField(string name) => PrepassOutputType != null ? AccessTools.Field(PrepassOutputType, name) : null;
        private static readonly FieldInfo PrepassGBuffer = PrepassField("gbuffer"), PrepassDepth = PrepassField("depthBuffer"),
            PrepassNormal = PrepassField("normalBuffer"), PrepassMotion = PrepassField("resolvedMotionVectorsBuffer");
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
                // Never enabled: the volume only gives the capture pass an owner; HDRP never runs it itself.
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
                // Shadow Detail, optional: without its hook the rest runs as before.
                try
                {
                    if (BeforePostProcess == null) throw new MissingMethodException("HDRenderPipeline.BeforeCustomPostProcessPass");
                    shadowPass = new ShadowDetailPass { name = "TG Path Tracing Shadow Detail", targetColorBuffer = CustomPass.TargetBuffer.None, targetDepthBuffer = CustomPass.TargetBuffer.None };
                    seenPass = new SeenMeterPass { name = "TG Path Tracing seen meter", targetColorBuffer = CustomPass.TargetBuffer.None, targetDepthBuffer = CustomPass.TargetBuffer.None };
                    harmony.Patch(BeforePostProcess, postfix: new HarmonyMethod(typeof(HdrpPasses), nameof(AfterBeforePostProcess)));
                }
                catch (Exception ex) { shadowPass = null; seenPass = null; Session.Log.LogWarning("Shadow Detail is unavailable in this game build (HDRP's post-processing step was not found): " + ex.Message); }
                // Water reflections, optional: without the hook water keeps HDRP's reflections.
                try
                {
                    waterPass = new WaterPass { name = "TG Path Tracing water reflections", targetColorBuffer = CustomPass.TargetBuffer.None, targetDepthBuffer = CustomPass.TargetBuffer.None };
                    harmony.Patch(RenderSsr, postfix: new HarmonyMethod(typeof(HdrpPasses), nameof(AfterSsr)));
                }
                catch (Exception ex) { waterPass = null; Session.Log.LogWarning("Water keeps HDRP's reflections in this game build (its SSR step could not be followed): " + ex.Message); }
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
            CharacterCapture.HideHairLayers(on);
            SuppressDistortion(on);
        }

        // HDRP's distortion (the heat shimmer over candle flames and fires) is off while the traced light is on screen: it
        // resamples the color buffer, which with Ray Reconstruction holds the traced light undenoised, and the noise it
        // shifted around made the air above every candle boil (NVIDIA's Ray Reconstruction guide, "Resampling a noisy
        // image"). The camera's own frame settings (whether it has custom ones, which it overrides, and their values) come
        // back as they were when path tracing stops or the camera changes.
        private static HDAdditionalCameraData distortionCamera;
        private static bool savedCustom;
        private static FrameSettingsOverrideMask savedMask;
        private static FrameSettings savedSettings;
        private static void SuppressDistortion(bool on)
        {
            try
            {
                var cam = on ? Camera.main : null;
                var data = cam != null ? cam.GetComponent<HDAdditionalCameraData>() : null;
                if (distortionCamera != null && distortionCamera != data)
                {
                    distortionCamera.customRenderingSettings = savedCustom;
                    distortionCamera.renderingPathCustomFrameSettingsOverrideMask = savedMask;
                    distortionCamera.renderingPathCustomFrameSettings = savedSettings;
                    distortionCamera = null;
                }
                if (!on || data == null) return;
                if (distortionCamera == null)
                {
                    distortionCamera = data;
                    savedCustom = data.customRenderingSettings;
                    savedMask = data.renderingPathCustomFrameSettingsOverrideMask;
                    savedSettings = data.renderingPathCustomFrameSettings;
                    // Custom settings it had off: only this override takes effect, not whatever its mask held.
                    if (!savedCustom) data.renderingPathCustomFrameSettingsOverrideMask = new FrameSettingsOverrideMask();
                }
                data.customRenderingSettings = true;
                data.renderingPathCustomFrameSettingsOverrideMask.mask[(uint)FrameSettingsField.Distortion] = true;
                data.renderingPathCustomFrameSettings.SetEnabled(FrameSettingsField.Distortion, false);
            }
            catch (Exception ex)
            {
                if (!distortionFailed) { distortionFailed = true; Session.Log.LogWarning("HDRP's distortion could not be turned off while tracing: " + ex.Message); }
            }
        }
        private static bool distortionFailed;

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
        // gain with it (traced_gain_at); the player's Exposure goes along as a factor.
        private static bool fogFailed;
        internal static float FogExtinction, FogBaseHeight, FogScaleHeight;
        private static unsafe void FillFog(HDCamera hd, Settings settings, ref NativeFrame frame)
        {
            frame.Fog[3] = Mathf.Pow(2f, Mathf.Clamp(settings.Exposure.Value, -3f, 3f));
            FogExtinction = 0f;
            if (fogFailed) return;
            try
            {
                var fog = hd.volumeStack?.GetComponent<Fog>();
                if (fog == null || !fog.enabled.value || !(fog.meanFreePath.value > 0f)) return;
                float layer = Mathf.Max(0.01f, fog.maximumHeight.value - fog.baseHeight.value);
                FogExtinction = 1f / fog.meanFreePath.value; FogBaseHeight = fog.baseHeight.value; FogScaleHeight = layer * 0.144765f;
                frame.Fog[0] = FogExtinction; frame.Fog[1] = FogBaseHeight; frame.Fog[2] = 1f / FogScaleHeight;
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
            recorded = executed = submitted = skippedCamera = skippedMode = skippedLit = 0;
            waterRecorded = waterIssued = waterNoSsr = 0;
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
                object prepass = __args[2];
                object gbuffer = PrepassGBuffer.GetValue(prepass);
                var mrt = (TextureHandle[])GBufferMrt.GetValue(gbuffer);
                int count = (int)GBufferCount.GetValue(gbuffer);
                if (mrt == null || count < 3) { ++skippedLit; return; }
                pendingGBuffer2 = mrt[2];
                hasGBuffer3 = count >= 4 && mrt[3].IsValid();
                pendingGBuffer3 = hasGBuffer3 ? mrt[3] : TextureHandle.nullHandle;
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
        private const float MidTone = -3.06f;   // log2 0.12: HDRP's auto exposure puts a scene's average near there
        private const float Highlight = 0.5f;   // log2 1.4: the brightest fiftieth (the 98th percentile) at most 1.4 times white
        private static float tracedLog2Gain;
        internal static float TracedGain => Mathf.Pow(2f, tracedLog2Gain);
        // The traced light's automatic brightening taken back out (the player's Exposure left in), for a light a mod places
        // for a look it set at the game's exposure (SceneLights: the dialogue light). At most 1.
        internal static float AutomaticGainInverse
        {
            get
            {
                var settings = Session.Settings;
                float player = settings != null ? Mathf.Clamp(settings.Exposure.Value, -3f, 3f) : 0f;
                return Mathf.Min(1f, Mathf.Pow(2f, player - tracedLog2Gain));
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

        // Shadow Detail's strength: the row's share, where the view is dark (the automatic gain brightens it by 0 to 2 EV
        // and more) and uneven (its brightest fiftieth 4.5 to 6.5 stops and more above its median): a room lit by one
        // candle, not a lit hall (4.5 stops) or a sunny street (no gain). The activation eases over about a second, from
        // nothing after a camera cut; the row's value applies at once (so switching it compares the two directly). None in a
        // debug view; it holds while a capture compares images.
        private static float shadowActivation;
        internal static float ShadowStrength { get; private set; }
        private static float SmoothStep(float edge0, float edge1, float x) { float t = Mathf.Clamp01((x - edge0) / (edge1 - edge0)); return t * t * (3f - 2f * t); }
        private static void UpdateShadowDetail(Settings settings, bool reset)
        {
            if (settings.CurrentView != DebugView.None) { ShadowStrength = 0f; return; }
            if (DebugCapture.Running) return;
            float automatic = AutomaticGain(settings), target = 0f;
            if (!float.IsNaN(automatic))
                target = SmoothStep(0f, 2f, automatic) * SmoothStep(4.5f, 6.5f, Session.Status.TracedLog2High - Session.Status.TracedLog2Mid);
            if (reset) shadowActivation = 0f;
            float dt = Mathf.Clamp(Time.unscaledDeltaTime, 0f, 0.25f);
            shadowActivation += (1f - Mathf.Exp(-dt / 1f)) * (target - shadowActivation);
            ShadowStrength = Mathf.Clamp(settings.ShadowDetail.Value, 0, 100) / 100f * shadowActivation;
        }
        // Records the Shadow Detail pass on the image HDRP is about to post-process, for the gameplay camera of a frame
        // whose traced light is shown.
        private static void AfterBeforePostProcess(HDRenderPipeline __instance, RenderGraph renderGraph, HDCamera hdCamera, ref TextureHandle __result)
        {
            bool shade = !shadowFailed && shadowPass != null, meter = !seenFailed && seenPass != null;
            if (failed || (!shade && !meter) || hdCamera == null || !__result.IsValid()) return;
            if (captureRecordedFrame != Time.frameCount || !IsGameplayCamera(hdCamera) || !Session.WantsFrames) return;
            var targets = new CustomPass.RenderTargets
            {
                colorBufferRG = __result,
                customColorBuffer = CustomColor.GetValue(__instance) as Lazy<RTHandle>,
                customDepthBuffer = CustomDepth.GetValue(__instance) as Lazy<RTHandle>
            };
            // The meter first: the traced light's exposure is measured without Shadow Detail's lift. Every fourth frame, one
            // readback at a time (a lost one is given up after two seconds).
            if (meter && seenPending && Time.frameCount - seenRequestFrame > 120) seenPending = false;
            if (meter && !seenPending && Time.frameCount >= nextSeenFrame)
            {
                try { ExecuteInternal.Invoke(seenPass, new object[] { renderGraph, hdCamera, default(CullingResults), default(CullingResults), targets, captureVolume }); nextSeenFrame = Time.frameCount + 4; }
                catch (Exception ex) { seenFailed = true; Session.Log.LogWarning("The traced light's exposure no longer measures HDRP's fog this session: " + (ex.InnerException ?? ex).Message); }
            }
            if (!shade || ShadowStrength < 0.002f) return;
            try { ExecuteInternal.Invoke(shadowPass, new object[] { renderGraph, hdCamera, default(CullingResults), default(CullingResults), targets, captureVolume }); }
            catch (Exception ex) { shadowFailed = true; Session.Log.LogWarning("Shadow Detail stopped for this session: " + (ex.InnerException ?? ex).Message); }
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
        internal static void ApplyShadowDetail(CustomPassContext ctx)
        {
            try
            {
                var tracer = Session.Tracer;
                IntPtr color = NativeTextures.Pointer(ctx.cameraColorBuffer);
                if (tracer == null || color == IntPtr.Zero) return;
                uint request = ++shadowRequest;
                if (tracer.SetShadowDetail(request, color, ShadowStrength) != 0) return;
                ctx.cmd.IssuePluginEventAndData(tracer.RenderEvent, Tgpt.EventShadowDetail, new IntPtr((long)request));
            }
            catch (Exception ex) { shadowFailed = true; Session.Log.LogWarning("Shadow Detail stopped for this session: " + ex.Message); }
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
                    " in the last 10 s, traced light x" + TracedGain.ToString("0.0") + " (" + tracedLog2Gain.ToString("0.0") + " EV" +
                    (Session.HaveStatus && Session.Status.TracedSamples > 0 ? "; its median log2 " + Session.Status.TracedLog2Mid.ToString("0.0") +
                        ", 98th percentile " + Session.Status.TracedLog2High.ToString("0.0") : "") +
                    (!float.IsNaN(seenLog2Mid) && Time.unscaledTime - seenAt < 1f ? "; seen with the fog: median log2 " + seenLog2Mid.ToString("0.0") +
                        ", " + (seenLog2Mid - seenLog2Gain).ToString("0.0") + " without the gain" : "; seen with the fog: not measured") +
                    "), shadow detail " + (ShadowStrength * 100f).ToString("0") +
                    "%, light at the camera " + cameraLight.ToString("0.00") + " of the open sky's";
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
        // How far light scatters under a solid profile's surface (pt_sss.hlsl), from HDRP's own values for its subsurface
        // pass: Burley's shape per channel (per millimetre) and the filter radius (millimetres), in the world's metres by
        // the profile's world scale (metres per unit). Kept per profile; the table goes to the tracer when it changes.
        private static readonly Dictionary<DiffusionProfileSettings, Vector4> scattering = new Dictionary<DiffusionProfileSettings, Vector4>();
        private static PropertyInfo shapeProperty, radiusProperty;
        private static FieldInfo worldScaleField;
        private static readonly float[] scatterTable = new float[64], scatterSent = new float[64];
        private static bool scatterKnown, scatterFailed;
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
        // Light through thin skin (TGPT_SetSkinTransmission): the constants HDRP's own shaders transmit through a profile with,
        // as its DiffusionProfileSettings keeps them for them (UpdateCache): the tint / 4, Burley's shape per millimetre and
        // the thickness remap. The tint is 0 while the camera's frame settings leave transmission out, as HDRP zeroes it then.
        private static readonly Dictionary<DiffusionProfileSettings, (Vector4 tint, Vector4 shape, Vector4 remap)> transmission =
            new Dictionary<DiffusionProfileSettings, (Vector4, Vector4, Vector4)>();
        private static FieldInfo tintField, shapeField, remapField;
        private static readonly float[] transmissionTable = new float[128], transmissionSent = new float[128];
        private static bool transmissionKnown, transmissionFailed;
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
        private static uint ThickProfiles(HDCamera hd)
        {
            try
            {
                var profiles = hd?.volumeStack?.GetComponent<DiffusionProfileList>()?.diffusionProfiles.value;
                uint mask = 0;
                if (profiles != null)
                    for (int i = 0; i < profiles.Length && i < 16; i++)
                        if (profiles[i] != null && IsThick(profiles[i])) mask |= 1u << i;
                // The thick profiles' transmission, for the tracer when it changes.
                bool transmits = hd != null && hd.frameSettings.IsEnabled(FrameSettingsField.Transmission);
                for (int i = 0; i < 16; i++)
                {
                    var t = transmits && profiles != null && i < profiles.Length && profiles[i] != null && ((mask >> i) & 1u) != 0 ? TransmissionOf(profiles[i]) : default;
                    transmissionTable[i * 8] = t.tint.x; transmissionTable[i * 8 + 1] = t.tint.y; transmissionTable[i * 8 + 2] = t.tint.z; transmissionTable[i * 8 + 3] = t.remap.z;
                    transmissionTable[i * 8 + 4] = t.shape.x; transmissionTable[i * 8 + 5] = t.shape.y; transmissionTable[i * 8 + 6] = t.shape.z; transmissionTable[i * 8 + 7] = t.remap.w;
                }
                bool sameTransmission = transmissionKnown;
                for (int i = 0; i < 128 && sameTransmission; i++) sameTransmission = transmissionTable[i] == transmissionSent[i];
                if (!sameTransmission && Session.Tracer != null && Session.Tracer.SetSkinTransmission(transmissionTable, 16) == 0)
                {
                    Array.Copy(transmissionTable, transmissionSent, 128); transmissionKnown = true;
                }
                // The thick profiles' scattering, for the tracer when it changes.
                bool same = scatterKnown;
                for (int i = 0; i < 16; i++)
                {
                    var v = profiles != null && i < profiles.Length && profiles[i] != null && ((mask >> i) & 1u) != 0 ? ScatteringOf(profiles[i]) : Vector4.zero;
                    scatterTable[i * 4] = v.x; scatterTable[i * 4 + 1] = v.y; scatterTable[i * 4 + 2] = v.z; scatterTable[i * 4 + 3] = v.w;
                }
                for (int i = 0; i < 64 && same; i++) same = scatterTable[i] == scatterSent[i];
                if (!same && Session.Tracer != null && Session.Tracer.SetDiffusionProfiles(scatterTable, 16) == 0)
                {
                    Array.Copy(scatterTable, scatterSent, 64); scatterKnown = true;
                }
                if (mask != thickProfiles)
                {
                    thickProfiles = mask;
                    var names = new System.Text.StringBuilder();
                    for (int i = 0; profiles != null && i < profiles.Length && i < 16; i++)
                        if (profiles[i] != null)
                        {
                            names.Append(names.Length > 0 ? ", " : "").Append(i).Append(' ').Append(profiles[i].name).Append(((mask >> i) & 1u) != 0 ? " (thick" : " (thin");
                            if (((mask >> i) & 1u) != 0 && scatterTable[i * 4 + 3] > 0f) names.Append(", scatters within ").Append((scatterTable[i * 4 + 3] * 1000f).ToString("0.#")).Append(" mm");
                            if (((mask >> i) & 1u) != 0 && transmissionTable[i * 8] + transmissionTable[i * 8 + 1] + transmissionTable[i * 8 + 2] > 0f)
                                names.Append(", lets through a tint of (").Append((transmissionTable[i * 8] * 4f).ToString("0.00")).Append(", ")
                                    .Append((transmissionTable[i * 8 + 1] * 4f).ToString("0.00")).Append(", ").Append((transmissionTable[i * 8 + 2] * 4f).ToString("0.00"))
                                    .Append(") where ").Append(transmissionTable[i * 8 + 3].ToString("0.#")).Append(" to ")
                                    .Append((transmissionTable[i * 8 + 3] + transmissionTable[i * 8 + 7]).ToString("0.#")).Append(" mm thick");
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
        internal static void DeclareExtraReads(RenderGraphBuilder builder)
        {
            if (pendingGBuffer2.IsValid()) builder.ReadTexture(in pendingGBuffer2);
            if (hasGBuffer3) builder.ReadTexture(in pendingGBuffer3);
        }

        // Render-graph execution of the capture pass, main thread: the frame to the native side, then render event 1.
        internal static unsafe void Capture(CustomPassContext ctx)
        {
            ++executed; unexecuted = 0;
            if (!armed) return;
            armed = false;
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
                var view = hd.mainViewConstants;
                var camera = view.worldSpaceCameraPos;
                var frame = new NativeFrame
                {
                    Size = 568, Abi = Tgpt.Abi, Sequence = ++sequence,
                    GBuffer0 = Ptr(gbuffer0), Normal = Ptr(normal), GBuffer2 = Ptr(gbuffer2), GBuffer3 = Ptr(gbuffer3),
                    Depth = Ptr(depth), Motion = Ptr(motion), Exposure = Ptr(exposure),
                    RenderWidth = (uint)Mathf.Max(0, hd.actualWidth), RenderHeight = (uint)Mathf.Max(0, hd.actualHeight),
                    DebugView = (uint)settings.CurrentView,
                    FrameIndex = (uint)Time.frameCount, Bounces = (uint)settings.Bounces.Value, SpatialSamples = (uint)settings.SpatialSamples.Value,
                    RaysPerPixel = DebugCapture.Rays != 0 ? DebugCapture.Rays : (uint)settings.RaysPerPixel.Value,
                    // ReSTIR's histories in frames, which NVIDIA RTXDI's reuse decorrelates for the denoisers (permutation
                    // sampling, the boiling filter): its defaults of 20 for direct light and 8 for bounced light, which
                    // NVIDIA's sample keeps with DLSS Ray Reconstruction too, unless the config's Direct light history
                    // or Bounced light history sets another. (Before RTXDI a history of 1 was the least grainy: the
                    // tracer's own reuse permuted too little, and longer reuse made noise stand still.)
                    DiHistory = settings.DirectLightHistory.Value > 0 ? (uint)settings.DirectLightHistory.Value : 20u,
                    GiHistory = settings.BouncedLightHistory.Value > 0 ? (uint)settings.BouncedLightHistory.Value : 8u,
                    TracedGain = TracedGain,
                    ThickProfiles = ThickProfiles(hd),
                    Options = settings.Options | DebugCapture.ExtraOptions | (RayReconstruction.Active ? Tgpt.OptionRayReconstruction : Session.WantsRayRegeneration ? Tgpt.OptionRayRegeneration : 0u),
                    // Indoors rays see no sky (SceneLights.Interior: interiors float in the sky above the world, and
                    // a ray through a seam between wall and floor found it: thin blue-white lines along the seams).
                    MaxRayDistance = settings.RayDistance.Value, SkyIntensity = DebugCapture.SkyOff || SceneLights.Interior ? 1e-6f : settings.SkyIntensity.Value,
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
                UpdateShadowDetail(settings, (frame.Flags & Tgpt.FrameReset) != 0);
                // DLSS Ray Reconstruction's guides, which TG Better Upscaling fetches later in this frame.
                if (Session.WantsRayReconstruction) { frame.Flags |= Tgpt.FrameGuides; RayReconstruction.GuidesFrame = Time.frameCount; }
                Copy(view.invViewProjMatrix, frame.InvViewProj);
                Copy(view.viewProjMatrix, frame.ViewProj);
                Copy(view.prevViewProjMatrix, frame.PrevViewProj);
                Copy(view.viewMatrix, frame.View);
                Copy(view.nonJitteredProjMatrix, frame.NonJitteredProj);
                frame.Jitter[0] = hd.taaJitter.x; frame.Jitter[1] = hd.taaJitter.y;
                FillFog(hd, settings, ref frame);
                // The share of the game's ambient light HDRP leaves hair and eyes (RaiseForwardAmbient): the tracer adds its
                // bounced light for the rest (pt_compose).
                frame.Forward[0] = ForwardAmbientKept;
                var prev = haveLastCamera ? lastCameraPosition : camera;
                frame.CameraPosition[0] = camera.x; frame.CameraPosition[1] = camera.y; frame.CameraPosition[2] = camera.z;
                frame.PrevCameraPosition[0] = prev.x; frame.PrevCameraPosition[1] = prev.y; frame.PrevCameraPosition[2] = prev.z;
                lastCameraPosition = camera; haveLastCamera = true;
                var sun = SceneLights.Sun;
                // Shadow maps are the camera's (the game's Shadows toggle): what HDRP shadows itself takes no traced shadow twice.
                ShadowMaps = hd.frameSettings.IsEnabled(FrameSettingsField.ShadowMaps);
                if (sun.Valid)
                {
                    frame.SunDirection[0] = sun.Direction.x; frame.SunDirection[1] = sun.Direction.y; frame.SunDirection[2] = sun.Direction.z; frame.SunDirection[3] = sun.AngularRadius;
                    frame.SunColor[0] = sun.Color.x; frame.SunColor[1] = sun.Color.y; frame.SunColor[2] = sun.Color.z;
                    frame.SunColor[3] = sun.HdrpShadows && ShadowMaps ? 2f : 1f; // 2: HDRP shadows it itself (its cascades)
                }
                if (frame.RenderWidth == 0 || frame.RenderHeight == 0) return;
                // This frame's characters, posed by the skinning that ran before this camera; they go with the frame.
                CharacterCapture.Capture(camera);
                int result = tracer.SubmitFrame(ref frame);
                if (result != 0) { LastFrame = "the native side refused the frame (" + result + ")"; return; }
                ctx.cmd.IssuePluginEventAndData(tracer.RenderEvent, Tgpt.EventTrace, new IntPtr((long)frame.Sequence));
                pendingComposite = frame.Sequence; pendingCompositeFrame = Time.frameCount;
                pendingWater = frame.Sequence; pendingWaterFrame = Time.frameCount;
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
        private static void AfterSsr(HDRenderPipeline __instance, RenderGraph renderGraph, HDCamera hdCamera, bool transparent, object[] __args, TextureHandle __result)
        {
            if (!transparent || failed || waterFailed || waterPass == null || hdCamera == null) return;
            if (captureRecordedFrame != Time.frameCount || !IsGameplayCamera(hdCamera) || !Session.WantsFrames) return;
            if (!Session.Settings.WaterReflections.Value || DebugCapture.WaterOff) return;
            try
            {
                // Without SSR this frame (off in the frame settings, or no history after a cut) the step returns HDRP's shared
                // black texture, which is never written.
                if (!__result.IsValid() || (TextureBuiltin != null && TextureBuiltin.GetValue(__result) is bool builtin && builtin)) { ++waterNoSsr; return; }
                object prepass = __args[2];
                var targets = new CustomPass.RenderTargets
                {
                    colorBufferRG = __result,
                    depthBufferRG = (TextureHandle)PrepassDepth.GetValue(prepass),
                    normalBufferRG = (TextureHandle)PrepassNormal.GetValue(prepass),
                    customColorBuffer = CustomColor.GetValue(__instance) as Lazy<RTHandle>,
                    customDepthBuffer = CustomDepth.GetValue(__instance) as Lazy<RTHandle>
                };
                // HDRP multiplies a surface's whole reflection by it, its SSR's part too (LightLoop.hlsl), and the scaled
                // reflections volume brings the camera's light into it: the native side divides it out of what it writes.
                waterMultiplier = hdCamera.volumeStack.GetComponent<IndirectLightingController>().reflectionLightingMultiplier.value;
                ExecuteInternal.Invoke(waterPass, new object[] { renderGraph, hdCamera, default(CullingResults), default(CullingResults), targets, captureVolume });
                ++waterRecorded;
            }
            catch (Exception ex)
            {
                waterFailed = true;
                Session.Log.LogWarning("Water keeps HDRP's reflections for the rest of the session (recording the water reflections failed): " + (ex.InnerException ?? ex).Message);
            }
        }
        // Render-graph execution of the water pass: the frame the capture handed over, then render event 4.
        internal static void Water(CustomPassContext ctx)
        {
            if (pendingWater == 0 || pendingWaterFrame != Time.frameCount || !IsGameplayCamera(ctx.hdCamera)) return;
            uint seq = pendingWater; pendingWater = 0;
            try
            {
                var tracer = Session.Tracer;
                IntPtr reflections = NativeTextures.Pointer(ctx.cameraColorBuffer), depth = NativeTextures.Pointer(ctx.cameraDepthBuffer),
                    normal = NativeTextures.Pointer(ctx.cameraNormalBuffer);
                if (tracer == null || reflections == IntPtr.Zero || depth == IntPtr.Zero || normal == IntPtr.Zero) return;
                if (tracer.SetWater(seq, reflections, depth, normal, waterMultiplier) != 0) return;
                ctx.cmd.IssuePluginEventAndData(tracer.RenderEvent, Tgpt.EventWater, new IntPtr((long)seq));
                ++waterIssued;
            }
            catch (Exception ex)
            {
                waterFailed = true;
                Session.Log.LogWarning("Water keeps HDRP's reflections for the rest of the session: " + ex.Message);
            }
        }
        // For the 10 s line and the capture's report.
        internal static string WaterText =>
            !Session.Settings.WaterReflections.Value ? "water reflections off" : waterPass == null || waterFailed ? "water reflections unavailable (the log says why)" :
            "water reflections: recorded " + waterRecorded + ", handed over " + waterIssued +
            (waterNoSsr > 0 ? ", frames without HDRP's transparent SSR " + waterNoSsr : "") +
            (Session.HaveStatus ? "; traced " + Session.Status.WaterTraced + ", applied " + Session.Status.WaterApplied + " in all" : "");

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
            var installed = harmony;
            harmony = null;
            try { installed?.UnpatchSelf(); } catch (Exception ex) { Session.Log?.LogWarning("Path tracing unpatch failed: " + ex.Message); }
            foreach (var host in new[] { captureHost, compositeHost, indirectHost, scaledHost }) if (host != null) UnityEngine.Object.Destroy(host);
            if (seenCopy != null) { RTHandles.Release(seenCopy); seenCopy = null; }
            captureHost = compositeHost = indirectHost = scaledHost = null; captureVolume = null; capturePass = null; indirectVolume = scaledVolume = null; armed = false;
            waterPass = null; pendingWater = 0;
        }
    }
}
