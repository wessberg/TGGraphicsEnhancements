// SPDX-License-Identifier: MIT
using System;
using BepInEx.Configuration;
using BepInEx.Logging;
using TGGraphicsBridge;
using UnityEngine;
using UnityEngine.Rendering.HighDefinition;
namespace TGPathTracing
{
    internal static class Session
    {
        internal static Settings Settings;
        internal static ManualLogSource Log;
        internal static NativeTracer Tracer;
        internal static NativeStatus Status;
        internal static bool HaveStatus;
        private static bool stopped = true, reportedSupport, reportedDisabled;
        private static float nextPoll, nextDiscovery, nextReport;
        private static ulong lastFramesTraced;
        private static uint lastComposited; private static float compositedAt = -1f;
        private static int skyId; private static IntPtr skyPointer;
        private const ulong GeometryBudget = 2560ul << 20;
        private static float nextClear;

        // HDRP's frame goes to the tracer: a mode is on, the native side is bound, can trace and has not given up.
        internal static bool WantsFrames => !stopped && Settings.CurrentMode != TracingMode.Off && Tracer != null && Tracer.Connected && HaveStatus &&
            Status.Supported != 0 && Status.Disabled == 0 && !HdrpPasses.Failed;

        // The denoisers this PC runs (the Denoising row lists only these): DLSS Ray Reconstruction on an NVIDIA GPU with
        // TG Better Upscaling installed, until NGX reports it does not run here; AMD Ray Regeneration where the native
        // side found it. Null until the native side has reported the GPU.
        internal static DenoiserChoice[] OfferedDenoisers
        {
            get
            {
                if (!HaveStatus || Status.DeviceReady == 0) return null;
                bool rr = SystemInfo.graphicsDeviceVendorID == 0x10DE && Status.Supported != 0 && RayReconstruction.UpscalingInstalled && RayReconstruction.Reported != 0;
                bool rrg = (Status.Features & Tgpt.FeatureRayRegeneration) != 0;
                return Settings.Offered(rr, rrg);
            }
        }
        // What Denoising starts on, and follows until the player picks another (Settings.Best).
        internal static DenoiserChoice BestDenoiser => Settings.Best(OfferedDenoisers);

        // DLSS Ray Reconstruction should denoise (TG Better Upscaling reads it through RayReconstruction.Wanted): it is the
        // Denoising setting and NGX has not reported that it doesn't run here. RenoDX's DLSS 5 works alongside it.
        internal static bool WantsRayReconstruction => WantsFrames && RayReconstruction.Reported != 0 &&
            Settings.CurrentDenoiser == DenoiserChoice.RayReconstruction;

        // AMD FSR Ray Regeneration denoises natively where the GPU runs it and it is the Denoising setting.
        internal static bool WantsRayRegeneration => WantsFrames && (Status.Features & Tgpt.FeatureRayRegeneration) != 0 &&
            Settings.CurrentDenoiser == DenoiserChoice.RayRegeneration;

        internal static void Start(ConfigFile config, ManualLogSource log)
        {
            if (!stopped) return;
            NativeTracer.Validate();
            Log = log; config.SaveOnConfigSet = false; Settings = new Settings(config); Settings.Save(); Tracer = new NativeTracer();
            SafeConfig.Take(config, PathTracingPlugin.Name);
            stopped = false; nextPoll = nextDiscovery = 0;
            HdrpPasses.Initialize();
            try { PerformanceOverlay.AddSection("Path Tracing", 20, TracingOverlay.Lines); }
            catch (Exception ex) { Log.LogWarning("No performance overlay section: " + ex.Message); }
            Log.LogInfo("TG Path Tracing " + PathTracingPlugin.Version + " loaded. Graphics API: " + SystemInfo.graphicsDeviceType + ". Mode " + Settings.Mode.Value +
                " (" + Settings.ModeKey.Value + " turns it on and off, " + Settings.ViewKey.Value + " the debug views; " + Settings.File.ConfigFilePath + ").");
        }

