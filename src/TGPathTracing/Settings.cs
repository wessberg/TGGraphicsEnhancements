// SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using BepInEx.Configuration;
using TGGraphicsBridge;
using UnityEngine;
namespace TGPathTracing
{
    public enum TracingMode { Off, Full }
    public enum DenoiserChoice { None, BuiltIn, RayRegeneration, RayReconstruction }
    // The Path Tracing section: exactly the rows of the Video settings (PathTracingRow), which FoA Mod Manager
    // lists with the same names, choices and order. The hotkeys, the scene switches and whether Denoising holds
    // the player's own pick are config-only.
    internal sealed class Settings
    {
        private const string Section = "Path Tracing", Advanced = "Advanced";
        internal readonly ConfigFile File;
        internal readonly ConfigEntry<string> Mode, View;
        internal ConfigEntry<string> Denoiser { get; private set; }
        internal readonly ConfigEntry<bool> DenoisingChosen;
        internal readonly ConfigEntry<float> IndirectColor, SkyIntensity, ReflectionBoost, RayDistance, SceneRadius;
        internal readonly ConfigEntry<int> Bounces, RaysPerPixel, SpatialSamples;
        internal readonly ConfigEntry<bool> Reflections, SpatialReuse, TemporalReuse;
        internal readonly ConfigEntry<KeyboardShortcut> ModeKey, ViewKey, ProbeKey;
        internal readonly ConfigEntry<bool> SceneObjects, DrakeWorld, Terrain, Vegetation, Lights, Characters, NoIndirectFromHdrp;
        internal static readonly string[] ModeNames = { "Off", "Full" };
        internal static readonly string[] ViewNames = { "None", "Albedo", "Normals", "Coverage", "Materials", "Indirect", "Indirect raw", "Direct" };
        // Indexed by DenoiserChoice; the row lists only those this PC runs.
        internal static readonly string[] DenoiserNames = { "None", "Built-in", "Ray Regeneration", "Ray Reconstruction" };
        internal const string DenoiserDescription =
            "Ray Reconstruction: NVIDIA's DLSS Ray Reconstruction (an RTX GPU, with TG Better Upscaling), which denoises and upscales in " +
            "one step while DLSS is the Upscaling setting; with another upscaler the built-in denoiser serves. Ray Regeneration: " +
            "AMD's FSR Ray Regeneration (a Radeon RX 9000). Built-in: temporal " +
            "and edge-aware spatial filtering. None: the traced light as it comes. Starts on the best one this PC runs.";
        // A value changed outside the Video tab (FoA Mod Manager, the config file, a hotkey): the rows follow.
        internal event Action Changed;
        private int editing;
        private static readonly Dictionary<string, ConfigurationManagerAttributes> rows = new Dictionary<string, ConfigurationManagerAttributes>();

