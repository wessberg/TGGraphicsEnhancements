// SPDX-License-Identifier: MIT
// TG Path Tracing's rows in the game's Video settings, after the upscaling and frame generation rows: Path
// Tracing (Off, Full), and nested under it while it is on Denoising and the tracing options. A real game
// Setting, so Apply Changes, Undo and Restore Defaults work as for the game's own rows; FoA Mod Manager lists the same rows from
// the config (Settings). Denoising offers only the denoisers this PC runs; Path Tracing only what it can.
// The Mono version only, like the mod.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using Awaken.TG.Main.Settings;
using Awaken.TG.Main.Settings.Graphics;
using Awaken.TG.Main.Settings.Options;
using Awaken.TG.MVC;
using TGGraphicsBridge;
using UnityEngine;
namespace TGPathTracing
{
    internal sealed class PathTracingSetting : Setting
    {
        internal const string Name = "Path Tracing";
        private readonly PathTracingRows rows;
        internal PathTracingSetting(PathTracingRows rows) { this.rows = rows; }
        public override string SettingName => Name;
        public override IEnumerable<PrefOption> Options => rows.Top;
        protected override bool AutoApplyOnInit => false;
        protected override void OnApply() => rows.Apply();
    }
    internal sealed class PathTracingRows
    {
        private const string IdPrefix = "TGPT.Video.";
        private readonly EnumArrowsOption mode, denoiser, view;
        private readonly ToggleOption[] modeChoices, denoiserChoices;
        private readonly SliderOption rays, bounces, indirectColor, spatial, distance, radius, sky, reflectionBoost;
        private readonly ToggleOption reflections;
        // Restore Defaults picks the best denoiser this PC runs, which is known only once the GPU has reported.
        private static readonly FieldInfo DefaultValue = typeof(EnumArrowsOption).GetField("_defaultValue", BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.Instance);
        internal PrefOption[] Top { get; }

        internal PathTracingRows()
        {
            var s = Session.Settings;
            mode = VideoTab.Enum(IdPrefix, "Mode", "Path Tracing", (int)TracingMode.Full, out modeChoices, Settings.ModeNames);
            VideoTab.Tooltip(mode, ModeTooltip);
            denoiser = VideoTab.Enum(IdPrefix, "Denoising", "Denoising", (int)DenoiserChoice.BuiltIn, out denoiserChoices, Settings.DenoiserNames);
            VideoTab.Tooltip(denoiser, DenoiserTooltip);
            rays = Slider("RaysPerPixel", "Rays Per Pixel", 1f, 8f, true, "{0:0}", 1f, s.RaysPerPixel);
            bounces = Slider("Bounces", "Bounces", 1f, 4f, true, "{0:0}", 2f, s.Bounces);
            indirectColor = Slider("IndirectColor", "Indirect Color", 0f, 4f, false, "{0:0.00}", 2f, s.IndirectColor);
            reflections = new ToggleOption(IdPrefix + "Reflections", "Reflections", true, false);
            VideoTab.Tooltip(reflections, () => s.Reflections.Description.Description);
            spatial = Slider("SpatialSamples", "Spatial Samples", 0f, 8f, true, "{0:0}", 4f, s.SpatialSamples);
            distance = Slider("RayDistance", "Ray Distance", 20f, 3000f, true, "{0:0} m", 500f, s.RayDistance);
            radius = Slider("SceneRadius", "Scene Radius", 30f, 2000f, true, "{0:0} m", 250f, s.SceneRadius);
            sky = Slider("SkyIntensity", "Sky Intensity", 0f, 4f, false, "{0:0.00}", 1f, s.SkyIntensity);
            reflectionBoost = Slider("ReflectionBoost", "Reflection Boost", 0f, 1f, false, "{0:0.00}", 0.15f, s.ReflectionBoost);
            view = VideoTab.Enum(IdPrefix, "DebugView", "Debug View", 0, out _, Settings.ViewNames);
            VideoTab.Tooltip(view, () => s.View.Description.Description);
            // In the same order as FoA Mod Manager lists them (Settings).
            var options = new PrefOption[] { denoiser, rays, bounces, indirectColor, reflections, spatial, distance, radius, sky, reflectionBoost, view };
            var rows = SwitchRowsOption.Create(mode, i => i == (int)TracingMode.Full ? options : new PrefOption[0], options);
            Top = rows != null ? new PrefOption[] { rows } : new PrefOption[] { mode }.Concat(options).ToArray(); // older game builds: every row, always
            // The game's rows path tracing replaces grey out as soon as it is chosen, before Apply.
            mode.onChange += (Action<ToggleOption>)(ignored => ReplacedRows.Refresh());
            RefreshChoices();
            SyncFromConfig();
        }
        // The Path Tracing row as it is set now, applied or not.
        internal TracingMode PendingMode => (TracingMode)Mathf.Clamp(mode.OptionInt, 0, Settings.ModeNames.Length - 1);