        internal static void Tick()
        {
            if (stopped) return;
            try
            {
                float now = Time.unscaledTime;
                Hotkeys();
                if (now >= nextPoll)
                {
                    nextPoll = now + 0.25f;
                    if (!Tracer.Connected && now >= nextDiscovery) { nextDiscovery = now + 5f; Tracer.Discover(); }
                    HaveStatus = Tracer.ReadStatus(out Status);
                    ReportSupport();
                    if (HaveStatus && Status.Composited != lastComposited) { lastComposited = Status.Composited; compositedAt = now; }
                    var offered = OfferedDenoisers;
                    if (offered != null) Settings.RefreshDenoisers(offered, BestDenoiser);
                }
                PathTracingRow.Tick(now); // catches its own failures
                ReplacedRows.Tick(now); // the game's rows path tracing replaces are greyed out; catches its own failures
                bool tracing = WantsFrames;
                // HDRP's indirect light goes only while the traced light replaces it on screen: if composites stop,
                // HDRP's own comes back within a second instead of leaving the scene without bounced light.
                HdrpPasses.SetIndirectOverride(tracing && Settings.NoIndirectFromHdrp.Value && compositedAt >= 0f && now - compositedAt < 1f);
                if (!tracing) return;
                var camera = Camera.main;
                if (camera == null) return;
                var position = camera.transform.position;
                // Meshes stay registered while the camera travels: past a budget the traced scene starts over from
                // what is near the camera now, rather than filling video memory.
                if (HaveStatus && Status.GeometryBytes + Status.AccelerationBytes > GeometryBudget && now >= nextClear)
                {
                    nextClear = now + 30f;
                    Log.LogInfo("Traced scene over " + (GeometryBudget >> 20) + " MB of geometry; it starts over from what is near the camera.");
                    Tracer.ClearScene();
                    SceneCapture.Reset();
                    CharacterCapture.Reset();
                }
                SceneCapture.Tick(now, position);
                SceneLights.Tick(now, position, Settings.Lights.Value, HDCamera.GetOrCreate(camera).frameSettings.IsEnabled(FrameSettingsField.LightLayers));
                // HDRP's sky reflection is a CubemapArray, not a render texture; its native texture changes only
                // when HDRP recreates it (GetNativeTexturePtr waits for the render thread, so it is cached).
                var sky = HdrpPasses.SkyTexture(HDCamera.GetOrCreate(camera));
                if (sky != null && (sky.GetInstanceID() != skyId || skyPointer == IntPtr.Zero))
                {
                    skyId = sky.GetInstanceID(); skyPointer = sky.GetNativeTexturePtr();
                    Log.LogInfo("Sky for rays: HDRP's " + sky.GetType().Name + " " + sky.width + "px.");
                }
                if (sky != null && skyPointer != IntPtr.Zero) Tracer.SetSky(skyPointer);
                if (now >= nextReport)
                {
                    nextReport = now + 10f;
                    Log.LogInfo("Path tracing: " + StatusText + " | scene: " + SceneCapture.Summary + " | characters " + CharacterCapture.Summary +
                        " | lights " + SceneLights.Count + (SceneLights.Sun.Valid ? " + sun" : ", no sun") +
                        (SceneLights.PlacedLeftOut > 0 ? ", " + SceneLights.PlacedLeftOut + " placed lights left out" : "") + (SceneLights.LeftOut > 0 ? " (" + SceneLights.LeftOut + " left out: specular-only or on other light layers)" : "") + " | hook: " + HdrpPasses.LastFrame);
                }
            }
            catch (Exception ex)
            {
                Log.LogError("TG Path Tracing failed closed: " + ex);
                Stop();
            }
        }