        internal Settings(ConfigFile file)
        {
            File = file;
            int order = 0;
            // Earlier versions also offered Global Illumination (HDRP's direct light with the traced bounced light added):
            // a config set to it becomes Full. Read before binding, which replaces a value the entry doesn't accept with Off.
            bool wasGlobalIllumination = StoredValue(file, "Mode") == "Global Illumination";
            Mode = file.Bind(Section, "Mode", ModeNames[(int)TracingMode.Full], Row(order++, "Path Tracing", new AcceptableValueList<string>(ModeNames),
                "Full: the path tracer lights deferred surfaces with the sun and every light (ReSTIR DI, traced shadows), adds the " +
                "light that bounces off the scene (ReSTIR GI, two bounces by default) in place of HDRP's baked indirect light, and " +
                "traces reflections. Needs an RTX 20, RX 6000, Arc or newer GPU."));
            if (wasGlobalIllumination) Mode.Value = ModeNames[(int)TracingMode.Full];
            // Every choice until this PC's denoisers are known (RefreshDenoisers), so a stored one is kept.
            Denoiser = file.Bind(Section, "Denoising", DenoiserNames[(int)DenoiserChoice.BuiltIn], Row(order++, "Denoising", new AcceptableValueList<string>(DenoiserNames), DenoiserDescription));
            // Until the player picks one other than the best, Denoising follows the best this PC runs (RefreshDenoisers).
            DenoisingChosen = file.Bind(Advanced, "Denoising chosen", false, Hidden(
                "Whether Denoising holds the player's own pick; otherwise it follows the best denoiser this PC runs."));
            MigrateDenoiser(file);
            RaysPerPixel = file.Bind(Section, "Rays per pixel", 1, Row(order++, "Rays Per Pixel", new AcceptableValueRange<int>(1, 8),
                "Indirect-light rays traced from every pixel each frame, each followed for the set bounces. More gives cleaner, " +
                "more detailed bounced light; the tracer's GPU time grows about in proportion."));
            Bounces = file.Bind(Section, "Bounces", 2, Row(order++, "Bounces", new AcceptableValueRange<int>(1, 4),
                "Diffuse bounces after the first: 1 lights bounced surfaces with the sun and lights only; 2 adds the sky and a second bounce."));
            RemoveIndirectIntensity(file);
            IndirectColor = file.Bind(Section, "Indirect color", 2f, Row(order++, "Indirect Color", new AcceptableValueRange<float>(0f, 4f),
                "How much color the bounced light keeps from the surfaces it bounced off, at the same brightness: 1 as traced, " +
                "0 grey, more exaggerates the tint (a red cloth reddening the floor beside it); the default 2 doubles it."));
            Reflections = file.Bind(Section, "Reflections", true, Row(order++, "Reflections", null,
                "Traced reflections in place of HDRP's reflection probes on deferred surfaces."));
            SpatialSamples = file.Bind(Section, "Spatial samples", 4, Row(order++, "Spatial Samples", new AcceptableValueRange<int>(0, 8),
                "Neighbouring pixels ReSTIR borrows samples from each frame. More is smoother and slower."));
            RemoveTemporalHistory(file);
            RayDistance = file.Bind(Section, "Ray distance", 500f, Row(order++, "Ray Distance", new AcceptableValueRange<float>(20f, 3000f),
                "How far rays travel, in metres, before they count as reaching the sky."));
            SceneRadius = file.Bind(Section, "Scene radius", 250f, Row(order++, "Scene Radius", new AcceptableValueRange<float>(30f, 2000f),
                "Objects within this many metres of the camera are in the traced scene. Larger costs memory and time."));
            SkyIntensity = file.Bind(Section, "Sky intensity", 1f, Row(order++, "Sky Intensity", new AcceptableValueRange<float>(0f, 4f),
                "Scales the light rays gather from HDRP's sky."));
            ReflectionBoost = file.Bind(Section, "Reflection boost", 0.15f, Row(order++, "Reflection Boost", new AcceptableValueRange<float>(0f, 1f),
                "Every surface glossier and more reflective than the game made it, so the traced reflections show. " +
                "0 leaves the materials as they are, the default 0.15 adds a little, 1 makes them nearly mirrors (to judge the reflections)."));
            View = file.Bind(Section, "Debug view", ViewNames[0], Row(order++, "Debug View", new AcceptableValueList<string>(ViewNames),
                "Shows what the tracer sees instead of the image. Coverage: every traced object in its own color, dark where HDRP " +
                "draws something the traced scene lacks. Materials: the traced scene's colors along camera rays."));
            ModeKey = file.Bind(Section, "Mode key", new KeyboardShortcut(KeyCode.Insert), Hidden(
                "Turns path tracing on and off. Not F9: games commonly load the quick save with it."));
            ViewKey = file.Bind(Section, "Debug view key", new KeyboardShortcut(KeyCode.Insert, KeyCode.LeftShift), Hidden("Cycles the debug views."));
            ProbeKey = file.Bind(Section, "Probe key", new KeyboardShortcut(KeyCode.F8), Hidden(
                "Writes to the log what the game has under the crosshair (objects, Drake entities, Medusa instances, the lights reaching it) and what the tracer makes of it."));
            SpatialReuse = file.Bind(Advanced, "Spatial reuse", true, Hidden("ReSTIR's spatial reuse between neighbouring pixels."));
            TemporalReuse = file.Bind(Advanced, "Temporal reuse", true, Hidden("ReSTIR's temporal reuse from the previous frame."));
            SceneObjects = file.Bind(Advanced, "Trace scene objects", true, Hidden("GameObjects with mesh renderers."));
            DrakeWorld = file.Bind(Advanced, "Trace Drake world", true, Hidden("The world Awaken's ECS renderer (Drake) draws: buildings, rocks, props."));
            Terrain = file.Bind(Advanced, "Trace terrain", true, Hidden("Unity terrains, as meshes built from their heightmaps."));
            Vegetation = file.Bind(Advanced, "Trace vegetation", true, Hidden(
                "Trees and large objects placed by Vegetation Studio (not grass or plants), with alpha-tested leaves."));
            Lights = file.Bind(Advanced, "Trace lights", true, Hidden("The scene's point and spot lights (not the game's fill lights, which stand in for bounced light)."));
            Characters = file.Bind(Advanced, "Trace characters", true, Hidden(
                "The characters near the camera, posed as the game draws them each frame: they shadow themselves and what is around them."));
            NoIndirectFromHdrp = file.Bind(Advanced, "Replace HDRP indirect light", true, Hidden(
                "While tracing, HDRP's screen-space GI and its own indirect diffuse light (probes, lightmaps) are turned off, so it " +
                "is not counted twice (the composite adds what HDRP bakes into the G-buffer's emission)."));
            file.SettingChanged += OnSettingChanged;
            RefreshVisibility();
        }
        // As in the Video tab: the options only while path tracing is on.
        internal void RefreshVisibility()
        {
            bool on = CurrentMode != TracingMode.Off;
            foreach (var pair in rows) pair.Value.Visible = pair.Key == "Path Tracing" || on;
        }

