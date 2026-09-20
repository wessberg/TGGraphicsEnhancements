// SPDX-License-Identifier: MIT
using System;
using BepInEx.Configuration;
using BepInEx.Logging;
using UnityEngine;
using UnityEngine.Rendering;
namespace TGFrameGeneration
{
    internal static class Session
    {
        internal static Settings Settings;
        internal static ManualLogSource Log;
        internal static NativeBridge Bridge;
        internal static NativeStatus Status;
        internal static bool HaveStatus;
        internal static Suspend Suspension;
        internal static string StatusText => (HaveStatus ? Status.Describe() : Bridge?.Detail ?? "Not initialized") +
            (Suspension!=Suspend.None ? " | suspended: " + Suspension : "");
        private static bool stopped=true;
        private static float nextPoll, nextDiscovery, settleUntil;
        private static int width, height;
        private static NativeRequest last;
        private static ulong sequence;
        private static uint lastDebugFlags=uint.MaxValue;
        private static float captureAt=-1f;
        private static string lastReport="";
        internal static void Start(ConfigFile config, ManualLogSource log)
        {
            if(!stopped) return;
            Abi.Validate(); Log=log; Settings=new Settings(config); Bridge=new NativeBridge();
            KandraMotionHistory.Initialize();
            FrameBridge.Initialize(); HdrpInputs.Initialize(); HudlessUi.Initialize(); stopped=false; nextPoll=nextDiscovery=0; captureAt=-1f;
            // A monotonic per-session seed avoids collision after a managed reload in the same process.
            sequence=(ulong)DateTime.UtcNow.Ticks;
            width=Screen.width; height=Screen.height;
            Log.LogInfo("TGFG " + Plugin.Version + " loaded. Graphics API: " + SystemInfo.graphicsDeviceType +
                ". Frame generation " + Settings.FrameGeneration + ", low latency " + Settings.LowLatency + " (" + Settings.File.ConfigFilePath + ").");
        }
        internal static void Tick()
        {
            if(stopped) return;
            try
            {
                float now=Time.unscaledTime;
                FirstPersonMotion.Tick(now);
                ModManagers.Tick(); // catches its own failures
                NativeVideoSettings.Tick(now); // catches its own failures
                if(Settings.EmergencyKey.Value!=KeyCode.None && Input.GetKeyDown(Settings.EmergencyKey.Value))
                    Settings.SetFrameGenerationOff();
                if(Settings.DumpNow.Value) { Settings.DumpNow.Value=false; Diagnostics.Write(); }
                if(Settings.FsrCaptureKey.Value!=KeyCode.None && Input.GetKeyDown(Settings.FsrCaptureKey.Value) &&
                   Settings.FrameGeneration==FrameGenerationSetting.FSR && Bridge.Connected)
                {
                    // Keep bit 32 off across presents before rearming, including repeated presses.
                    captureAt=now+0.3f;
                    uint idleFlags=(uint)Settings.FsrDebug.Value & ~32u;
                    if(Bridge.SetDebugFlags(idleFlags)==0) lastDebugFlags=idleFlags;
                    Log.LogInfo("FSR input capture requested; keep the hands visible and turn the camera.");
                }
                if(now<nextPoll) return;
                nextPoll=now+0.1f;
                if(!Bridge.Connected && now>=nextDiscovery) { nextDiscovery=now+5f; Bridge.Discover(); }
                // ConfigEntry already enforces the supported range. A second clamp here
                // turned capture flag 34 into SDK overlay flags 31 and prevented readback.
                uint debugFlags=(uint)Settings.FsrDebug.Value;
                if(captureAt>=0) debugFlags=(debugFlags & ~32u) | (now>=captureAt?32u:0u);
                if(Bridge.Connected && debugFlags!=lastDebugFlags && Bridge.SetDebugFlags(debugFlags)==0) lastDebugFlags=debugFlags;
                Suspension=GetSuspension(now);
                var request=MakeRequest(Suspension);
                if(!Same(request,last)) { ++sequence; request.Sequence=sequence; last=request; }
                else request.Sequence=sequence;
                NativeStatus result;
                HaveStatus=Bridge.Exchange(ref request,out result);
                if(HaveStatus)
                {
                    Status=result;
                    // The rows and mod-manager entries offer what the runtimes report for this GPU.
                    if(Status.CapsKnown && Settings.Capabilities==null) Settings.ApplyCapabilities(new VideoCapabilities(true,Status));
                }
                string report=(HaveStatus ? Status.Reason + "/" + Status.DlssgStatus + "/" + Status.LastSetOptionsResult : Bridge.Detail) + "/" + Suspension;
                if(report!=lastReport) { lastReport=report; Log.LogInfo(StatusText); }
            }
            catch(Exception ex)
            {
                // Do not hammer a broken graphics binding every frame.
                Log.LogError("TGFG failed closed: " + ex);
                Stop();
            }
        }
        private static Suspend GetSuspension(float now)
        {
            Suspend flags=Suspend.None;
            if(!Application.isFocused) flags|=Suspend.Unfocused;
            if(Settings.SuspendOnCursor.Value && Cursor.lockState!=CursorLockMode.Locked) flags|=Suspend.CursorUnlocked;
            if(Time.timeScale<=0) flags|=Suspend.TimePaused;
            if(NativeVideoSettings.IsSettingsOpen) flags|=Suspend.VideoMenu;
            var camera=Camera.main;
            if(Settings.RequireCamera.Value && (camera==null || !camera.enabled || !camera.gameObject.activeInHierarchy)) flags|=Suspend.NoGameCamera;
            if(Screen.width!=width || Screen.height!=height) { width=Screen.width; height=Screen.height; settleUntil=now+2f; }
            if(now<settleUntil) flags|=Suspend.ResolutionChanged;
            return flags;
        }
        private static NativeRequest MakeRequest(Suspend suspension)
        {
            float limit=Settings.BaseLimit.Value;
            if(limit>0 && limit<1) limit=1;
            var mode=Settings.RequestedMode;
            var backend=Settings.FrameGeneration;
            int multiplier=mode==GenerationMode.Dynamic ? Settings.MaxMultiplier : Settings.Multiplier;
            // Clamp to what the selected runtime reports; native clamps again, so this is only for clarity in logs.
            uint reported=HaveStatus && Status.CapsKnown ? Status.MaxGenerated(backend) : 0;
            if(reported>0) multiplier=Math.Min(multiplier,(int)reported+1);
            return new NativeRequest {
                Size=(uint)Abi.RequestSize, Abi=Abi.Version, Sequence=sequence,
                Mode=(uint)mode,
                FixedMultiplier=(uint)Mathf.Clamp(multiplier,2,Settings.MaxMultiplierBound),
                DynamicTargetFps=Mathf.Clamp(Settings.TargetFps,0,1000), BaseFpsLimit=limit,
                SuspensionMask=(uint)suspension,
                SourceGraphicsApi=SystemInfo.graphicsDeviceType==GraphicsDeviceType.Direct3D12 ? 12u :
                    SystemInfo.graphicsDeviceType==GraphicsDeviceType.Direct3D11 ? 11u : 0u,
                LatencyMode=(uint)Mathf.Clamp((int)Settings.LowLatency,0,4),
                Backend=mode==GenerationMode.Off ? 0u : (uint)backend
            };
        }
        private static bool Same(NativeRequest a, NativeRequest b) => a.Mode==b.Mode && a.FixedMultiplier==b.FixedMultiplier &&
            a.DynamicTargetFps==b.DynamicTargetFps && a.BaseFpsLimit==b.BaseFpsLimit &&
            a.SuspensionMask==b.SuspensionMask && a.SourceGraphicsApi==b.SourceGraphicsApi && a.LatencyMode==b.LatencyMode && a.Backend==b.Backend;
        internal static void Stop()
        {
            if(stopped) return;
            stopped=true;
            try {
                var request=MakeRequest(Suspend.Stopping); request.Mode=0; request.Sequence=++sequence;
                NativeStatus ignored; Bridge.Exchange(ref request,out ignored);
            } catch(Exception ex) { Log?.LogWarning("Native stop request failed: " + ex.Message); }
            FirstPersonMotion.Restore();
            KandraMotionHistory.Dispose();
#if BEPINEX_IL2CPP
            Il2CppOptionConditions.Dispose();
#endif
            FrameBridge.Dispose(); HudlessUi.Dispose(); HdrpInputs.Dispose();
            // Native heartbeat expiry is a secondary stop condition, not a GPU-hang recovery mechanism.
        }
    }
}
