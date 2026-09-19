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
        private static string lastReport="";
        internal static void Start(ConfigFile config, ManualLogSource log)
        {
            if(!stopped) return;
            Abi.Validate(); Log=log; Settings=new Settings(config); Bridge=new NativeBridge();
            FrameBridge.Initialize(); stopped=false; nextPoll=nextDiscovery=0;
            // A monotonic per-session seed avoids collision after a managed reload in the same process.
            sequence=(ulong)DateTime.UtcNow.Ticks;
            width=Screen.width; height=Screen.height;
            Log.LogInfo("TGFG experimental: native Video settings adapter loaded; rendering is NOT verified. " +
                "Graphics API: " + SystemInfo.graphicsDeviceType + ". Frame generation " + Settings.FrameGeneration.Value +
                " (" + Settings.VideoFilePath + ").");
        }
        internal static void Tick()
        {
            if(stopped) return;
            try
            {
                float now=Time.unscaledTime;
                NativeVideoSettings.Tick(now); // catches its own failures
                if(Settings.EmergencyKey.Value!=KeyCode.None && Input.GetKeyDown(Settings.EmergencyKey.Value))
                    Settings.SetFrameGenerationOff();
                if(Settings.DumpNow.Value) { Settings.DumpNow.Value=false; Diagnostics.Write(); }
                if(now<nextPoll) return;
                nextPoll=now+0.1f;
                if(!Bridge.Connected && now>=nextDiscovery) { nextDiscovery=now+5f; Bridge.Discover(); }
                Suspension=GetSuspension(now);
                var request=MakeRequest(Suspension);
                if(!Same(request,last)) { ++sequence; request.Sequence=sequence; last=request; }
                else request.Sequence=sequence;
                NativeStatus result;
                HaveStatus=Bridge.Exchange(ref request,out result);
                if(HaveStatus) Status=result;
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
            return new NativeRequest {
                Size=(uint)Abi.RequestSize, Abi=Abi.Version, Sequence=sequence,
                Mode=(uint)mode,
                FixedMultiplier=(uint)Mathf.Clamp(mode==GenerationMode.Dynamic ? Settings.MaxMultiplier.Value : Settings.Multiplier.Value,2,6),
                DynamicTargetFps=Mathf.Clamp(Settings.TargetFps.Value,0,1000), ReflexBaseFpsLimit=limit,
                SuspensionMask=(uint)suspension,
                SourceGraphicsApi=SystemInfo.graphicsDeviceType==GraphicsDeviceType.Direct3D12 ? 12u :
                    SystemInfo.graphicsDeviceType==GraphicsDeviceType.Direct3D11 ? 11u : 0u,
                ReflexMode=(uint)Mathf.Clamp((int)Settings.Reflex.Value,0,2)
            };
        }
        private static bool Same(NativeRequest a, NativeRequest b) => a.Mode==b.Mode && a.FixedMultiplier==b.FixedMultiplier &&
            a.DynamicTargetFps==b.DynamicTargetFps && a.ReflexBaseFpsLimit==b.ReflexBaseFpsLimit &&
            a.SuspensionMask==b.SuspensionMask && a.SourceGraphicsApi==b.SourceGraphicsApi && a.ReflexMode==b.ReflexMode;
        internal static void Stop()
        {
            if(stopped) return;
            stopped=true;
            try {
                var request=MakeRequest(Suspend.Stopping); request.Mode=0; request.Sequence=++sequence;
                NativeStatus ignored; Bridge.Exchange(ref request,out ignored);
            } catch(Exception ex) { Log?.LogWarning("Native stop request failed: " + ex.Message); }
            FrameBridge.Dispose();
            // Native heartbeat expiry is a secondary stop condition, not a GPU-hang recovery mechanism.
        }
    }
}