        internal TracingMode CurrentMode
        {
            get { int i = Array.IndexOf(ModeNames, Mode.Value); return i < 0 ? TracingMode.Off : (TracingMode)i; }
            set => Mode.Value = ModeNames[(int)value];
        }
        internal DebugView CurrentView
        {
            get { int i = Array.IndexOf(ViewNames, View.Value); return i < 0 ? DebugView.None : (DebugView)i; }
            set => View.Value = ViewNames[(int)value];
        }
        internal DenoiserChoice CurrentDenoiser
        {
            get { int i = Array.IndexOf(DenoiserNames, Denoiser.Value); return i < 0 ? DenoiserChoice.BuiltIn : (DenoiserChoice)i; }
        }
        internal uint Options =>
            (SpatialReuse.Value ? 0u : Tgpt.OptionNoSpatial) | (TemporalReuse.Value ? 0u : Tgpt.OptionNoTemporal) |
            (CurrentDenoiser == DenoiserChoice.None ? Tgpt.OptionNoDenoiser : 0u) | (Reflections.Value ? 0u : Tgpt.OptionNoSpecular);
        // The denoisers this PC runs, once known: the Denoising row lists only those. Until then every one is kept.
        internal static DenoiserChoice[] Offered(bool rayReconstruction, bool rayRegeneration) =>
            ((DenoiserChoice[])Enum.GetValues(typeof(DenoiserChoice)))
                .Where(c => (c != DenoiserChoice.RayReconstruction || rayReconstruction) && (c != DenoiserChoice.RayRegeneration || rayRegeneration)).ToArray();
        // What Denoising starts on and Restore Defaults picks: Ray Reconstruction, then Ray Regeneration, then the
        // built-in denoiser.
        internal static DenoiserChoice Best(DenoiserChoice[] offered)
        {
            if (offered == null) return DenoiserChoice.BuiltIn;
            if (offered.Contains(DenoiserChoice.RayReconstruction)) return DenoiserChoice.RayReconstruction;
            if (offered.Contains(DenoiserChoice.RayRegeneration)) return DenoiserChoice.RayRegeneration;
            return DenoiserChoice.BuiltIn;
        }
        internal void RefreshDenoisers(DenoiserChoice[] offered, DenoiserChoice best)
        {
            var choices = offered.Select(c => DenoiserNames[(int)c]).ToArray();
            bool same = Denoiser.Description.AcceptableValues is AcceptableValueList<string> list && list.AcceptableValues.SequenceEqual(choices);
            // The player's pick stays while this PC runs it; otherwise Denoising follows the best.
            bool keep = DenoisingChosen.Value && choices.Contains(Denoiser.Value);
            string value = keep ? Denoiser.Value : DenoiserNames[(int)best];
            if (same && value == Denoiser.Value && DenoisingChosen.Value == keep) return;
            Batch(() =>
            {
                if (!same)
                {
                    // BepInEx fixes an entry's acceptable values when it is bound: the entry is bound again.
                    var description = new ConfigDescription(Denoiser.Description.Description, new AcceptableValueList<string>(choices), Denoiser.Description.Tags);
                    File.Remove(Denoiser.Definition);
                    Denoiser = File.Bind(Denoiser.Definition, DenoiserNames[(int)DenoiserChoice.BuiltIn], description);
                }
                if (Denoiser.Value != value) Denoiser.Value = value;
                DenoisingChosen.Value = keep;
            });
            Save();
            ModManagers.RequestFoARefresh();
            Changed?.Invoke();
        }
        // Earlier versions had a Temporal History option (how many frames ReSTIR may keep reusing a sample, 20 by
        // default). Every denoiser averages over time and needs samples that change from frame to frame: measured on
        // normal-mapped surfaces, 20 left two to four times the noise of 1 (tests/pathtracing_gpu_test.cpp), so the
        // tracer always uses 1 and the old entry leaves the config.
        private static void RemoveTemporalHistory(ConfigFile file)
        {
            var legacy = file.Bind(Section, "Temporal history", 0, Hidden("No longer used."));
            file.Remove(legacy.Definition);
        }
        // Earlier versions had Indirect Intensity, which scaled the traced bounced light in the Global Illumination mode
        // they offered; Full traces every light as it is.
        private static void RemoveIndirectIntensity(ConfigFile file)
        {
            var legacy = file.Bind(Section, "Indirect intensity", 1f, Hidden("No longer used."));
            file.Remove(legacy.Definition);
        }
        // A value as the file holds it, before its entry is bound; null when the file has none (or BepInEx hides it).
        private static string StoredValue(ConfigFile file, string key)
        {
            try
            {
                var orphans = typeof(ConfigFile).GetProperty("OrphanedEntries", BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public)?
                    .GetValue(file, null) as Dictionary<ConfigDefinition, string>;
                return orphans != null && orphans.TryGetValue(new ConfigDefinition(Section, key), out var value) ? value : null;
            }
            catch { return null; }
        }
        // Earlier versions stored "Denoiser" (Auto, None, Built-in; Off before that): the pick carries over, Auto follows the best.
        private void MigrateDenoiser(ConfigFile file)
        {
            var legacy = file.Bind(Section, "Denoiser", "", Hidden("Replaced by Denoising."));
            string old = legacy.Value;
            file.Remove(legacy.Definition);
            DenoiserChoice? pick = old == "None" || old == "Off" ? DenoiserChoice.None : old == "Built-in" ? DenoiserChoice.BuiltIn
                : old == "DLSS Ray Reconstruction" ? DenoiserChoice.RayReconstruction : old == "AMD Ray Regeneration" ? DenoiserChoice.RayRegeneration
                : (DenoiserChoice?)null;
            if (old.Length == 0) return;
            if (pick.HasValue) Denoiser.Value = DenoiserNames[(int)pick.Value];
            DenoisingChosen.Value = pick.HasValue;
        }