        private SliderOption Slider(string id, string name, float min, float max, bool whole, string format, float defaultValue, BepInEx.Configuration.ConfigEntryBase entry)
        {
            float? step = null;
            var slider = new SliderOption(IdPrefix + id, name, min, max, whole, format, defaultValue, false, step);
            VideoTab.Tooltip(slider, () => entry.Description.Description);
            return slider;
        }

        // What this PC offers: the denoisers it runs, and path tracing only where it can run at all.
        internal void RefreshChoices()
        {
            var offered = Session.OfferedDenoisers;
            var best = Session.BestDenoiser;
            VideoTab.Forbid(denoiser, offered == null ? Enumerable.Empty<ToggleOption>()
                : denoiserChoices.Where((choice, i) => !offered.Contains((DenoiserChoice)i)));
            DefaultValue?.SetValue(denoiser, denoiserChoices[(int)best]);
            if (offered != null && !offered.Contains((DenoiserChoice)Mathf.Clamp(denoiser.OptionInt, 0, denoiserChoices.Length - 1)))
                denoiser.OptionInt = (int)best;
            bool unsupported = Session.HaveStatus && Session.Status.DeviceReady != 0 && Session.Status.Supported == 0;
            VideoTab.Forbid(mode, unsupported ? new[] { modeChoices[(int)TracingMode.Full] } : new ToggleOption[0]);
            if (unsupported) mode.OptionInt = (int)TracingMode.Off;
        }
        // Config to rows; applying stores the in-game values, so the game reports no unsaved changes.
        internal void SyncFromConfig()
        {
            var s = Session.Settings;
            mode.OptionInt = (int)s.CurrentMode;
            denoiser.OptionInt = (int)s.CurrentDenoiser;
            rays.Value = s.RaysPerPixel.Value; bounces.Value = s.Bounces.Value; indirectColor.Value = s.IndirectColor.Value;
            reflections.Enabled = s.Reflections.Value;
            spatial.Value = s.SpatialSamples.Value;
            distance.Value = s.RayDistance.Value; radius.Value = s.SceneRadius.Value; sky.Value = s.SkyIntensity.Value; reflectionBoost.Value = s.ReflectionBoost.Value;
            view.OptionInt = (int)s.CurrentView;
            RefreshChoices();
            foreach (var option in new PrefOption[] { mode, denoiser, rays, bounces, indirectColor, reflections, spatial, distance, radius, sky, reflectionBoost, view }) option.Apply();
        }
        internal void Apply()
        {
            Session.Settings.Store(PendingMode, (DenoiserChoice)Mathf.Clamp(denoiser.OptionInt, 0, denoiserChoices.Length - 1),
                Mathf.RoundToInt(rays.Value), Mathf.RoundToInt(bounces.Value), indirectColor.Value, reflections.Enabled,
                Mathf.RoundToInt(spatial.Value), distance.Value, radius.Value, sky.Value, reflectionBoost.Value,
                (DebugView)Mathf.Clamp(view.OptionInt, 0, Settings.ViewNames.Length - 1));
        }

