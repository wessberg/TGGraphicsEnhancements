// SPDX-License-Identifier: MIT
// HDRP's own depth and motion vectors as the frame generation inputs when no upscaler provides
// them: TAA, STP or no anti-aliasing, and GPUs without DLSS. A global custom pass runs before
// post-processing, where HDRP has finished the frame's depth and motion vectors; the copy itself
// happens on the render thread (render event 3). While DLSS runs, the native side uses DLSS's
// inputs instead.
using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.HighDefinition;
using TGGraphicsBridge;
#if BEPINEX_IL2CPP
using Il2CppInterop.Runtime.Injection;
#endif
namespace TGFrameGeneration
{
    internal sealed class FrameInputPass : CustomPass
    {
#if BEPINEX_IL2CPP
        public FrameInputPass(IntPtr pointer) : base(pointer) { }
        internal FrameInputPass() : base(ClassInjector.DerivedConstructorPointer<FrameInputPass>())
        {
            ClassInjector.DerivedConstructorBody(this);
            Il2CppTypes.Construct(this, typeof(CustomPass), "NativeMethodInfoPtr__ctor_Protected_Void_0");
        }
        private new void Execute(CustomPassContext ctx) => HdrpInputs.Execute(ctx); // see Il2CppTypes
#else
        protected override void Execute(CustomPassContext ctx) => HdrpInputs.Execute(ctx);
#endif
    }
    internal static class HdrpInputs
    {
        private const int CaptureEvent = 3;
        private static GameObject host;
        private static int lastResult = int.MinValue;
        private static int lastMotionFlags = -1;
        internal static bool Available { get; private set; }
        internal static string Detail { get; private set; } = "Not installed.";
        internal static ulong Packets { get; private set; }
        internal static void Initialize()
        {
            if (host != null) return;
            try
            {
#if BEPINEX_IL2CPP
                Il2CppTypes.Register<FrameInputPass>();
#endif
                // Configured while inactive: the volume registers with HDRP when it becomes enabled.
                host = new GameObject("TGFG frame generation inputs") { hideFlags = HideFlags.HideAndDontSave };
                host.SetActive(false);
                UnityEngine.Object.DontDestroyOnLoad(host);
                var volume = host.AddComponent<CustomPassVolume>();
                volume.isGlobal = true;
                volume.injectionPoint = CustomPassInjectionPoint.BeforePostProcess;
                volume.customPasses.Add(new FrameInputPass
                {
                    name = "TGFG frame generation inputs",
                    targetColorBuffer = CustomPass.TargetBuffer.None,
                    targetDepthBuffer = CustomPass.TargetBuffer.None
                });
                host.SetActive(true);
                Available = true;
                Detail = "Custom pass installed; no gameplay frame yet.";
            }
            catch (Exception ex)
            {
                Available = false;
                Detail = "Custom pass could not be installed: " + ex.Message;
                Session.Log.LogWarning("HDRP inputs unavailable, frame generation needs DLSS upscaling: " + ex);
                Dispose();
            }
        }
        internal static void Execute(CustomPassContext ctx)
        {
            if (!Available) return;
            try
            {
                var bridge = Session.Bridge;
                if (bridge == null || !bridge.CanSetHdrpInputs || Session.Selected.Backend == FrameGenerationSetting.Off) return;
                var hd = ctx.hdCamera;
                if (hd == null || !FrameBridge.GameplayFrame(hd.camera, out uint frameId)) return;
                if (Session.Selected.Backend != FrameGenerationSetting.Off)
                {
                    int motionFlags = (hd.frameSettings.IsEnabled(FrameSettingsField.MotionVectors) ? 1 : 0) |
                        (hd.frameSettings.IsEnabled(FrameSettingsField.ObjectMotionVectors) ? 2 : 0);
                    if (motionFlags != lastMotionFlags)
                    {
                        lastMotionFlags = motionFlags;
                        Session.Log.LogInfo("FG HDRP motion passes: camera=" + ((motionFlags & 1) != 0) +
                            ", objects=" + ((motionFlags & 2) != 0));
                    }
                }
                if (!hd.frameSettings.IsEnabled(FrameSettingsField.MotionVectors)) { Report(-10, "HDRP renders no motion vectors for the gameplay camera."); return; }
                var depth = NativeTextures.Pointer(ctx.cameraDepthBuffer);
                var motion = NativeTextures.Pointer(ctx.cameraMotionVectorsBuffer);
                if (depth == IntPtr.Zero || motion == IntPtr.Zero) { Report(-11, "The gameplay camera has no depth or motion vector buffer."); return; }
                var packet = new NativeHdrpInputs
                {
                    Size = (uint)Abi.HdrpInputsSize, Abi = Abi.Version, FrameId = frameId,
                    Flags = SystemInfo.usesReversedZBuffer ? 1u : 0u,
                    Depth = unchecked((ulong)depth.ToInt64()), Motion = unchecked((ulong)motion.ToInt64()),
                    RenderWidth = (uint)Mathf.Max(0, hd.actualWidth), RenderHeight = (uint)Mathf.Max(0, hd.actualHeight),
                    JitterX = hd.taaJitter.x, JitterY = hd.taaJitter.y
                };
                int result = bridge.SetHdrpInputs(ref packet);
                Report(result, result == 0 ? null : "The native side rejected the HDRP inputs: " + result + ".");
                if (result != 0) return;
                ++Packets;
                ctx.cmd.IssuePluginEventAndData(bridge.RenderEvent, CaptureEvent, new IntPtr((long)frameId));
            }
            catch (Exception ex)
            {
                Available = false;
                Detail = "Disabled after an error: " + ex.Message;
                Session.Log.LogError("HDRP inputs disabled: " + ex);
            }
        }
        private static void Report(int result, string problem)
        {
            if (result == lastResult) return;
            lastResult = result;
            if (problem == null)
            {
                Detail = "Supplying HDRP depth and motion vectors when DLSS isn't running.";
                Session.Log.LogInfo("Frame generation inputs: HDRP depth and motion vectors are available (used when DLSS upscaling is off).");
            }
            else
            {
                Detail = problem;
                Session.Log.LogWarning("Frame generation inputs: " + problem);
            }
        }
        internal static void Dispose()
        {
            if (host != null) UnityEngine.Object.Destroy(host);
            host = null;
        }
    }
}