        private static void Hotkeys()
        {
            if (Settings.ProbeKey.Value.IsDown())
            {
                try { Probe.Report(); Notices.Show("tgpt.probe", PathTracingPlugin.Name, "What is under the crosshair: written to the log", 3f); }
                catch (Exception ex) { Log.LogWarning("The probe failed: " + ex); }
            }
            else if (Settings.ViewKey.Value.IsDown())
            {
                var next = (DebugView)(((int)Settings.CurrentView + 1) % Settings.ViewNames.Length);
                Settings.CurrentView = next;
                Notices.Show("tgpt.view", PathTracingPlugin.Name, "Debug view: " + Settings.View.Value, 3f);
            }
            else if (Settings.ModeKey.Value.IsDown())
            {
                var next = (TracingMode)(((int)Settings.CurrentMode + 1) % Settings.ModeNames.Length);
                Settings.CurrentMode = next;
                string why = "";
                if (next != TracingMode.Off && HaveStatus && Status.Supported == 0) why = " (unavailable: " + Tracer.UnsupportedReason() + ")";
                else if (next != TracingMode.Off && HdrpPasses.Failed) why = " (off after an error this session: " + HdrpPasses.Detail + ")";
                Notices.Show("tgpt.mode", PathTracingPlugin.Name, "Path tracing: " + Settings.Mode.Value + why, 3f);
            }
        }

        private static void ReportSupport()
        {
            if (!HaveStatus) return;
            if (!reportedSupport && Status.DeviceReady != 0)
            {
                reportedSupport = true;
                if (Status.Supported != 0) Log.LogInfo("Path tracing is available on this GPU.");
                else Log.LogWarning("Path tracing is unavailable: " + Tracer.UnsupportedReason());
            }
            if (!reportedDisabled && Status.Disabled != 0)
            {
                reportedDisabled = true;
                Log.LogError("Path tracing turned itself off after repeated errors (last 0x" + Status.LastError.ToString("X8") + "); see BepInEx/TGPathTracing/Native/TGPathTracing.Native.log.");
            }
        }

        internal static string StatusText
        {
            get
            {
                if (!HaveStatus) return Tracer?.Detail ?? "Not initialized";
                var s = Status;
                if (s.DeviceReady == 0) return "TG Graphics Bridge has no D3D12 device yet (" + GraphicsBridge.Describe() + ")";
                if (s.Supported == 0) return "unavailable: " + Tracer.UnsupportedReason();
                ulong frames = s.FramesTraced - lastFramesTraced; lastFramesTraced = s.FramesTraced;
                string denoiser = RayReconstruction.Active ? "DLSS Ray Reconstruction" : (s.Features & Tgpt.FeatureRayRegenerationActive) != 0 ? "AMD Ray Regeneration"
                    : Settings.CurrentDenoiser == DenoiserChoice.None ? "no denoiser" : "built-in denoiser";
                // DirectX Raytracing 1.2: opacity micromaps, and Shader Execution Reordering where the GPU reorders.
                string raytracing = (s.Features & Tgpt.FeatureDxr12) == 0 ? "DirectX Raytracing 1.1"
                    : "DirectX Raytracing 1.2" + ((s.Features & Tgpt.FeatureSer) != 0 ? " with Shader Execution Reordering" : "") + ", opacity micromaps on " + s.MicromapMeshes + " meshes (" +
                      s.MicromapInstances + " instances" + (s.MicromapsWaiting != 0 ? ", " + s.MicromapsWaiting + " waiting for textures" : "") + ")";
                return Settings.Mode.Value + " " + s.RenderWidth + "x" + s.RenderHeight + " (" + denoiser + "), GPU " + s.GpuMs.ToString("0.00") + " ms, render thread " + s.FrameMs.ToString("0.00") +
                    " ms | " + raytracing +
                    " | meshes " + s.MeshesReady + " ready, " + s.MeshesQueued + " queued, " + s.MeshesFailed + " failed | " + s.InstancesTraced + " of " + s.Instances +
                    " instances traced, " + (s.Triangles / 1000) + "k triangles, " + ((s.GeometryBytes + s.AccelerationBytes) >> 20) + " MB | traced " + s.FramesTraced +
                    " (+" + frames + "), failed " + s.FramesFailed + (s.LastError != 0 ? ", last error 0x" + s.LastError.ToString("X8") : "") +
                    (compositedAt >= 0f && Time.unscaledTime - compositedAt < 1f ? " | on screen" : " | not on screen (see the native log)");
            }
        }

        internal static void Stop()
        {
            if (stopped) return;
            stopped = true;
            try { ReplacedRows.Release(); } catch { }
            try { HdrpPasses.SetIndirectOverride(false); } catch { }
            HdrpPasses.Dispose();
        }
    }
}