        private static string ModeTooltip()
        {
            string text = Session.Settings.Mode.Description.Description;
            if (Session.HaveStatus && Session.Status.DeviceReady != 0 && Session.Status.Supported == 0) text = "Unavailable: " + Session.Tracer.UnsupportedReason() + " " + text;
            return text;
        }
        private string DenoiserTooltip()
        {
            // What stops the selected denoiser comes first: the game's tooltip is a narrow column.
            var selected = (DenoiserChoice)Mathf.Clamp(denoiser.OptionInt, 0, denoiserChoices.Length - 1);
            string text = Settings.DenoiserDescription;
            if (selected == DenoiserChoice.RayReconstruction && !DlssSelected())
                text = "Select DLSS as the upscaler for Ray Reconstruction; until then the built-in denoiser serves. " + text;
            if (RayReconstruction.Reported == 0) text += " Ray Reconstruction does not run on this GPU and driver (TG Better Upscaling's native log says why).";
            return text;
        }
        // DLSS in the game's upscaling row, also before Apply (TG Better Upscaling's DLSS is the game's). Found by class
        // name among the settings the game created, so this never runs UpScaling's static constructor early.
        private static bool DlssSelected()
        {
            try
            {
                var master = World.Any<SettingsMaster>();
                var list = master != null ? VideoTab.GraphicSettings(master) : null;
                var upscaling = list?.OfType<ISetting>().FirstOrDefault(s => VideoTab.TypeName(s) == "UpScaling") as UpScaling;
                return upscaling == null || upscaling.IsDLSSEnabled;
            }
            catch { return true; }
        }
    }
    internal static class PathTracingRow
    {
        private static PathTracingSetting setting;
        private static PathTracingRows rows;
        // The Path Tracing row's choice before Apply, or the applied mode while the rows don't exist.
        internal static TracingMode PendingMode => rows?.PendingMode ?? Session.Settings.CurrentMode;
        private static SettingsMaster owner;
        private static bool failed, subscribed, syncPending;
        private static float nextPoll;
        private static string offered = "";
        internal static void Tick(float now)
        {
            if (failed || now < nextPoll) return;
            nextPoll = now + 0.5f;
            try
            {
                if (!subscribed)
                {
                    // FoA Mod Manager, the config file or a hotkey changed a value: the rows follow once the settings
                    // screen is closed, never under the player's hands.
                    subscribed = true;
                    Session.Settings.Changed += () => syncPending = true;
                }
                var master = World.Any<SettingsMaster>();
                if (master == null) return;
                var list = VideoTab.GraphicSettings(master);
                if (list == null) { failed = true; Session.Log.LogWarning("Game settings API not found; TG Path Tracing's options stay in FoA Mod Manager and the config."); return; }
                bool open = VideoTab.IsSettingsOpen;
                if (setting != null && VideoTab.Same(master, owner) && VideoTab.IndexOf(list, setting) >= 0)
                {
                    if (open) return;
                    var current = Session.OfferedDenoisers;
                    string key = current == null ? "" : string.Join(",", current.Select(c => c.ToString()).ToArray()) + (Session.HaveStatus ? Session.Status.Supported.ToString() : "");
                    if (key != offered) { offered = key; syncPending = true; }
                    if (syncPending) { syncPending = false; rows.SyncFromConfig(); }
                    VideoTab.Reposition(list, setting, "UpScaling", "FrameGenerationVideoSetting");
                    return;
                }
                if (open) return; // never change the list while the screen is open
                rows = new PathTracingRows();
                setting = new PathTracingSetting(rows);
                owner = master;
                syncPending = false;
                VideoTab.AddGraphicSetting(master, setting);
                VideoTab.Reposition(list, setting, "UpScaling", "FrameGenerationVideoSetting");
                Session.Log.LogInfo("Path Tracing added to the Video settings (" + Session.Settings.Mode.Value + ", denoising " + Session.Settings.Denoiser.Value + ").");
            }
            catch (Exception ex)
            {
                failed = true;
                Session.Log.LogWarning("TG Path Tracing's Video settings rows could not be added; its options stay in FoA Mod Manager and the config: " + ex);
            }
        }
    }
}
