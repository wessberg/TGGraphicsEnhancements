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
using System;
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

    internal static class HdrpPasses
    {
        private static Harmony harmony;
        private static GameObject captureHost, compositeHost, indirectHost;
        private static CustomPassVolume captureVolume;
        private static CapturePass capturePass;
        private static Volume indirectVolume;
        private static bool failed;
        internal static string Detail { get; private set; } = "Not installed.";
        // The frame recorded in RenderSSR, completed when the capture pass executes.
        private static TextureHandle pendingGBuffer2, pendingGBuffer3;
        private static bool armed, hasGBuffer3;
        private static int unexecuted;
        private static uint sequence, pendingComposite;
        private static int pendingCompositeFrame = -1;
        private static Vector3 lastCameraPosition; private static bool haveLastCamera;
        internal static ulong Captured { get; private set; }
        internal static ulong Composited { get; private set; }
        internal static string LastFrame { get; private set; } = "none yet";
        private static long recorded, executed, submitted, skippedCamera, skippedMode, skippedLit;
        private static float nextReport;

        private static readonly FieldInfo CustomColor = AccessTools.Field(typeof(HDRenderPipeline), "m_CustomPassColorBuffer");
        private static readonly FieldInfo CustomDepth = AccessTools.Field(typeof(HDRenderPipeline), "m_CustomPassDepthBuffer");
        private static readonly MethodInfo ExecuteInternal = AccessTools.Method(typeof(CustomPass), "ExecuteInternal");
        private static readonly MethodInfo RenderSsr = AccessTools.Method(typeof(HDRenderPipeline), "RenderSSR");
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
                // other mods', which use priorities up to 10000).
                indirectHost = new GameObject("TG Path Tracing indirect light") { hideFlags = HideFlags.HideAndDontSave };
                indirectHost.SetActive(false);
                UnityEngine.Object.DontDestroyOnLoad(indirectHost);
                indirectVolume = indirectHost.AddComponent<Volume>();
                indirectVolume.isGlobal = true;
                indirectVolume.priority = 1000000f;
                indirectVolume.weight = 1f;
                var profile = ScriptableObject.CreateInstance<VolumeProfile>();
                profile.hideFlags = HideFlags.HideAndDontSave;
                profile.Add<IndirectLightingController>(true).indirectDiffuseLightingMultiplier.Override(0f);
                profile.Add<GlobalIllumination>(true).enable.Override(false);
                profile.Add<ScreenSpaceAmbientOcclusion>(true).intensity.Override(0f);
                profile.Add<ContactShadows>(true).enable.Override(false);
                profile.Add<ScreenSpaceReflection>(true).enabled.Override(false);
                indirectVolume.sharedProfile = profile;
                var candidate = new Harmony(PathTracingPlugin.Id);
                try { candidate.Patch(RenderSsr, prefix: new HarmonyMethod(typeof(HdrpPasses), nameof(BeforeSsr))); harmony = candidate; }
                catch { candidate.UnpatchSelf(); throw; }
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
        }

        // Forward-drawn surfaces (eyes, hair: HDRP's Eye and Hair shaders draw forward only, outside the composite) take
        // HDRP's baked ambient light from the indirect diffuse multiplier when they are shaded, which the volume above
        // sets to 0 for the GBuffer's sake: they were black in shade. While HDRP records the camera's
        // passes after the GBuffer (RenderSSR, where the capture is recorded), its global constants get the game's own
        // multiplier (its day and night curve): the push RenderShadows records next carries it to the deferred
        // lighting (whose result the composite replaces) and the forward passes. The GBuffer was drawn with the
        // camera's first push, with 0; HDRP fills its constants from the volumes again at the next camera. (A push of
        // our own from the capture pass was overwritten by RenderShadows' before the forward passes drew anything.)
        private static FieldInfo globalConstantsField, multiplierField;
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
                float value = GameIndirectMultiplier();
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
                " | composited " + Composited + " | last: " + LastFrame);
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
                ++recorded;
                RaiseForwardAmbient(__instance);
            }
            catch (Exception ex)
            {
                armed = false;
                Fail("Recording the path tracing capture failed: " + ex);
            }
        }

        // HDRP's exposure averaged over about ten seconds (in log space, a GPU readback a frame or two old): the tracer's
        // radiance clamps are measured against it, so turning the camera between bright and dark, which moves HDRP's auto
        // exposure within a second, no longer changes how much of the bounced light they cut, while walking into a dark
        // hall still adapts them. 0 when the exposure can't be read (the tracer then uses HDRP's current one).
        private const float ClampExposureSeconds = 10f;
        private static float clampExposureLog;
        private static bool haveClampExposure;
        private static readonly Action<HDCamera, RTHandle> RequestExposure =
            AccessTools.Method(typeof(HDCamera), "RequestGpuExposureValue", new[] { typeof(RTHandle) }) is MethodInfo request
                ? (Action<HDCamera, RTHandle>)Delegate.CreateDelegate(typeof(Action<HDCamera, RTHandle>), request) : null;
        private static readonly Func<HDCamera, float> ExposureValue =
            AccessTools.Method(typeof(HDCamera), "GpuExposureValue", Type.EmptyTypes) is MethodInfo value
                ? (Func<HDCamera, float>)Delegate.CreateDelegate(typeof(Func<HDCamera, float>), value) : null;
        private static float ClampExposure(HDRenderPipeline pipeline, HDCamera hd, bool reset)
        {
            try
            {
                if (ExposureTexture == null || RequestExposure == null || ExposureValue == null) return 0f;
                var texture = ExposureTexture(pipeline, hd);
                if (texture != null) RequestExposure(hd, texture);
                float e = ExposureValue(hd);
                if (!(e > 0f) || float.IsInfinity(e)) return haveClampExposure ? Mathf.Pow(2f, clampExposureLog) : 0f;
                float log = Mathf.Log(e, 2f);
                if (reset || !haveClampExposure) { clampExposureLog = log; haveClampExposure = true; }
                else clampExposureLog += (log - clampExposureLog) * (1f - Mathf.Exp(-Mathf.Max(Time.unscaledDeltaTime, 0f) / ClampExposureSeconds));
                return Mathf.Pow(2f, clampExposureLog);
            }
            catch { return 0f; }
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
                    Size = 528, Abi = Tgpt.Abi, Sequence = ++sequence,
                    GBuffer0 = Ptr(gbuffer0), Normal = Ptr(normal), GBuffer2 = Ptr(gbuffer2), GBuffer3 = Ptr(gbuffer3),
                    Depth = Ptr(depth), Motion = Ptr(motion), Exposure = Ptr(exposure),
                    RenderWidth = (uint)Mathf.Max(0, hd.actualWidth), RenderHeight = (uint)Mathf.Max(0, hd.actualHeight),
                    DebugView = (uint)settings.CurrentView,
                    FrameIndex = (uint)Time.frameCount, Bounces = (uint)settings.Bounces.Value, SpatialSamples = (uint)settings.SpatialSamples.Value,
                    RaysPerPixel = (uint)settings.RaysPerPixel.Value,
                    TemporalCap = 1, // ReSTIR reuses a sample for about a frame: the denoisers average over time (Settings)
                    Options = settings.Options | (RayReconstruction.Active ? Tgpt.OptionRayReconstruction : Session.WantsRayRegeneration ? Tgpt.OptionRayRegeneration : 0u),
                    MaxRayDistance = settings.RayDistance.Value, SkyIntensity = settings.SkyIntensity.Value,
                    IndirectColor = settings.IndirectColor.Value, ReflectionBoost = settings.ReflectionBoost.Value
                };
                if (!haveLastCamera || (camera - lastCameraPosition).sqrMagnitude > 400f) frame.Flags |= Tgpt.FrameReset;
                if (pipeline != null) frame.ClampExposure = ClampExposure(pipeline, hd, (frame.Flags & Tgpt.FrameReset) != 0);
                // DLSS Ray Reconstruction's guides, which TG Better Upscaling fetches later in this frame.
                if (Session.WantsRayReconstruction) { frame.Flags |= Tgpt.FrameGuides; RayReconstruction.GuidesFrame = Time.frameCount; }
                Copy(view.invViewProjMatrix, frame.InvViewProj);
                Copy(view.viewProjMatrix, frame.ViewProj);
                Copy(view.prevViewProjMatrix, frame.PrevViewProj);
                Copy(view.viewMatrix, frame.View);
                Copy(view.nonJitteredProjMatrix, frame.NonJitteredProj);
                frame.Jitter[0] = hd.taaJitter.x; frame.Jitter[1] = hd.taaJitter.y;
                var prev = haveLastCamera ? lastCameraPosition : camera;
                frame.CameraPosition[0] = camera.x; frame.CameraPosition[1] = camera.y; frame.CameraPosition[2] = camera.z;
                frame.PrevCameraPosition[0] = prev.x; frame.PrevCameraPosition[1] = prev.y; frame.PrevCameraPosition[2] = prev.z;
                lastCameraPosition = camera; haveLastCamera = true;
                var sun = SceneLights.Sun;
                if (sun.Valid)
                {
                    frame.SunDirection[0] = sun.Direction.x; frame.SunDirection[1] = sun.Direction.y; frame.SunDirection[2] = sun.Direction.z; frame.SunDirection[3] = sun.AngularRadius;
                    frame.SunColor[0] = sun.Color.x; frame.SunColor[1] = sun.Color.y; frame.SunColor[2] = sun.Color.z; frame.SunColor[3] = 1f;
                }
                if (frame.RenderWidth == 0 || frame.RenderHeight == 0) return;
                // This frame's characters, posed by the skinning that ran before this camera; they go with the frame.
                CharacterCapture.Capture(camera);
                int result = tracer.SubmitFrame(ref frame);
                if (result != 0) { LastFrame = "the native side refused the frame (" + result + ")"; return; }
                ctx.cmd.IssuePluginEventAndData(tracer.RenderEvent, Tgpt.EventTrace, new IntPtr((long)frame.Sequence));
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
            foreach (var host in new[] { captureHost, compositeHost, indirectHost }) if (host != null) UnityEngine.Object.Destroy(host);
            captureHost = compositeHost = indirectHost = null; captureVolume = null; capturePass = null; indirectVolume = null; armed = false;
        }
    }
}
