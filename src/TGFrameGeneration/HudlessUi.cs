// SPDX-License-Identifier: MIT
// AfterPostProcess forces HDRP to use an intermediate color RT, before overlay UI composition.
using System;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.HighDefinition;
#if BEPINEX_IL2CPP
using Il2CppInterop.Runtime.Injection;
#endif
namespace TGFrameGeneration
{
    internal sealed class HudlessPass : CustomPass
    {
#if BEPINEX_IL2CPP
        public HudlessPass(IntPtr pointer) : base(pointer) { }
        internal HudlessPass() : base(ClassInjector.DerivedConstructorPointer<HudlessPass>())
        {
            ClassInjector.DerivedConstructorBody(this);
            Il2CppTypes.Construct(this, typeof(CustomPass), "NativeMethodInfoPtr__ctor_Protected_Void_0");
        }
        private new void Execute(CustomPassContext ctx) => HudlessUi.Execute(ctx);
#else
        protected override void Execute(CustomPassContext ctx) => HudlessUi.Execute(ctx);
#endif
    }
    internal static class HudlessUi
    {
        private static GameObject host;
        private static bool active, failed;
        private static string lastLayout;
        internal static string Detail { get; private set; } = "Not installed.";
        internal static void Initialize()
        {
            if (host != null) return;
            try
            {
#if BEPINEX_IL2CPP
                Il2CppTypes.Register<HudlessPass>();
#endif
                host = new GameObject("TGFG HUD-less color") { hideFlags = HideFlags.HideAndDontSave };
                host.SetActive(false);
                UnityEngine.Object.DontDestroyOnLoad(host);
                var volume = host.AddComponent<CustomPassVolume>();
                volume.isGlobal = true;
                volume.injectionPoint = CustomPassInjectionPoint.AfterPostProcess;
                volume.customPasses.Add(new HudlessPass
                {
                    name = "TGFG HUD-less color",
                    targetColorBuffer = CustomPass.TargetBuffer.None,
                    targetDepthBuffer = CustomPass.TargetBuffer.None
                });
                failed = false;
                Detail = "HDRP color pass installed; awaiting gameplay.";
            }
            catch (Exception ex) { Fail(ex); }
        }
        internal static void BeginGameplayCamera()
        {
            bool want = !failed && host != null && Session.Bridge != null && Session.Bridge.CanSetHudless &&
                Session.Settings.HudlessCapture.Value && Session.Settings.FrameGeneration != FrameGenerationSetting.Off;
            if (want == active) return;
            active = want;
            host?.SetActive(want);
            Session.Bridge?.SetHudlessCapture(want);
            Detail = want ? "Capturing HDRP color before overlay UI; native verification pending." : "Off.";
        }
        internal static void Execute(CustomPassContext ctx)
        {
            if (!active) return;
            try
            {
                var hd = ctx.hdCamera;
                if (hd == null || !FrameBridge.GameplayFrame(hd.camera, out uint frameId)) return;
                var rt = ctx.cameraColorBuffer?.rt;
                if (rt == null) { Detail = "No HDRP intermediate color texture."; return; }
                uint flags = 1; // accepted full-screen gameplay camera: HDRP final blit flips Y
                var hdr = HDROutputSettings.main;
                bool hdrActive = hdr != null && hdr.active;
                if (hdrActive)
                {
                    var gamut = hdr.displayColorGamut;
                    if (gamut == ColorGamut.Rec709 || gamut == ColorGamut.sRGB) flags |= 2;
                    else if (gamut == ColorGamut.Rec2020 || gamut == ColorGamut.HDR10) flags |= 2 | 4;
                    else { Detail = "Unsupported HDR color gamut: " + gamut; return; }
                }
                if (rt.sRGB) flags |= 8;
                var pointer = HdrpInputs.Pointer(ctx.cameraColorBuffer);
                if (pointer == IntPtr.Zero) return;
                var packet = new NativeHudless
                {
                    Size = (uint)Abi.HudlessSize, Abi = Abi.Version, FrameId = frameId, Flags = flags,
                    Color = unchecked((ulong)pointer.ToInt64()),
                    Width = (uint)Mathf.Max(0, hd.camera.pixelWidth), Height = (uint)Mathf.Max(0, hd.camera.pixelHeight)
                };
                int result = Session.Bridge.SetHudless(ref packet);
                if (result != 0) { Detail = "HDRP color packet rejected: " + result; return; }
                // Copy before the graph releases or reuses postProcessDest.
                ctx.cmd.IssuePluginEventAndData(Session.Bridge.RenderEvent, 4, new IntPtr((long)frameId));
                string layout = packet.Width + "x" + packet.Height + " in " + rt.width + "x" + rt.height +
                    " " + rt.graphicsFormat + ", Unity HDR=" + hdrActive + ", flags=" + flags;
                if (layout != lastLayout)
                {
                    lastLayout = layout;
                    Session.Log.LogInfo("HUD-less HDRP color: " + layout);
                }
                Detail = "HDRP AfterPostProcess color submitted; see native log for verification.";
            }
            catch (Exception ex) { Fail(ex); }
        }
        private static void Fail(Exception ex)
        {
            failed = true;
            Reset();
            Detail = "HDRP color capture disabled: " + ex.Message;
            Session.Log?.LogError(Detail + "\n" + ex);
        }
        internal static void Reset()
        {
            active = false;
            host?.SetActive(false);
            Session.Bridge?.SetHudlessCapture(false);
        }
        internal static void Dispose()
        {
            Reset();
            if (host != null) UnityEngine.Object.Destroy(host);
            host = null;
            lastLayout = null;
        }
    }
}
