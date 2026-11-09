// SPDX-License-Identifier: MIT
using System;
using System.Linq;
using BepInEx.Configuration;
using BepInEx.Logging;
using TGGraphicsBridge;
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
        internal static float StatusTime; // Time.unscaledTime when Status was read, every 0.1 s
        internal static Suspend Suspension;
        internal static string StatusText => (HaveStatus ? Status.Describe() : Bridge?.Detail ?? "Not initialized") +
            (Suspension!=Suspend.None ? " | suspended: " + Suspension : "");
        // A troubleshooting step's choice (FrameGenerationSweep); null the rest of the time.
        internal static Selection? Override;
        // What frame generation runs: the troubleshooting step's choice, or the player's settings.
        internal static Selection Selected => Override ?? Selection.From(Settings);
        private static bool checkFailed;
        private static bool stopped=true;
        private static float nextPoll, nextDiscovery, settleUntil, cursorUnlockedAt=-10f;
        private static int width, height;
        private static NativeRequest last;
        private static ulong sequence;
        private static uint lastDebugFlags=uint.MaxValue, lastDlssPreset=uint.MaxValue;
        private static float captureAt=-1f;
        private static string lastReport="";
        internal static void Start(ConfigFile config, ManualLogSource log)
        {
            if(!stopped) return;
            // Bound with BepInEx's saving off, then saved once through SafeConfig (Settings.Save): a config file
            // that can't be written must not stop the mod here, before it has logged anything.
            Abi.Validate(); Log=log; config.SaveOnConfigSet=false; Settings=new Settings(config); Settings.Save(); Bridge=new NativeBridge();
            Settings.Changed+=DisplayMode.Arm; // every path that changes the Video selection settles through this
            Settings.Changed+=HybridVsync.Arm;
            // The advanced rows do not, so this one is watched on its own.
            Settings.BorderlessForFg.SettingChanged+=BorderlessSettingChanged;
            FrameBridge.Initialize(); HdrpInputs.Initialize(); HudlessUi.Initialize(); stopped=false; nextPoll=nextDiscovery=0; captureAt=-1f;
            // The performance overlay's section; a bridge from before the overlay has no such call.
            try { PerformanceOverlay.AddSection("Frame generation", 20, FrameGenerationOverlay.Lines); }
            catch(Exception ex) { Log.LogWarning("No performance overlay section: " + ex.Message); }
            // The troubleshooting report's steps, measurements and section; a bridge from before it has none.
            try { FrameGenerationSweep.Register(); }
            catch(Exception ex) { Log.LogWarning("No troubleshooting report part: " + ex.Message); }
            // A monotonic per-session seed avoids collision after a managed reload in the same process.
            sequence=(ulong)DateTime.UtcNow.Ticks;
            width=Screen.width; height=Screen.height;
            Log.LogInfo("TGFG " + Plugin.Version + " loaded. Graphics API: " + SystemInfo.graphicsDeviceType +
                ". Frame generation " + Settings.FrameGeneration + ", low latency " + Settings.LowLatency + " (" + Settings.File.ConfigFilePath + ").");
            // Started: the bridge otherwise reports that it loaded and didn't start, and stands in for its row.
            try { ClaimRow(); } catch { }
        }
        private static void ClaimRow() => RowAlerts.Claim(AlertRow.FrameGeneration);
        private static void BorderlessSettingChanged(object sender,EventArgs args) { DisplayMode.Arm(); }
        // Why the native side reported nothing: the first step that is missing, from the installation to
        // the game's swapchain, as sentences with what to do (Explain). An explanation that fails to build
        // must never take the plugin down: an older bridge lacks what it reads.
        internal static string UnavailableReason()
        {
            try { return Explain.Unavailable(); }
            catch(Exception ex) { return "Its native part hasn't reported what this GPU supports (" + ex.GetType().Name + ")."; }
        }
        // The same in a few words, for the performance overlay ("installation problem: wrong folder").
        internal static string UnavailableShort()
        {
            try { return ShortReason(); }
            catch { return "waiting for the GPU's report"; }
        }
        private static string ShortReason() => Explain.UnavailableWhy().Short;
        // The Frame Generation row's tooltip: the reason and the one fix, short enough for the game's tooltip.
        internal static string UnavailableTip()
        {
            try { return Tip(); }
            catch { return "Frame generation can't run in this session. BepInEx/LogOutput.log says why."; }
        }
        private static string Tip()
        {
            var why = Explain.UnavailableWhy();
            return "Frame generation can't run: " + why.Short + "." + (why.Fix.Length > 0 ? " " + why.Fix : "");
        }
        // Frame generation has to stop now: a troubleshooting step's choice would keep it running, so the
        // test ends with it.
        internal static void EndTest(string why)
        {
            if(!Override.HasValue) return;
            Override=null;
            try { AbortTest(why); } catch { } // a bridge from before the report
        }
        private static void AbortTest(string why) => Troubleshooting.Abort(why);
        internal static void Tick()
        {
            if(stopped) return;
            try
            {
                float now=Time.unscaledTime;
                // TG Graphics Bridge corrects the game's motion vectors while frame generation reads them.
                var selected=Selected;
                MotionVectors.Request(Plugin.Name, selected.Backend != FrameGenerationSetting.Off);
                NativeVideoSettings.Tick(now); // catches its own failures
                if(Settings.EmergencyKey.Value!=KeyCode.None && Input.GetKeyDown(Settings.EmergencyKey.Value))
                {
                    EndTest("stopped by the emergency off key");
                    Settings.SetFrameGenerationOff();
                }
                if(Settings.DumpNow.Value) { Settings.DumpNow.Value=false; Diagnostics.Write(); }
                if(Settings.FsrCaptureKey.Value!=KeyCode.None && Input.GetKeyDown(Settings.FsrCaptureKey.Value) &&
                   selected.Backend==FrameGenerationSetting.FSR && Bridge.Connected)
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
                uint dlssPreset=Settings.DlssPreset;
                if(Bridge.Connected && dlssPreset!=lastDlssPreset && Bridge.SetDlssPreset(dlssPreset)==0) lastDlssPreset=dlssPreset;
                DisplayMode.Tick();
                HybridVsync.Tick(now);
                ExtraFramerateLimit.Tick();
                Suspension=GetSuspension(now);
                var request=MakeRequest(Suspension);
                if(!Same(request,last)) { ++sequence; request.Sequence=sequence; last=request; }
                else request.Sequence=sequence;
                NativeStatus result;
                HaveStatus=Bridge.Exchange(ref request,out result);
                if(HaveStatus)
                {
                    Status=result; StatusTime=now;
                    // The rows and mod-manager entries offer what the runtimes report for this GPU.
                    if(Status.CapsKnown && Settings.Capabilities==null) Settings.ApplyCapabilities(new VideoCapabilities(true,Status));
                    FrameRates.Update(Status,now,Selected.Backend,Suspension!=Suspend.None);
                }
                // Whether turning frame generation on raised the framerate; stops on its own failure.
                if(!checkFailed)
                {
                    try { FrameRateCheck.Tick(now); }
                    catch(Exception ex) { checkFailed=true; Log.LogWarning("The frame generation framerate check stopped: " + ex); }
                }
                if(now>=nextAvailability) { nextAvailability=now+2f; try { NoteAvailability(now); } catch { } }
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
        private static float nextAvailability;
        // Frame generation that can't run in this session is a notice, so its row reads Off with the bridge's
        // alert icon and the reason. The GPU's report gets 20 s after startup; an install problem the install
        // check found is its own notice already.
        private static void NoteAvailability(float now)
        {
            bool installProblem=Notices.For(AlertRow.FrameGeneration).Any(n => n.Source==Plugin.Name && n.Id.StartsWith("install.",StringComparison.Ordinal));
            if((HaveStatus && Status.CapsKnown) || now<20f || installProblem) { Notices.Clear("fg.unavailable"); return; }
            var why=Explain.UnavailableWhy();
            Notices.Set("fg.unavailable",NoticeLevel.Problem,Plugin.Name,"Unavailable",
                "Frame generation can't run: "+why.Short+"."+(why.Fix.Length>0 ? " "+why.Fix : ""),why.Full);
        }
        // A display-mode change tears the swapchain down and builds it again; hold frame generation
        // over it, the same as a resolution change, which the width/height check cannot see because
        // the mode changes while both stay the same.
        internal static void NoteDisplayChange() { settleUntil=Time.unscaledTime+2f; }
        private static Suspend GetSuspension(float now)
        {
            Suspend flags=Suspend.None;
            if(!Application.isFocused) flags|=Suspend.Unfocused;
            // Mods and the game can fight over the cursor while a menu is open (FoA Mod Manager, Tainted Interface and
            // others set it each frame): frame generation resumes only once the cursor has stayed locked for half a
            // second, not for the odd frame the game re-locks it, which switched it on and off with hitches.
            bool unlocked=Cursor.lockState!=CursorLockMode.Locked;
            if(unlocked) cursorUnlockedAt=now;
            if(Settings.SuspendOnCursor.Value && (unlocked || now-cursorUnlockedAt<0.5f)) flags|=Suspend.CursorUnlocked;
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
            var selected=Selected;
            var mode=selected.Mode;
            var backend=selected.Backend;
            int multiplier=mode==GenerationMode.Dynamic ? selected.MaxMultiplier : selected.Multiplier;
            // Clamp to what the selected runtime reports; native clamps again, so this is only for clarity in logs.
            uint reported=HaveStatus && Status.CapsKnown ? Status.MaxGenerated(backend) : 0;
            if(reported>0) multiplier=Math.Min(multiplier,(int)reported+1);
            return new NativeRequest {
                Size=(uint)Abi.RequestSize, Abi=Abi.Version, Sequence=sequence,
                Mode=(uint)mode,
                FixedMultiplier=(uint)Mathf.Clamp(multiplier,2,Settings.MaxMultiplierBound),
                DynamicTargetFps=Mathf.Clamp(selected.TargetFps,0,1000), BaseFpsLimit=limit,
                SuspensionMask=(uint)suspension,
                SourceGraphicsApi=SystemInfo.graphicsDeviceType==GraphicsDeviceType.Direct3D12 ? 12u :
                    SystemInfo.graphicsDeviceType==GraphicsDeviceType.Direct3D11 ? 11u : 0u,
                LatencyMode=(uint)Mathf.Clamp((int)selected.Latency,0,4),
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
            DisplayMode.Release(); // never leave the game's display choices restricted behind us
            HybridVsync.Release();
            MotionVectors.Request(Plugin.Name, false);
            FrameBridge.Dispose(); HudlessUi.Dispose(); HdrpInputs.Dispose();
            // Native heartbeat expiry is a secondary stop condition, not a GPU-hang recovery mechanism.
        }
    }
}
