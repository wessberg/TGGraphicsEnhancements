// SPDX-License-Identifier: MIT
using System;
using System.Reflection;
using TGGraphicsBridge;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.HighDefinition;
namespace TGNeuralRendering
{
    internal static class SceneCapture
    {
        private static bool active, reset = true;
        private static uint lastFrame;
        private static int cameraId;
        private static Vector3 previousPosition;
        private static Quaternion previousRotation;
        private static NeuralFrame guideFrame;
        private static bool haveGuides;
        private static FieldInfo ptSettings;
        private static PropertyInfo ptView, ptMode, ptDenoiser;
        internal static bool BeforeAvailable
        {
            get
            {
                var settings = ptSettings?.GetValue(null);
                if (settings == null || ptMode == null || Convert.ToInt32(ptMode.GetValue(settings)) == 0) return true;
                var denoiser = ptDenoiser?.GetValue(settings)?.ToString() ?? "";
                return denoiser.IndexOf("Reconstruction", StringComparison.OrdinalIgnoreCase) < 0;
            }
        }
        internal static string BlockReason
        {
            get
            {
                var settings = ptSettings?.GetValue(null);
                if (settings == null || ptMode == null || Convert.ToInt32(ptMode.GetValue(settings)) == 0) return null;
                if (ptView != null && Convert.ToInt32(ptView.GetValue(settings)) != 0) return "DLSS Neural Rendering is bypassed while a path tracing debug view is shown.";
                if (ptDenoiser != null && Convert.ToInt32(ptDenoiser.GetValue(settings)) == 0) return "Select a path tracing denoiser to supply a clean image to DLSS Neural Rendering.";
                return null;
            }
        }
#if !BEPINEX_IL2CPP
        private static readonly Func<HDRenderPipeline, HDCamera, RTHandle> ExposureTexture =
            HarmonyLib.AccessTools.Method(typeof(HDRenderPipeline), "GetExposureTexture", new[] { typeof(HDCamera) }) is MethodInfo exposureTexture
                ? (Func<HDRenderPipeline, HDCamera, RTHandle>)Delegate.CreateDelegate(typeof(Func<HDRenderPipeline, HDCamera, RTHandle>), exposureTexture) : null;
        private static readonly FieldInfo historyReset = typeof(HDCamera).GetField("resetPostProcessingHistory", BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
        private static readonly FieldInfo historyWasReset = typeof(HDCamera).GetField("didResetPostProcessingHistoryInLastFrame", BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
#endif
        internal static void Start()
        {
            try { UpscalePlacement.Start(); }
            catch { UpscalePlacement.Stop(); throw; }
            // Optional PT observation only; NR has no assembly dependency on the tracer.
            foreach (var assembly in AppDomain.CurrentDomain.GetAssemblies())
            {
                if (assembly.GetName().Name != "TGPathTracing") continue;
                var type = assembly.GetType("TGPathTracing.Session");
                ptSettings = type?.GetField("Settings", BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic);
                ptView = ptSettings?.FieldType.GetProperty("CurrentView", BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
                ptMode = ptSettings?.FieldType.GetProperty("CurrentMode", BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
                ptDenoiser = ptSettings?.FieldType.GetProperty("CurrentDenoiser", BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
            }
        }
        internal static bool Gameplay(HDCamera camera)
        {
            var c = camera?.camera; var main = Camera.main;
            if (c == null || main == null || c.GetInstanceID() != main.GetInstanceID() || c.cameraType != CameraType.Game || c.orthographic || c.stereoEnabled || c.targetTexture != null) return false;
            if (Mathf.Abs(c.pixelWidth - Screen.width) > 2 || Mathf.Abs(c.pixelHeight - Screen.height) > 2 || c.pixelRect.x > 2 || c.pixelRect.y > 2) return false;
            var settings = ptSettings?.GetValue(null);
            if (settings != null && ptView != null && ptMode != null && Convert.ToInt32(ptMode.GetValue(settings)) != 0 && Convert.ToInt32(ptView.GetValue(settings)) != 0) { Reset(); return false; }
            return true;
        }
        internal static void SetActive(bool want)
        {
            if (active == want) return;
            active = want; Reset();
            if (!want && Session.Native != null && Session.Native.RenderEvent != IntPtr.Zero)
            {
                var commands = new CommandBuffer { name = "TGNR release frame resources" };
                commands.IssuePluginEventAndData(Session.Native.RenderEvent, 3, IntPtr.Zero);
                Graphics.ExecuteCommandBuffer(commands); commands.Release();
            }
        }
        internal static void Reset() { reset = true; haveGuides = false; }
        internal static void Render(CustomPassContext ctx, bool begin)
        {
            try { if (begin) ArmDlss(ctx); else Color(ctx); }
            catch (Exception ex) { Fail(ex); }
        }
        internal static void Fail(Exception ex)
        {
            Session.Fault = "Neural Rendering could not capture this frame: " + ex.Message;
            Session.Log.LogError(ex); Reset();
        }
        private static void ArmDlss(CustomPassContext ctx)
        {
            if (!active || !Gameplay(ctx.hdCamera) || Session.HdrActive) return;
            var hd = ctx.hdCamera; var c = hd.camera;
            if (!hd.frameSettings.IsEnabled(FrameSettingsField.MotionVectors) || !hd.frameSettings.IsEnabled(FrameSettingsField.ObjectMotionVectors))
            {
                Session.Fault = "The gameplay camera is not providing camera and object motion vectors. Neural Rendering is disabled for this session.";
                Reset(); return;
            }
            uint frame = unchecked((uint)Time.frameCount);
            int id = c.GetInstanceID();
#if BEPINEX_IL2CPP
            bool engineReset = hd.resetPostProcessingHistory || hd.didResetPostProcessingHistoryInLastFrame || hd.isFirstFrame;
#else
            bool engineReset = (historyReset?.GetValue(hd) is bool value && value) || (historyWasReset?.GetValue(hd) is bool wasReset && wasReset);
#endif
            bool cut = reset || engineReset || cameraId != id || frame != lastFrame + 1 || Vector3.Distance(previousPosition, c.transform.position) > 2f || Quaternion.Angle(previousRotation, c.transform.rotation) > 35f;
            var f = new NeuralFrame {
                Size = 104, Abi = NativeNeural.Abi, Frame = frame, Camera = unchecked((uint)id),
                Width = (uint)(Session.BeforeUpscaling ? hd.actualWidth : c.pixelWidth), Height = (uint)(Session.BeforeUpscaling ? hd.actualHeight : c.pixelHeight), RenderWidth = (uint)hd.actualWidth, RenderHeight = (uint)hd.actualHeight,
                // Only the source identifies this camera. Actual depth, motion, jitter and scales come from DLSS.
                Color = unchecked((ulong)NativeTextures.Pointer(ctx.cameraColorBuffer).ToInt64()),
                Flags = (SystemInfo.usesReversedZBuffer ? 1u : 0u) | (cut ? 2u : 0u) | 8u | (Session.BeforeUpscaling ? 16u : 0u), Strength = Session.Strength.Value,
                Model = (uint)Array.IndexOf(Session.Models, Session.Model.Value), LocalTone = Session.LocalTone.Value,
                LocalStructure = Session.LocalStructure.Value, SkinStructure = Session.SkinStructure.Value,
                AutoMask = Session.AutoMask.Value == "On" ? 1u : 0u,
                Passes = (uint)Session.Passes.Value,
                JitterX = hd.taaJitter.x, JitterY = hd.taaJitter.y, Near = c.nearClipPlane, Far = c.farClipPlane };
            var ticket = Session.Native.Submit(ref f, 4);
            if (ticket == IntPtr.Zero) { Reset(); return; }
            if (Session.Native.DiagnosticsEnabled)
            {
                var pipeline = RenderPipelineManager.currentPipeline as HDRenderPipeline;
#if BEPINEX_IL2CPP
                var exposure = pipeline == null ? null : pipeline.GetExposureTexture(hd);
#else
                var exposure = pipeline == null ? null : ExposureTexture?.Invoke(pipeline, hd);
#endif
                Session.Native.AttachDiagnosticExposure(ticket, NativeTextures.Pointer(exposure));
            }
            ctx.cmd.IssuePluginEventAndData(Session.Native.RenderEvent, 4, ticket);
            guideFrame = f; haveGuides = true;
            cameraId = id; lastFrame = frame; previousPosition = c.transform.position; previousRotation = c.transform.rotation;
        }
        internal static void ToneMappedColor(CustomPassContext ctx)
        {
            try { Color(ctx, true); }
            catch (Exception ex) { Fail(ex); }
        }
        private static void Color(CustomPassContext ctx, bool toneMapped = false)
        {
            if (!active || !haveGuides || !Gameplay(ctx.hdCamera) || Session.HdrActive) return;
            if (guideFrame.Frame != unchecked((uint)Time.frameCount) || guideFrame.Camera != unchecked((uint)ctx.hdCamera.camera.GetInstanceID())) { Reset(); return; }
            var rt = ctx.cameraColorBuffer?.rt;
            if (rt == null) { Reset(); return; }
            var f = guideFrame; f.Depth = f.Motion = 0;
            f.Color = unchecked((ulong)NativeTextures.Pointer(ctx.cameraColorBuffer).ToInt64());
            // Both placements are before lens effects/tone mapping. Keep scene highlights through
            // the reversible HDR adapter; never pass raw HDR to the model or clip it to SDR.
            f.Flags |= 8u;
            if (toneMapped) f.Flags &= ~8u;
            if (rt.sRGB) f.Flags |= 4u;
            bool defer = !toneMapped && Session.Native.PostToneMapDiagnostic;
            uint stage = toneMapped ? 6u : defer ? 7u : 5u;
            var ticket = Session.Native.Submit(ref f, stage);
            if (ticket == IntPtr.Zero) { Reset(); return; }
            ctx.cmd.IssuePluginEventAndData(Session.Native.RenderEvent, (int)stage, ticket);
            if (defer) return;
            haveGuides = false; reset = false;
        }
        internal static void Stop()
        {
            SetActive(false); UpscalePlacement.Stop();
        }
    }
}