        // The Video tab applied these values.
        internal void Store(TracingMode mode, DenoiserChoice denoiser, int rays, int bounces, float indirectColor, bool reflections, int spatial,
                            float distance, float radius, float sky, float reflectionBoost, DebugView view)
        {
            Batch(() =>
            {
                CurrentMode = mode;
                var choices = (Denoiser.Description.AcceptableValues as AcceptableValueList<string>)?.AcceptableValues;
                string name = DenoiserNames[(int)denoiser];
                if (choices == null || choices.Contains(name))
                {
                    Denoiser.Value = name;
                    DenoisingChosen.Value = denoiser != Session.BestDenoiser; // the best (Restore Defaults too) follows this PC
                }
                RaysPerPixel.Value = rays; Bounces.Value = bounces; IndirectColor.Value = indirectColor; Reflections.Value = reflections;
                SpatialSamples.Value = spatial; RayDistance.Value = distance; SceneRadius.Value = radius;
                SkyIntensity.Value = sky; ReflectionBoost.Value = reflectionBoost; CurrentView = view;
            });
            RefreshVisibility();
            Save();
            ModManagers.RequestFoARefresh();
        }
        internal void Save() => SafeConfig.Save(File, PathTracingPlugin.Name);

        private void Batch(Action change)
        {
            ++editing;
            try { change(); }
            finally { --editing; }
        }
        private void OnSettingChanged(object sender, SettingChangedEventArgs e)
        {
            if (editing != 0) return;
            // FoA Mod Manager or the config file: a pick other than the best is the player's own.
            if (e.ChangedSetting == Denoiser) DenoisingChosen.Value = CurrentDenoiser != Session.BestDenoiser;
            RefreshVisibility();
            Save();
            ModManagers.RequestFoARefresh();
            Changed?.Invoke();
        }
        private static ConfigDescription Row(int order, string displayName, AcceptableValueBase values, string description)
        {
            var row = new ConfigurationManagerAttributes
            {
                DisplayName = displayName, DispName = displayName, DisplaySection = Section, Category = Section, SortOrder = order, Visible = true
            };
            rows[displayName] = row;
            return new ConfigDescription(description, values, row);
        }
        private static ConfigDescription Hidden(string description) =>
            new ConfigDescription(description, null, new ConfigurationManagerAttributes { Visible = false });
    }
}
