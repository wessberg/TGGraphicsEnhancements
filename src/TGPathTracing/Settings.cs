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
    public enum TracingMode { Off, On }
    public enum DenoiserChoice { None, Nrd, RayRegeneration, RayReconstruction }
    public enum ReflectionChoice { Off, Glossy, All }
    // The Path Tracing section: exactly the rows of the Video settings (PathTracingRow), which FoA Mod Manager
    // lists with the same names, choices and order. The hotkeys, the scene switches and whether Denoising holds
    // the player's own pick are config-only.
    internal sealed class Settings
    {
        private const string Section = "Path Tracing", Advanced = "Advanced";
        internal readonly ConfigFile File;
        internal readonly ConfigEntry<string> Mode, View, Reflections;
        internal ConfigEntry<string> Denoiser { get; private set; }
        internal readonly ConfigEntry<bool> DenoisingChosen;
        internal readonly ConfigEntry<float> IndirectColor, SkyIntensity, ReflectionBoost, RayDistance, SceneRadius, Exposure, ExposureLowest;
        internal readonly ConfigEntry<int> Bounces, RaysPerPixel, SpatialSamples, DirectLightHistory, BouncedLightHistory, DarkAdaptation, ShadowDetail;
        internal readonly ConfigEntry<bool> WaterReflections, BoilingFilter, EmissiveLights, RadianceCache, SpatialReuse, TemporalReuse, DirectLightVisibility, SpatialFeedback;
        internal readonly ConfigEntry<bool> CreviceLight, HairShadows, FillLightFog;
        internal readonly ConfigEntry<KeyboardShortcut> ModeKey, ViewKey, ProbeKey;
        internal readonly ConfigEntry<bool> SceneObjects, DrakeWorld, Terrain, Vegetation, Lights, Characters, NoIndirectFromHdrp;
        internal static readonly string[] ModeNames = { "Off", "On" };
        internal static readonly string[] ViewNames = { "None", "Albedo", "Normals", "Coverage", "Materials", "Indirect", "Indirect raw", "Direct", "Surfaces", "Specular", "Emissive" };
        // Indexed by DenoiserChoice; the row lists only those this PC runs. The tracer's own filters are no choice of their
        // own (the player's): NRD takes their place wherever the tracer denoises the image itself.
        internal static readonly string[] DenoiserNames = { "None", "NRD", "Ray Regeneration", "Ray Reconstruction" };
        // Indexed by ReflectionChoice.
        internal static readonly string[] ReflectionNames = { "Off", "Glossy Surfaces", "All Surfaces" };
        internal const string DenoiserDescription =
            "Ray Reconstruction: NVIDIA DLSS, with DLSS upscaling. Ray Regeneration: AMD FSR, on a Radeon RX 9000. " +
            "NRD: NVIDIA's denoiser, on any GPU. None: no denoising.";
        // A value changed outside the Video tab (FoA Mod Manager, the config file, a hotkey): the rows follow.
        internal event Action Changed;
        private int editing;
        private static readonly Dictionary<string, ConfigurationManagerAttributes> rows = new Dictionary<string, ConfigurationManagerAttributes>();

        internal Settings(ConfigFile file)
        {
            File = file;
            int order = 0;
            // Earlier versions offered Global Illumination (HDRP's direct light with the traced bounced light added) and called
            // path tracing Full: a config set to either becomes On. Read before binding, which replaces a value the entry
            // doesn't accept with Off.
            string stored = StoredValue(file, "Mode");
            bool wasOn = stored == "Global Illumination" || stored == "Full";
            Mode = file.Bind(Section, "Mode", ModeNames[(int)TracingMode.On], Row(order++, "Path Tracing", new AcceptableValueList<string>(ModeNames),
                "On: the path tracer lights every surface with the sun and every light (ReSTIR DI, traced shadows), adds the " +
                "light that bounces off the scene (ReSTIR GI, with a radiance cache) in place of HDRP's baked indirect light, and " +
                "traces reflections. Needs an RTX 20, RX 6000, Arc or newer GPU."));
            if (wasOn) Mode.Value = ModeNames[(int)TracingMode.On];
            // Every choice until this PC's denoisers are known (RefreshDenoisers), so a stored one is kept. Earlier versions
            // offered Built-in (the tracer's own filters): NRD takes its place. Read before binding, which would replace the
            // value the entry no longer accepts with the first choice, None.
            string storedDenoising = StoredValue(file, "Denoising");
            Denoiser = file.Bind(Section, "Denoising", DenoiserNames[(int)DenoiserChoice.Nrd], Row(order++, "Denoising", new AcceptableValueList<string>(DenoiserNames), DenoiserDescription));
            if (storedDenoising == "Built-in") Denoiser.Value = DenoiserNames[(int)DenoiserChoice.Nrd];
            // Until the player picks one other than the best, Denoising follows the best this PC runs (RefreshDenoisers).
            DenoisingChosen = file.Bind(Advanced, "Denoising chosen", false, Hidden(
                "Whether Denoising holds the player's own pick; otherwise it follows the best denoiser this PC runs."));
            MigrateDenoiser(file);
            // The traced light's exposure (HdrpPasses.TracedGainTarget): the game's own, dark places brightened part of the way.
            Exposure = file.Bind(Section, "Exposure", 0f, Row(order++, "Exposure", new AcceptableValueRange<float>(-3f, 3f),
                "How bright the path-traced image is, in stops: +1 twice as bright, -1 half as bright. 0 is automatic: the " +
                "game's own exposure, with dark places brightened as far as Dark Adaptation sets."));
            MigrateExposure(file);
            DarkAdaptation = file.Bind(Section, "Dark adaptation", 50, Row(order++, "Dark Adaptation", new AcceptableValueRange<int>(0, 100),
                "How far dark places (interiors, night, deep shade) are brightened, as eyes adjust to the dark, in percent. 0 keeps " +
                "the game's own exposure, which is set for the fill lights path tracing leaves out, so interiors come out dark; " +
                "100 brightens them all the way to a mid tone. The brightest parts of the view are kept from turning white."));
            ShadowDetail = file.Bind(Section, "Shadow detail", 50, Row(order++, "Shadow Detail", new AcceptableValueRange<int>(0, 100),
                "Lifts the dark parts of a dark, uneven view (a room lit by one candle) while leaving everything brighter than a mid " +
                "tone as it is, as eyes adjust to a dim room: on its own only there, not in daylight or in an evenly lit room. " +
                "0 shows the contrast as traced."));
            RaysPerPixel = file.Bind(Section, "Rays per pixel", 1, Row(order++, "Rays Per Pixel", new AcceptableValueRange<int>(1, 16),
                "Indirect-light rays traced from every pixel each frame, each followed for the set bounces. At 1 the bounced light " +
                "is ReSTIR's, shared between neighbouring pixels and frames: smooth, but it can boil. More rays each count on " +
                "their own, taking over from the sharing: less boiling, finer noise the denoiser takes out. The bounced light's " +
                "GPU time grows about in proportion."));
            // 1 by default: in game no difference showed between 1 and 4 (the radiance cache carries the light of the bounces
            // after the last), and the bounced light was most of a frame's GPU time.
            Bounces = file.Bind(Section, "Bounces", 1, Row(order++, "Bounces", new AcceptableValueRange<int>(1, 4),
                "How many surfaces each indirect-light path is traced across before it takes the Radiance Cache's light, which " +
                "holds the light of every bounce after (the sky's too). More traces those bounces themselves, at a cost. Without " +
                "the Radiance Cache the light stops at the last bounce: at 1 the surfaces the rays meet are lit by the sun and lights only."));
            RadianceCache = file.Bind(Section, "Radiance cache", true, Row(order++, "Radiance Cache", null,
                "Keeps the bounced light reaching each part of the world from frame to frame, so light bounces on past the last " +
                "bounce and reflections show surfaces lit by bounced light. Off stops the light at the last bounce."));
            RemoveIndirectIntensity(file);
            IndirectColor = file.Bind(Section, "Indirect color", 3f, Row(order++, "Indirect Color", new AcceptableValueRange<float>(0f, 4f),
                "How much color the bounced light keeps from the surfaces it bounced off, at the same brightness: 1 as traced, " +
                "0 grey, more exaggerates the tint (a red cloth reddening the floor beside it); the default 3 triples it."));
            // Earlier versions stored Reflections on or off: on traced every surface's, All Surfaces. Read before binding, which
            // replaces a value the entry doesn't accept with the first choice.
            string storedReflections = StoredValue(file, "Reflections");
            Reflections = file.Bind(Section, "Reflections", ReflectionNames[(int)ReflectionChoice.All], Row(order++, "Reflections",
                new AcceptableValueList<string>(ReflectionNames),
                "Traced reflections in place of HDRP's reflection probes. All Surfaces traces them on every surface; Glossy Surfaces " +
                "only on the glossy ones (wet stone, metal, polished wood), and the rough rest reflect the bounced light the " +
                "radiance cache holds around them, nearly the same and faster. Off keeps HDRP's reflection probes."));
            if (string.Equals(storedReflections, "false", StringComparison.OrdinalIgnoreCase)) Reflections.Value = ReflectionNames[(int)ReflectionChoice.Off];
            else if (string.Equals(storedReflections, "true", StringComparison.OrdinalIgnoreCase)) Reflections.Value = ReflectionNames[(int)ReflectionChoice.All];
            // HdrpPasses records the water's reflections (render event 4) only while it is on.
            WaterReflections = file.Bind(Section, "Water reflections", true, Row(order++, "Water Reflections", null,
                "Water reflects the traced scene, what is off screen or hidden too, instead of the sky or a stored picture of the place."));
            HairShadows = file.Bind(Section, "Hair shadows", true, Row(order++, "Hair Shadows", null,
                "Traced shadows on hair and eyes: a lamp or the sun behind a wall or a head no longer lights them. Off leaves " +
                "them lit as the game lights them."));
            // The fill lights the tracer leaves out light no surface while tracing (SceneLights: the traced light takes their
            // place); an interior's haze is theirs, though (the tavern's 24 of them light its fog), as much an artist's fog as
            // a light, so they keep lighting the fog by default.
            FillLightFog = file.Bind(Section, "Fill light fog", true, Row(order++, "Fill Light Fog", null,
                "The game's fill lights, which path tracing replaces, still light its fog as in the game, so interiors keep their " +
                "haze; they light no surfaces either way. Off: only the traced lights light the fog. For comparing the two."));
            SpatialSamples = file.Bind(Section, "Spatial samples", 4, Row(order++, "Spatial Samples", new AcceptableValueRange<int>(0, 16),
                "Neighbouring pixels ReSTIR borrows samples from each frame. More is smoother and slower."));
            BoilingFilter = file.Bind(Section, "Boiling filter", true, Row(order++, "Boiling Filter", null,
                "Drops a sample of bounced light far brighter than the ones around it (NVIDIA RTXDI's boiling filter): it keeps rare " +
                "bright samples from spreading as blotches, but also throws real light away next to strong lights, about a fifth of " +
                "a candle-lit room's bounced light. Off keeps it all. For comparing the two."));
            // On by default: the light glowing surfaces cast converges to the same either way (the harness's floor under a glowing
            // panel within 0.1 % on and off), with far less noise sampled as a light (a frame's error 1.7 times lower with ReSTIR's
            // reuse, 5 times with its temporal reuse alone), and the radiance clamps keep it (off, they cut that panel's light to
            // 22 %, as rare bright hits; on, 98 % stayed). A budget of triangles bounds the cost.
            EmissiveLights = file.Bind(Section, "Emissive lights", true, Row(order++, "Emissive Lights", null,
                "Glowing surfaces (embers, lit windows, magic crystals) are sampled as lights, so the light they cast on what is " +
                "around them comes out far less noisy. Off leaves it to the rays that happen to meet them. For comparing the two."));
            // ReSTIR GI's reuse without its biases in dark gaps (TGPT_OPTION_GI_VISIBILITY, tracer.cpp): measured in the harness,
            // the mouth of a gap under a canvas 14 % short without it, and 15 to 30 % more error after denoising with it (not
            // yet judged in game), so it is off by default and a row, to compare.
            CreviceLight = file.Bind(Section, "Crevice light", false, Row(order++, "Crevice Light", null,
                "Shares bounced light only between pixels that can see it, so the mouths of gaps and the shade under cloth keep all " +
                "of theirs (no light is added), for a little more noise. Off is quieter but darker there."));
            RemoveTemporalHistory(file);
            RayDistance = file.Bind(Section, "Ray distance", 500f, Row(order++, "Ray Distance", new AcceptableValueRange<float>(20f, 3000f),
                "How far rays travel, in metres, before they count as reaching the sky."));
            SceneRadius = file.Bind(Section, "Scene radius", 250f, Row(order++, "Scene Radius", new AcceptableValueRange<float>(30f, 2000f),
                "Objects within this many metres of the camera are in the traced scene. Larger costs memory and time."));
            SkyIntensity = file.Bind(Section, "Sky intensity", 1f, Row(order++, "Sky Intensity", new AcceptableValueRange<float>(0f, 4f),
                "Scales the light rays gather from HDRP's sky."));
            ReflectionBoost = file.Bind(Section, "Reflection boost", 0.1f, Row(order++, "Reflection Boost", new AcceptableValueRange<float>(0f, 1f),
                "Every surface glossier and more reflective than the game made it, so the traced reflections show. " +
                "0 leaves the materials as they are, the default 0.1 adds a little, 1 makes them nearly mirrors (to judge the reflections)."));
            View = file.Bind(Section, "Debug view", ViewNames[0], Row(order++, "Debug View", new AcceptableValueList<string>(ViewNames),
                "Shows what the tracer sees instead of the image. Coverage: every traced object in its own color, dark where HDRP " +
                "draws something the traced scene lacks. Materials: the traced scene's colors along camera rays."));
            ModeKey = file.Bind(Section, "Mode key", new KeyboardShortcut(KeyCode.Insert), Hidden(
                "Turns path tracing on and off. Not F9: games commonly load the quick save with it."));
            ViewKey = file.Bind(Section, "Debug view key", new KeyboardShortcut(KeyCode.Insert, KeyCode.LeftShift), Hidden("Cycles the debug views."));
            ProbeKey = file.Bind(Section, "Probe key", new KeyboardShortcut(KeyCode.F8), Hidden(
                "Captures what the tracer sees, for finding out why something looks wrong (hold still for about 8 seconds): the image, each debug view, " +
                "the game's own rendering, and a report of the exposure, the lights near the camera and what the game has under the crosshair, " +
                "in BepInEx/TGPathTracing/Captures."));
            SpatialReuse = file.Bind(Advanced, "Spatial reuse", true, Hidden("ReSTIR's spatial reuse between neighbouring pixels."));
            TemporalReuse = file.Bind(Advanced, "Temporal reuse", true, Hidden("ReSTIR's temporal reuse from the previous frame."));
            SpatialFeedback = file.Bind(Advanced, "Spatial reuse feeds the next frame", false, Hidden(
                "ReSTIR's spatial reuse result is what the next frame's temporal reuse starts from, as NVIDIA RTXDI's sample does: faster " +
                "convergence, but samples less independent of their neighbours for DLSS Ray Reconstruction. For testing."));
            DirectLightVisibility = file.Bind(Advanced, "Direct light visibility reuse", false, Hidden(
                "ReSTIR DI keeps whether a pixel sees its light (NVIDIA RTXDI's initial visibility and ray-traced bias correction): fewer " +
                "bright dots inside strong lights' shadows and less noise, but lamps' shadow edges several times softer. For testing."));
            DirectLightHistory = file.Bind(Advanced, "Direct light history", 0, Hidden(
                "How many frames ReSTIR DI keeps reusing a light choice (NVIDIA RTXDI's maxHistoryLength), 1 to 64; 0 is its default of 20. For testing."));
            // Earlier keys for it, "Auto exposure lowest EV" (2) and "Auto exposure darkest EV" (5), each left the tavern
            // overblown: replaced (configs keep a key's saved value, not its new default).
            file.Remove(file.Bind(Advanced, "Auto exposure lowest EV", 2f, Hidden("No longer used.")).Definition);
            file.Remove(file.Bind(Advanced, "Auto exposure darkest EV", 5f, Hidden("No longer used.")).Definition);
            ExposureLowest = file.Bind(Advanced, "Auto exposure darkest scene EV", 7f, Hidden(
                "How far the traced light's exposure may brighten a dark scene: down to this EV100 from the game's own exposure " +
                "(a candle-lit room is about 3, moonlight about -2); lower lets dark interiors come out brighter. For tuning."));
            BouncedLightHistory = file.Bind(Advanced, "Bounced light history", 0, Hidden(
                "How many frames ReSTIR GI keeps reusing a light path (NVIDIA RTXDI's maxHistoryLength), 1 to 64; 0 is its default of 8. For testing."));
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
        internal ReflectionChoice CurrentReflections
        {
            get { int i = Array.IndexOf(ReflectionNames, Reflections.Value); return i < 0 ? ReflectionChoice.All : (ReflectionChoice)i; }
        }
        internal DenoiserChoice CurrentDenoiser
        {
            get { int i = Array.IndexOf(DenoiserNames, Denoiser.Value); return i < 0 ? DenoiserChoice.Nrd : (DenoiserChoice)i; }
        }
        internal uint Options =>
            (SpatialReuse.Value ? 0u : Tgpt.OptionNoSpatial) | (TemporalReuse.Value ? 0u : Tgpt.OptionNoTemporal) |
            (CurrentDenoiser == DenoiserChoice.None ? Tgpt.OptionNoDenoiser : 0u) | (CurrentReflections == ReflectionChoice.Off ? Tgpt.OptionNoSpecular : 0u) |
            (CurrentReflections == ReflectionChoice.Glossy ? Tgpt.OptionGlossyReflections : 0u) |
            (BoilingFilter.Value ? 0u : Tgpt.OptionNoGiBoiling) | (RadianceCache.Value ? 0u : Tgpt.OptionNoRadianceCache) |
            (EmissiveLights.Value ? Tgpt.OptionEmissiveLights : 0u) | (HairShadows.Value ? 0u : Tgpt.OptionNoHdrpShadows) |
            (DirectLightVisibility.Value ? Tgpt.OptionDiVisibility : 0u) | (SpatialFeedback.Value ? Tgpt.OptionSpatialFeedback : 0u) |
            (CreviceLight.Value ? Tgpt.OptionGiVisibility : 0u) |
            // NRD whenever the tracer denoises the image itself: chosen, or while the choice (Ray Reconstruction without DLSS
            // upscaling, Ray Regeneration that failed) doesn't run; the native side prefers those whenever they do.
            (CurrentDenoiser != DenoiserChoice.None ? Tgpt.OptionNrd : 0u);
        // The denoisers this PC runs, once known: the Denoising row lists only those. Until then every one is kept.
        internal static DenoiserChoice[] Offered(bool rayReconstruction, bool rayRegeneration, bool nrd) =>
            ((DenoiserChoice[])Enum.GetValues(typeof(DenoiserChoice)))
                .Where(c => (c != DenoiserChoice.RayReconstruction || rayReconstruction) && (c != DenoiserChoice.RayRegeneration || rayRegeneration) &&
                            (c != DenoiserChoice.Nrd || nrd)).ToArray();
        // What Denoising starts on and Restore Defaults picks: Ray Reconstruction, then Ray Regeneration, then NRD (None
        // only where NRD didn't start either).
        internal static DenoiserChoice Best(DenoiserChoice[] offered)
        {
            if (offered == null) return DenoiserChoice.Nrd;
            if (offered.Contains(DenoiserChoice.RayReconstruction)) return DenoiserChoice.RayReconstruction;
            if (offered.Contains(DenoiserChoice.RayRegeneration)) return DenoiserChoice.RayRegeneration;
            if (offered.Contains(DenoiserChoice.Nrd)) return DenoiserChoice.Nrd;
            return DenoiserChoice.None;
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
                    Denoiser = File.Bind(Denoiser.Definition, DenoiserNames[(int)DenoiserChoice.Nrd], description);
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
        // they offered; path tracing traces every light as it is.
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
        // Earlier builds kept the exposure's offset under Advanced as "Auto exposure compensation": it carries over to Exposure.
        private void MigrateExposure(ConfigFile file)
        {
            var legacy = file.Bind(Advanced, "Auto exposure compensation", 0f, Hidden("Replaced by Exposure."));
            float old = legacy.Value;
            file.Remove(legacy.Definition);
            if (old != 0f) Exposure.Value = Mathf.Clamp(old, -3f, 3f);
        }
        // Earlier versions stored "Denoiser" (Auto, None, Built-in; Off before that): the pick carries over (Built-in as NRD,
        // which took its place), Auto follows the best.
        private void MigrateDenoiser(ConfigFile file)
        {
            var legacy = file.Bind(Section, "Denoiser", "", Hidden("Replaced by Denoising."));
            string old = legacy.Value;
            file.Remove(legacy.Definition);
            DenoiserChoice? pick = old == "None" || old == "Off" ? DenoiserChoice.None : old == "Built-in" ? DenoiserChoice.Nrd
                : old == "DLSS Ray Reconstruction" ? DenoiserChoice.RayReconstruction : old == "AMD Ray Regeneration" ? DenoiserChoice.RayRegeneration
                : (DenoiserChoice?)null;
            if (old.Length == 0) return;
            if (pick.HasValue) Denoiser.Value = DenoiserNames[(int)pick.Value];
            DenoisingChosen.Value = pick.HasValue;
        }

        // The Video tab applied these values.
        internal void Store(TracingMode mode, DenoiserChoice denoiser, float exposure, int darkAdaptation, int shadowDetail, int rays, int bounces,
                            bool radianceCache, float indirectColor, ReflectionChoice reflections, bool waterReflections, bool hairShadows, bool fillLightFog, int spatial,
                            bool boilingFilter, bool emissiveLights, bool creviceLight, float distance, float radius, float sky, float reflectionBoost,
                            DebugView view)
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
                Exposure.Value = exposure; DarkAdaptation.Value = darkAdaptation; ShadowDetail.Value = shadowDetail;
                RaysPerPixel.Value = rays; Bounces.Value = bounces; RadianceCache.Value = radianceCache; IndirectColor.Value = indirectColor; Reflections.Value = ReflectionNames[(int)reflections];
                WaterReflections.Value = waterReflections; HairShadows.Value = hairShadows; FillLightFog.Value = fillLightFog; BoilingFilter.Value = boilingFilter;
                EmissiveLights.Value = emissiveLights; CreviceLight.Value = creviceLight;
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
