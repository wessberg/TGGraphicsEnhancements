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
        internal readonly ConfigEntry<bool> ParticleSimulationDiagnostic;
        internal readonly ConfigEntry<float> IndirectColor, SkyIntensity, ReflectionBoost, Exposure, ExposureLowest, HighlightsKept;
        internal readonly ConfigEntry<int> Bounces, RaysPerPixel, SpatialSamples, DirectLightHistory, BouncedLightHistory, DarkAdaptation;
        internal readonly ConfigEntry<bool> BoilingFilter, EmissiveLights, RadianceCache, SpatialReuse, TemporalReuse, DirectLightVisibility, SpatialFeedback, CameraShift;
        internal readonly ConfigEntry<bool> CreviceLight, FillLightFog, Caustics;
        internal readonly ConfigEntry<bool> SmoothSurfaces, LightsAsEmitters, ReflectionsTopMip, OwnCameraRays, WetGround;
        // PT-81 on trial (docs/agents/water-9.md): the caustics' light on what is smaller than their grid (TGPT_OPTION_CAUSTICS_SPLIT).
        internal readonly ConfigEntry<bool> CausticsSplit, CausticsLit;
        // PT-114 on trial (docs/agents/caustgrid.md): the caustics' grid does the same with less work, in three steps (TGPT_FRAME_CAUSTICS_GRID).
        internal readonly ConfigEntry<int> CausticsGrid;
        internal readonly ConfigEntry<bool> WaterShoreFoam, WaterBodyLamps, WaterLightWithoutView;
        internal readonly ConfigEntry<KeyboardShortcut> ModeKey, ViewKey, ProbeKey;
        internal readonly ConfigEntry<bool> SceneObjects, DrakeWorld, Terrain, Vegetation, Lights, Characters, NoIndirectFromHdrp;
        // PT-85 on trial: where what is reused of the frame before is fetched from (TGPT_Frame.jitter's z).
        internal readonly ConfigEntry<int> ReuseFollowsShift;
        // PT-85 and PT-40: a lamp's near size (how far from its flame its light stops growing), and three switches still
        // on trial: which other use of a small lamp's size makes specks (TGPT_Frame.jitter's w, LampTrial).
        internal readonly ConfigEntry<int> LampNearSize, LampTrialReflected, LampTrialHighlight, LampTrialShadow;
        // PT-85 and PT-40 on trial (docs/agents/flame-near.md): what stands right by a lamp found on purpose (LampTrial's bits
        // 14-21), and what is far past white rolled off above a knee (TGPT_Frame.frameReserved, HighlightShoulder).
        internal readonly ConfigEntry<int> LampTrialAround;
        internal readonly ConfigEntry<float> HighlightShoulderKnee, HighlightShoulderMost;
        // PT-13 and PT-35 on trial (docs/agents/glass-3.md): why the jars are cloudy (TGPT_Frame.cameraPosition's w).
        internal readonly ConfigEntry<int> GlassTrial;
        // PT-92 on trial (docs/agents/char-normals.md): characters shaded by the smooth normals the game's own skinning
        // leaves beside their positions (CharacterCapture; TGPT_SetSkinned's stride bits 16-23), and at 2 NRD's guide too.
        internal readonly ConfigEntry<int> CharacterNormals;
        // PT-93 on trial (docs/agents/grass-boil.md): a light's ray through grass, by a coin a blade or by the product
        // (TGPT_Frame.fogMip's w).
        internal readonly ConfigEntry<int> GrassShadowTrial;
        // PT-99 on trial (docs/agents/speed-1.md): speed that leaves the picture as it is, a bit each
        // (TGPT_Frame.prevCameraPosition's w).
        internal readonly ConfigEntry<int> SpeedTrial;
        // PT-112 (A), for tests only: HDRP draws under the traced picture as the game does (GamePasses.cs: the rule is that it
        // draws no opaque object and no decal while tracing).
        internal readonly ConfigEntry<bool> HdrpDraws;
        // PT-112 (B), ON TRIAL (docs/agents/fog-2.md): the game's fog traced by the path tracer, HDRP's opaque fog pass left out.
        internal readonly ConfigEntry<bool> FogScatteredOnce, HdrpTransparentsOff;
        internal readonly ConfigEntry<int> NrdTrial;
        internal readonly ConfigEntry<int> SkyTrial; // PT-112, the sky, on trial (SkyModel)
        // PT-108 and PT-104 on trial (docs/agents/rain-1.md): the rain wets what a ray meets as the game's own wetness pass
        // wets what the camera sees (RainWetness; TGPT_SetRain, a record of its own: no field of TGPT_Frame, no option bit).
        internal readonly ConfigEntry<int> RainWetness;
        // PT-95 on trial (docs/agents/kandra-lit.md): the textures a material reads looked at again whatever the game's
        // streaming says of them (TextureShare.Audit; the plugin alone, nothing travels to the native side).
        internal readonly ConfigEntry<int> TextureFollow;
        // PT-98, for measuring (docs/agents/arrival-1.md): 1 puts the plugin's part of the traced world's arrival back to the
        // rules of before (SceneCapture.ArrivalAsBefore); a change starts the traced scene over (Session.Tick).
        internal readonly ConfigEntry<int> ArrivalAsBefore;
        // PT-95 and PT-19 on trial (docs/agents/nrd-relief.md): the reflections' own filter, which serves with NRD too, keeps
        // a bumpy metal's relief (TGPT_Frame.prevCameraPosition's w, its bits 0 to 2).
        internal readonly ConfigEntry<int> ReflectionReliefTrial;
        // PT-109 on trial (docs/agents/megageo-1.md): NVIDIA RTX Mega Geometry for the tracer's acceleration structures
        // (TGPT_Frame.flags' bits 2 to 5; a native side built with -DTGPT_MEGA_GEOMETRY=ON, and a driver that reports it).
        internal readonly ConfigEntry<int> MegaGeometryTrial;
        // PT-110, for measuring (docs/agents/omm-1.md): what each kind of cut-out (leaves, hair, fences) costs the rays
        // (TGPT_Frame.flags' bits 6 to 13). 1, 2, 4 and 8 change the picture or its speed on purpose, for one step of a run.
        internal readonly ConfigEntry<int> MicromapsTrial;
        internal static readonly string[] ModeNames = { "Off", "On" };
        internal static readonly string[] ViewNames = { "None", "Albedo", "Normals", "Coverage", "Materials", "Indirect", "Indirect raw", "Direct", "Surfaces", "Specular", "Emissive", "Yardstick" };
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
            ParticleSimulationDiagnostic = file.Bind(Advanced, "Particle simulation diagnostic", false, Hidden(
                "Capture simulation buffer evidence for the harness. Requires its startup marker; does not draw or light particles."));
            DenoisingChosen = file.Bind(Advanced, "Denoising chosen", false, Hidden(
                "Whether Denoising holds the player's own pick; otherwise it follows the best denoiser this PC runs."));
            MigrateDenoiser(file);
            // The traced light's exposure (HdrpPasses.TracedGainTarget): the game's own, dark places brightened part of the way,
            // and each pixel by how dark it is (pt_common.hlsli highlight_hold): what is bright keeps the game's exposure.
            Exposure = file.Bind(Section, "Exposure", 0f, Row(order++, "Exposure", new AcceptableValueRange<float>(-3f, 3f),
                "How bright the path-traced image is, in stops: +1 twice as bright, -1 half as bright. 0 is automatic: the " +
                "game's own exposure, with dark places brightened as far as Dark Adaptation sets."));
            MigrateExposure(file);
            DarkAdaptation = file.Bind(Section, "Dark adaptation", 35, Row(order++, "Dark Adaptation", new AcceptableValueRange<int>(0, 100),
                "How far dark places (interiors, night, deep shade) are brightened, as eyes adjust to the dark, in percent. 0 keeps " +
                "the game's own exposure, which is set for the fill lights path tracing leaves out, so interiors come out dark; " +
                "100 brightens them all the way to a mid tone. The brightest parts of the view are kept from turning white."));
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
            // 1 as traced. It was 3 until then: a config that still holds that old default is
            // moved to 1, once.
            IndirectColor = file.Bind(Section, "Indirect color", 1f, Row(order++, "Indirect Color", new AcceptableValueRange<float>(0f, 4f),
                "How much color the bounced light keeps from the surfaces it bounced off, at the same brightness: 1 as traced " +
                "(the default), 0 grey, more exaggerates the tint (a red cloth reddening the floor beside it)."));
            var indirectColorAsTraced = file.Bind(Advanced, "Indirect color default moved to 1", false, Hidden("Set once the old default of 3 was moved to 1."));
            if (!indirectColorAsTraced.Value)
            {
                if (Mathf.Approximately(IndirectColor.Value, 3f)) IndirectColor.Value = 1f;
                indirectColorAsTraced.Value = true;
            }
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
            // The light through a glass bottle or flask focused onto what lies in its shadow (pt_common.hlsli caustic_through), the
            // water's caustic pattern on the light that shines through it (WaterCaustics, pt_compose.hlsl water_caustic), and the
            // sunlight the water reflects onto what faces it (water_reflected_sun).
            Caustics = file.Bind(Section, "Caustics", true, Row(order++, "Caustics", null,
                "The sun and lamps shining through a glass bottle, flask or potion are bent and focused by it, a bright spot or line " +
                "in its shadow, as real glass does. Sunlight reaching a lake bed or a pool's floor through the water ripples with the " +
                "water's caustic pattern, only where the light really passes through the water, and what faces the water from above " +
                "catches the rippling sunlight it reflects. Off: glass tints the light that passes through it without focusing it, " +
                "and water casts no pattern."));
            // No Hair Shadows row since hair and eyes are the tracer's (2026-09-28): what HDRP still shades itself (a character's
            // forward surface the traced scene lacks) always takes the traced shadows.
            // The fill lights the tracer leaves out light no surface while tracing (SceneLights: the traced light takes their
            // place); an interior's haze is theirs, though (the tavern's 24 of them light its fog), as much an artist's fog as
            // a light, so they keep lighting the fog by default.
            FillLightFog = file.Bind(Section, "Fill light fog", true, Row(order++, "Fill Light Fog", null,
                "The game's fill lights, which path tracing replaces, still light its fog as in the game, so interiors keep their " +
                "haze; they light no surfaces either way. Off: only the traced lights light the fog. For comparing the two."));
            // (No Fog Adaptation row since 2026-10-11: it gave HDRP's fog the traced light's gain, and the fog is the tracer's.)
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
            // PT-73: Internal highlight threshold for Dark Adaptation near bright lantern walls; retain the
            // accepted balance. This is not a player row.
            HighlightsKept = file.Bind(Advanced, "Highlights kept", 0f, Hidden("Dark Adaptation brightens what is dark and holds back what is already bright: " +
                "the brightness (as the game's exposure shows it) from which a lit surface is not brightened at all. 0 is the built-in level (1, chosen " +
                "by eye on 2026-10-08); lower holds more firmly (0.54), higher lets the surroundings of a lamp bloom more; 1000000 holds nothing back."));
            // PT-85, for finding a cause only: off, the tracer's camera rays carry no sub-pixel shift (as before the edges job of
            // 2026-10-08) while the game's upscaler is still told one: edges turn to stairs again. Never a row.
            CameraShift = file.Bind(Advanced, "Camera shift", true, Hidden("For finding causes only. Off: the path tracer's camera rays are not " +
                "shifted by a fraction of a pixel each frame as the game's own picture is, so edges come out as stairs. Leave it on."));
            TemporalReuse = file.Bind(Advanced, "Temporal reuse", true, Hidden("ReSTIR's temporal reuse from the previous frame."));
            SpatialFeedback = file.Bind(Advanced, "Spatial reuse feeds the next frame", false, Hidden(
                "ReSTIR's spatial reuse result is what the next frame's temporal reuse starts from, as NVIDIA RTXDI's sample does: faster " +
                "convergence, but samples less independent of their neighbours for DLSS Ray Reconstruction. For testing."));
            DirectLightVisibility = file.Bind(Advanced, "Direct light visibility reuse", false, Hidden(
                "ReSTIR DI keeps whether a pixel sees its light (NVIDIA RTXDI's initial visibility and ray-traced bias correction): fewer " +
                "bright dots inside strong lights' shadows and less noise, but lamps' shadow edges several times softer. For testing."));
            OwnCameraRays = file.Bind(Advanced, "Own camera rays", true, Hidden(
                "Each pixel shows what the tracer's own camera ray finds in the traced scene (PT-59). Off is the route before: each pixel's " +
                "surface from the game's own G-buffer, the tracer's own rays for hair alone. To compare; the Yardstick debug view and the " +
                "Yardstick line in the log say where the two differ. For testing."));
            SmoothSurfaces = file.Bind(Advanced, "Smooth round surfaces", true, Hidden(
                "Round things (jars, pots, cups, cloth folds, shoulders) are shaded by their meshes' smooth normals, as the game shades them. " +
                "Off shades them by their flat faces as before (glass and props the tracer draws itself, the direct light's fade near the " +
                "shadow line on everything, the built-in denoiser stopping at every face): to compare. For testing."));
            LightsAsEmitters = file.Bind(Advanced, "Lights sized as their emitters", true, Hidden(
                "Each flame's, lamp's and glowing thing's light is traced at the size of what emits it, a flame's at its flame. Off traces " +
                "them where the game places them, at the radius it gives them (2 cm to 30 cm, a fire's up to 1 m), as before: to compare. For testing."));
            // PT-46 (mirrors not sharp): the harness's mirror-sharp recipe holds a mirror read at the reflection's own ray cone
            // against one read at the textures' full size, to tell whether the mip a reflection reads keeps a mirror soft.
            ReflectionsTopMip = file.Bind(Advanced, "Reflections read the top mip", false, Hidden(
                "What reflections show is read from each texture at its full size, however far away it is: sharper in mirrors, but " +
                "far textures shimmer. Off reads each at the size it is seen, as the camera does. For testing."));
            // PT-52: where the waves have been the ground is wet and dries (pt_wet.hlsl); off for the harness's comparisons.
            WetGround = file.Bind(Advanced, "Wet ground", true, Hidden(
                "Sand and rock the waves have covered stay wet behind them, darker and glossy, and dry over minutes. Off keeps the " +
                "ground dry, as before: to compare. For testing."));
            // PT-81 on trial (docs/agents/water-9.md): a crab in shallow water was dark with the Caustics row on. The light
            // through the water is carried by a grid of rays; where the three rays of a grid triangle met different surfaces
            // (a small thing, an edge, a round stone) the triangle's light was left out. On: each ray's third of it lands on
            // what that ray met.
            CausticsSplit = file.Bind(Advanced, "Caustics land split light", false, Hidden(
                "On trial. Off: as before, the light through the water lands only where three neighbouring rays of it meet one surface, " +
                "so small things under water (a crab, a pebble, an edge) get little of it. On: each ray's share lands on what that ray met. " +
                "Needs the Caustics row on."));
            // PT-81 on trial (docs/agents/water-9.md): under water the sun's light was diffuse alone, so a shell had no
            // highlight and a normal map no relief. On: it comes from the direction it came from, as any light does.
            CausticsLit = file.Bind(Advanced, "Caustics light keeps its direction", false, Hidden(
                "On trial. Off: as before, what lies under water takes the light through the water as flat, diffuse light. " +
                "On: that light comes from the direction it came through the waves, so a surface under water shows its relief " +
                "and its highlight, as it does above water. Needs the Caustics row on."));
            // PT-114 on trial (docs/agents/caustgrid.md): the grid that carries the light through the water cost 4 to 7 ms of
            // every outdoor frame, whatever landed. Three steps, each with the same light landing: 1 the lights of each
            // piece of the grid are picked in a pass of their own; 2 and only the pieces with water under them are run (a
            // river's extent is mostly dry land); 3 and a light that cannot land anything in view is not traced.
            // 3 BY DEFAULT SINCE 2026-10-12: the game answered (run 20261012-114808, 0 / 1 / 2 / 3 / 0 in one run: the
            // caustics' stage 5.25 / 3.28 / 3.12 / 3.13 / 5.41 ms in a town, 5.34 / 3.42 / 3.12 / 2.75 / 5.27 in a forest;
            // the beach's frozen picture 56.0 / 56.2 / 56.3 with 0 / 3 / 0; offline the written triangles are the same,
            // checksum for checksum). 0 stays as the tests' yardstick until the old way is taken out (PT-114).
            CausticsGrid = file.Bind(Advanced, "Caustics grid trial", 3, Hidden(
                "3 is the default. 0: as before, for comparing. 1: the grid that carries the light through the water picks its " +
                "lights in a pass of their own. 2: and runs only where a water surface really lies under it. 3: and traces no " +
                "light that cannot land in view. The picture is the same at every step. Needs the Caustics row on."));
            WaterShoreFoam = file.Bind(Advanced, "Water shore foam", false, Hidden(
                "On trial. Foam from waves breaking over the traced shore, seen by every ray. Off keeps the existing crest foam."));
            WaterBodyLamps = file.Bind(Advanced, "Water body lamps", false, Hidden(
                "On trial. Lamps light the water inside, through its waves. Off keeps the previous sampling."));
            WaterLightWithoutView = file.Bind(Advanced, "Water light without view", false, Hidden(
                "On trial. Surfaces under water ask for light through the waves at their own position, including outside the view. " +
                "Needs Caustics on. More noise is possible."));
            DirectLightHistory = file.Bind(Advanced, "Direct light history", 0, Hidden(
                "How many frames ReSTIR DI keeps reusing a light choice (NVIDIA RTXDI's maxHistoryLength), 1 to 64; 0 is the default of 4 (RTXDI's 20 boiled on thin geometry). For testing."));
            // Earlier keys for it, "Auto exposure lowest EV" (2) and "Auto exposure darkest EV" (5), each left the tavern
            // overblown: replaced (configs keep a key's saved value, not its new default).
            file.Remove(file.Bind(Advanced, "Auto exposure lowest EV", 2f, Hidden("No longer used.")).Definition);
            file.Remove(file.Bind(Advanced, "Auto exposure darkest EV", 5f, Hidden("No longer used.")).Definition);
            ExposureLowest = file.Bind(Advanced, "Auto exposure darkest scene EV", 7f, Hidden(
                "How far the traced light's exposure may brighten a dark scene: down to this EV100 from the game's own exposure " +
                "(a candle-lit room is about 3, moonlight about -2); lower lets dark interiors come out brighter. For tuning."));
            BouncedLightHistory = file.Bind(Advanced, "Bounced light history", 0, Hidden(
                "How many frames ReSTIR GI keeps reusing a light path (NVIDIA RTXDI's maxHistoryLength), 1 to 64; 0 is its default of 8. For testing."));
            // PT-85, ON TRIAL (docs/agents/reuse-shift.md), never a row: the camera's rays carry another sub-pixel shift every
            // frame (PT-84), and what the tracer reuses of the frame before was fetched by a pixel's motion alone, up to a
            // render pixel beside the point: at thin things and edges that is another surface, the history is refused and the
            // raw sample shows (the "stars"). 1 fetches the tracer's own histories where the point was in the previous frame's
            // own pixel grid; 2 ReSTIR's reservoirs as well (measured offline: no gain at edges, more boiling inside).
            ReuseFollowsShift = file.Bind(Advanced, "Reuse follows camera shift", 1, Hidden(
                "On trial, for finding causes. 0: what the path tracer reuses of the frame before is fetched by a pixel's motion alone, as before. " +
                "1: its own histories for direct light, reflections and bounced light are fetched where the pixel's point was in the previous " +
                "frame's own pixel grid (the two frames' sub-pixel shifts' difference added). 2: ReSTIR's reuse across frames as well. Leave it at 0."));
            // PT-85 and PT-40, ON TRIAL (docs/agents/flame-specks.md), never rows: with each
            // lamp in its flame at its flame's size the tavern shows twice the specks of lamps
            // at the game's place and size. Four switches, one a suspect, all 0 (off) by
            // default; the lamps stay in their flames whatever these say. They travel packed in
            // TGPT_Frame.jitter's w (LampTrial); HDRP's radius for a lamp in its flags' top bits
            // (SceneLights, only while a switch is at 1). The near size is no trial any more. A
            // key of its own, so that a config holding the trial key's 0 takes this default. 15,
            // not 10: with the size going by a lamp's strength, 15 is where the tavern's wall
            // candelabra (burnt pixels 216 at 10, 28 at 15, 29 at 20) and the night scene's
            // torch (9,520, 3,050, 568; the game's own 3,380) stop burning, and the picture's
            // mean stays (run 20261009-075923).
            LampNearSize = file.Bind(Advanced, "Lamp near size", 15, Hidden(
                "In centimetres, 0 to 255. The game's lamps are far stronger than real flames: their strengths were set for lights a " +
                "metre across. Path tracing puts each lamp in its flame, where its own wax and holder would be burnt white and " +
                "throw bright specks around the room. Within this distance of a flame its light stops growing; from twice this " +
                "distance on nothing changes. The number is for a lamp as strong as a tavern candlestick's; a stronger lamp's " +
                "distance is wider in step, so that every candle's wax comes out equally bright. 0: off."));
            LampTrialReflected = file.Bind(Advanced, "Lamp trial reflected size", 0, Hidden(
                "On trial, for finding causes. How large the glowing ball is that see-through glass and water reflect a lamp as. " +
                "0: the lamp's own size, as before. 1: the radius the game gave the light. 2: five times its own size. Leave it at 0."));
            LampTrialHighlight = file.Bind(Advanced, "Lamp trial highlight size", 0, Hidden(
                "On trial, for finding causes. How large a lamp is for the highlight it makes on glossy surfaces. " +
                "0: the lamp's own size, as before. 1: the radius the game gave the light. 2: five times its own size. Leave it at 0."));
            LampTrialShadow = file.Bind(Advanced, "Lamp trial shadow size", 0, Hidden(
                "On trial, for finding causes. How large a lamp is for the softness of its shadows. " +
                "0: the lamp's own size, as before. 1: the radius the game gave the light. 2: five times its own size. Leave it at 0."));
            // PT-85 and PT-40, ON TRIAL (docs/agents/flame-near.md), never rows, both off (0) by default. The lamps stay in
            // their flames, and neither takes light away from a surface. The first travels in LampTrial's number (bits
            // 14-21), the two shoulder numbers packed in TGPT_Frame.frameReserved (HdrpPasses.HighlightShoulder).
            LampTrialAround = file.Bind(Advanced, "Lamp trial lit surroundings", 0, Hidden(
                "On trial. 0: off. 1 to 255: what stands within this many centimetres of a lamp (its wax, its holder, the wall behind it) " +
                "is found on purpose by the rest of the room's light, not only by the rare ray that happens to land on it: fewer bright " +
                "specks, the same light (30 is the one to try). Leave it at 0."));
            // PT-13 and PT-35, ON TRIAL (docs/agents/glass-3.md), never a row, off (0) by default: the picture as before. The
            // Crypt's glass jars come out cloudy with dark blotches where the game's are clear. A bit each, added up (1, 2 and 4
            // keep their meaning; 8 crosses the actual wall planes); they
            // travel in TGPT_Frame.cameraPosition's w (HdrpPasses) to the frame flags' bits 8-15 (pt_common glass_trial).
            GlassTrial = file.Bind(Advanced, "Glass trial", 0, Hidden(
                "On trial. 0: glass as before. Add up: 1, what is seen through see-through glass is no longer blurred by the glass's " +
                "roughness or bent by its bump map (the game shows it sharp; the glass's highlight keeps both); 2, the paint and dirt on " +
                "such glass only on the faces the game draws (a jar's film once a wall, not twice); 4, a glass thing has an inside " +
                "wherever its own piece of the mesh is closed, even with an open lid in the same mesh (a jar's wall bends the view " +
                "at its outline; without 1 its roughness then frosts it); 8, with 1, bend the view at the actual wall planes, " +
                "so different shading normals on the two sides do not turn a thin wall into a lens. Compare 7 and 15. Leave it at 0."));
            // PT-92 (docs/agents/char-normals.md), never a row, ON (1) since the game said yes (run 20261009-084003: a helmet's
            // bands and a cloak's facets gone, the metal scene's speck spots 91.5 a frame to 0.6); 0 is the old look. A character
            // reached the tracer as skinned positions alone, so every triangle of it was shaded flat (a guard's helmet in bands,
            // a cloak in facets). The game's skinning leaves each vertex's skinned normal beside its position; at 1 the
            // tracer takes it with the position, and a character's hit is as round as the game shades it, for the camera's
            // rays, reflections and bounces alike, and the normal NRD is handed as its guide is the smooth one as well.
            CharacterNormals = file.Bind(Advanced, "Character normals", 1, Hidden(
                "1: characters are shaded by the smooth normals the game's own skinning gives their vertices, with their normal " +
                "maps laid on those: round helmets, cloaks and faces, on screen and in reflections, and the NRD denoiser is told " +
                "the smooth shape as well. 0: by their flat triangles, the old look."));
            HighlightShoulderKnee = file.Bind(Advanced, "Highlight shoulder knee", 0f, Hidden(
                "On trial, with 'Highlight shoulder most'. The brightness (as the game's exposure shows it) above which the path-traced " +
                "light rolls off. 0: where 'Highlights kept' stops holding (1 unless set). Nothing under it changes."));
            HighlightShoulderMost = file.Bind(Advanced, "Highlight shoulder most", 0f, Hidden(
                "On trial. 0: off. Above 0: the brightness the brightest path-traced light comes to at most, rolling off smoothly above " +
                "the knee, so what is far past white (a candle's wax right under its flame) keeps its shape and color. Try 3, 6 and 12. " +
                "A flame itself is not held. Leave it at 0."));
            // PT-93, ON TRIAL (docs/agents/grass-boil.md), never a row, off (0) by default: grass lit by a fire on the ground
            // flickers in every frame (the campfire: 460 speck spots a frame with NRD, the game's own picture 1). A light's
            // shadow ray flips a coin at every blade it meets; a ray that runs level through a tuft meets many, so it arrives
            // in one frame of many, carrying all of the light. 1: the same ray takes the product of what each blade lets
            // through, the coin's own mean, the same in every frame. It travels in TGPT_Frame.fogMip's w.
            GrassShadowTrial = file.Bind(Advanced, "Grass shadow trial", 0, Hidden(
                "On trial. 0: a light's ray passes each blade of grass or leaf in its way by chance, as before. 1: it takes the share " +
                "each blade lets through, multiplied: the same soft, partial shadows from grass and leaves, without the flicker " +
                "in grass lit by a fire or a lamp near the ground. Leave it at 0."));
            // PT-99, ON TRIAL (docs/agents/speed-1.md), never a row, off (0) by default: the tracer ran at 8 frames a second
            // in a town square. Work that can go without changing the picture, a bit each, added up; they travel in
            // TGPT_Frame.prevCameraPosition's w (HdrpPasses) to the frame flags' bits 16-31 (pt_common speed_trial). 1: every
            // shadow ray that nothing stopped asked "which water does this point lie in" with one more ray, straight up, as
            // far as the scene reaches, wherever the scene holds any water; with the bit only a point under the water's
            // highest point asks (no water stands over the rest). 8, for finding causes: the bounced light's and the
            // reflections' passes run as plain compute passes, without Shader Execution Reordering (the same picture; what
            // the regrouping gains or costs shows in the log's stage times). 16 and up do something in a measuring build alone.
            // 2 and 4 (docs/agents/perf-1.md; pt_water_path.hlsli water_regroup), in a build of the native DLL configured
            // with -DTGPT_REGROUP_WATER=ON alone: the water's own path regroups its threads by what each does next, 2 at
            // every turn, 4 before a point makes its own patch of waves (the same picture, another speed).
            // (16 and 32768 were trials of this number until 2026-10-10, docs/agents/rule-1.md: the lamps a reflection off
            // water or glass shows are found by a search tree, and the two big passes regroup by whether a pixel feeds the
            // radiance cache, in every build now and with no switch. 32, a ray inside the water asked only as far as its
            // path flies, gave no gain in the game and came out.)
            SpeedTrial = file.Bind(Advanced, "Speed trial", 0, Hidden(
                "On trial. 0: as before. Add up: (1 is the rule since round 20 and does nothing more); 2 and 4, the light in water is worked " +
                "out in another order (the same picture, another speed; only in a build made to try it); 8, for finding causes: " +
                "bounced light and reflections without Shader Execution Reordering (the same picture, another speed); 16 or 32, " +
                "the reflections always regrouped by material or always with no hint, where the path tracer otherwise picks the " +
                "faster itself (the same picture); 64, for one comparison, the sun's light inside water worked out as " +
                "before 2026-10-12 (as fine as the waves for every ray; another speed); " +
                "128 or 256, for measuring, the micromaps of leaves and grass at level 4 " +
                "as before or at 5 (the rule is 6: the same picture, another speed). The numbers from 512 to 32768 are for a measuring " +
                "build of the path tracer and do nothing in this one. Leave it at 0."));
            // PT-112 (A), never a row: the rule since 2026-10-11 is that HDRP draws no opaque object and no decal under the traced
            // picture (GamePasses.cs); on, it draws them as the game does, for a test that looks at its G-buffer (the yardstick).
            // (It replaces round 19's "Game passes while tracing", whose other numbers measured passes that cost nothing.)
            HdrpDraws = file.Bind(Advanced, "HDRP draws under the traced picture", false, Hidden(
                "For tests only. Off: while path tracing is on screen the game's renderer draws no solid object and no decal under " +
                "it, and the path tracer gives the frame its depth and motion. On: the game draws them as without path tracing, " +
                "for a comparison with its own surfaces (the Yardstick view and the old route do this by themselves). Leave it off."));
            // PT-112 (B), the rule since 2026-10-11: the game's fog traced along each camera ray
            // (TGPT_FRAME_TRACED_FOG), with the light it scatters more than once
            // (TGPT_FRAME_FOG_WALK: the mist's cache cells), HDRP's own opaque fog pass left out
            // (GamePasses). Never a row; one hidden switch for tests. Its light is held in cells
            // of a grid that follows the camera, as NVIDIA's RTX Remix builds its fog (PT-118).
            FogScatteredOnce = file.Bind(Advanced, "Traced fog: light scattered once only", false, Hidden(
                "For tests only. Off: the traced fog carries the light it scatters from droplet to droplet too. On: only the light it " +
                "scatters once. Leave it off."));
            // PT-37 / PT-112 / PT-127: All physical emitters light the authored fog, including
            // outdoors, with multiple scattering.
            NrdTrial = file.Bind(Advanced, "NRD trial", 0, Hidden(
                "For a look only. 0 or 1: NRD's RELAX (NVIDIA's pick for this kind of path tracing). 2: NRD's REBLUR instead. " +
                "Leave it at 0."));
            HdrpTransparentsOff = file.Bind(Advanced, "HDRP transparents not drawn (trial)", false, Hidden(
                "For one test only. On: HDRP draws none of the game's transparent things (its effects vanish). Leave it off."));
            // PT-112, the sky, ON TRIAL (docs/agents/sky-1.md, sky-2.md; SkyModel.cs), never a
            // row, 0 by default until the game has answered. While the traced light is on screen
            // under HDRP's Physically Based Sky: 1, for measuring, HDRP's sky passes are left
            // out (its cube map's update, its sky and clouds drawn on the screen) and the path
            // tracer draws what the camera sees of the sky from the sky its rays read now
            // (HDRP's last); 2, the path tracer makes the sky itself (the atmosphere, the
            // clouds, the sun's and the moon's discs) for its rays and for the screen, by day;
            // at night (the game's stars and night sky, not made by the path tracer yet) HDRP
            // draws what the camera sees; 3, the same day and night (no stars yet: for looking).
            SkyTrial = file.Bind(Advanced, "Sky trial", 0, Hidden(
                "On trial. 0: the game draws the sky as before. 1, for measuring: the game's sky passes are left out and the path tracer " +
                "shows the sky its rays read now. 2: the path tracer makes the sky (air, haze, clouds, the sun and the moon) for its " +
                "rays and the screen by day; at night the game draws the stars as before. 3: the same day and night (no stars yet). " +
                "Leave it at 0."));
            // PT-108 and PT-104, ON TRIAL (docs/agents/rain-1.md), never a row, off (0) by default until the game has answered.
            // When it rains the game makes everything under the open sky wet: one pass over the screen rewrites how rough
            // each pixel is (nothing else: no bumps, no colour), by its moisture, a noise picture laid over the world and
            // a top-down height map that keeps what stands under a roof dry. The tracer knew nothing of it: the ground by
            // a fire in the rain was dry and even (run 20261009-190504: the terrain's roughness unlike the game's on 98 %
            // of its pixels). 1: every hit is wetted by the same rule, for every kind of ray, from the game's own moisture
            // and its own noise texture. NOT BUILT YET: the shelter (everything counts as under the open sky, indoors
            // too), which is why this stays 0. It needs a native side built with -DTGPT_RAIN_WETNESS=ON.
            RainWetness = file.Bind(Advanced, "Rain wetness", 0, Hidden(
                "On trial. 0: the path tracer knows nothing of the rain, as before. 1: while it rains, what a ray meets is as wet " +
                "(as smooth) as the game makes what the camera sees: wet ground with glints of a fire, in reflections too. " +
                "What stands under a roof is not kept dry yet. Leave it at 0."));
            // PT-95, ON TRIAL (docs/agents/kandra-lit.md), never a row, off (0) by default. A guard came out in blocks of
            // colour with no relief on his armour (run 20261009-084003): his materials read their 64x64 tiles, which is what
            // a material gets whose textures the game has replaced by new ones (its streaming loads and drops their sharp
            // levels) without the plugin following. 1 looks and names them in the log; 2 follows them as well.
            TextureFollow = file.Bind(Advanced, "Texture follow", 0, Hidden(
                "On trial, for finding a cause. 0: a texture the game loads sharper or coarser is looked at again only when the " +
                "game says its sharpness changed, as before. 1: every texture a traced material reads is looked at every few " +
                "seconds, and the log names those the path tracer no longer reads (it shows their coarse stand-ins there); the " +
                "picture stays as it is. 2: those are read again as well. Leave it at 0."));
            // PT-98, FOR MEASURING (docs/agents/arrival-1.md), never a row, 0 by default: the traced world arrives by the
            // clock. 1 puts the plugin's three rules back to what they were until 2026-10-09 16:00 (the time for registering
            // meshes counted from the frame's start, a scan every 2 s whatever waits, the textures looked at once a scan),
            // so that one run of one scene shows the world arrive both ways (the recipe `arrival`; the log's lines that
            // start with "Arrival"). A change starts the traced scene over: the next frame's world arrives from nothing.
            // The native side's own rules (what a frame takes in, by the clock) have no switch. It goes when the game has
            // shown which way is right.
            ArrivalAsBefore = file.Bind(Advanced, "Arrival as before", 0, Hidden(
                "For measuring. 0: the traced world is taken in by the clock, as fast at a low frame rate as at a high one. 1: " +
                "by the frame and by the scan, as before. A change starts the traced scene over. Leave it at 0."));
            // PT-95 and PT-19, ON TRIAL (docs/agents/nrd-relief.md), never a row, off (0) by default: the picture as before.
            // A guard's hammered iron helmet came out smooth bright metal with NRD, where the frame with no denoiser has the
            // game's relief (run 20261009-103336: its face kept 0.59 of 10.51 of the detail). NRD is handed the bounced
            // diffuse light alone; a metal has none, and its reflections go through the tracer's own filter, which spread
            // every bump's reflection over its neighbours. A bit each, added up (0 to 7); they travel in
            // TGPT_Frame.prevCameraPosition's w (HdrpPasses) to that filter's passes (pt_atrous pass.b.y).
            ReflectionReliefTrial = file.Bind(Advanced, "Reflection relief trial", 0, Hidden(
                "On trial. 0: reflections are smoothed as before. Add up: 4, each pixel keeps the memory of its own reflection " +
                "(the smoothed picture is no longer written back into it every frame), so dents and scratches stay where the " +
                "noise is smaller than they are; 1, two pixels share a reflection only as far as their surfaces face the same " +
                "way for it (a rough surface still shares widely, a glossy one hardly); 2, and only as far as they reflect " +
                "alike: a metal's stains stay too. For a hammered helmet or a door's iron strap with the NRD denoiser, try 4, " +
                "then 5 and 7. Glass, water and hair are left as they are. Leave it at 0."));
            // PT-109: Keep Mega Geometry enabled where supported; diagnose integration cost
            // rather than disabling it.
            MegaGeometryTrial = file.Bind(Advanced, "Mega Geometry trial", 7, Hidden(
                "7 is the default: NVIDIA RTX Mega Geometry, where the graphics driver offers it (elsewhere nothing changes). " +
                "0: the path tracer's acceleration structures as before, for comparing. 1, 2 or 3: the top-level structure is kept " +
                "from frame to frame and only the parts of it whose things changed are built again (NVIDIA RTX Mega Geometry; " +
                "the parts are cells of 64, 128 or 256 m), where the graphics driver offers it; the picture is the same. " +
                "4, 5 or 6: the small plants get structures of their own; at 5 they are built from clusters (NVIDIA RTX Mega " +
                "Geometry), where the graphics driver offers it. 7 or 8: every plant gets a structure of its own built from " +
                "clusters, in the kept top-level structure. Leave it at 7."));
            // PT-110, FOR MEASURING (docs/agents/omm-1.md), never a row, 0 by default: what the cut-outs cost. An opacity
            // micromap lets the graphics card settle most of a cut-out triangle (a leaf's card, a fence, hair) without
            // running the tracer's alpha test; where there is none, or the map says "unknown", every ray that crosses the
            // triangle asks the shader. The tracer's log counts them every 10 s ("PT cut-outs": by kind, and how much the
            // micromaps settle). These numbers bound what each kind costs, as NVIDIA's own guide advises (trace once
            // with the cut-outs solid): 1, the plants in the wind's pose; 2, the characters; 4, every other cut-out
            // (still meshes, things the game moves, cloth in the wind's pose). The picture is WRONG while one of them is
            // set: shadows, bounced light and reflections take leaves and hair for whole cards (the camera's own rays
            // still cut them out). 8: no micromap is used (the same picture, slower). 16: the parts of characters and of
            // plants in the wind that are no cut-out (a body, a trunk) are handed to the alpha test with their hair and
            // leaves again, as they were until 2026-10-10 (the same picture, slower: what marking them solid brought).
            // They travel in TGPT_Frame.flags' bits 6 to 13 (HdrpPasses). One step of one run each; never a default.
            // And ON TRIAL in the same number, off until the game has answered (M1): 32, the leaves and grass that sway
            // in the wind are traced with their meshes' micromaps too (until now only what stands still had any: a
            // town's 6.4 million swaying triangles were all left to the alpha test). The same picture, faster if it
            // works: a micromap settles only what the alpha test would decide. 64 with it, for finding a cause: their
            // structures are built anew at every pose of the wind in place of refitted (slow).
            MicromapsTrial = file.Bind(Advanced, "Micromaps trial", 0, Hidden(
                "For measuring. 0: as it ships. Add up: 1, the leaves and grass that sway in the wind are whole cards to " +
                "shadows, bounced light and reflections; 2, characters' hair and torn cloth the same; 4, every other " +
                "cut-out (fences, still trees, banners) the same; 8, the graphics card's opacity micromaps are left unused " +
                "(the same picture, slower); 16, the solid parts of characters and of swaying plants are tested like " +
                "their hair and leaves, as before (the same picture, slower). 1, 2 and 4 make the picture wrong on " +
                "purpose, to show in the log's stage times what each kind of cut-out costs. 32, swaying leaves " +
                "and grass without opacity micromaps, as before (the same picture, slower); 64, for finding a " +
                "cause only (slow). Leave it at 0."));
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
            (EmissiveLights.Value ? Tgpt.OptionEmissiveLights : 0u) |
            (DirectLightVisibility.Value ? Tgpt.OptionDiVisibility : 0u) | (SpatialFeedback.Value ? Tgpt.OptionSpatialFeedback : 0u) |
            (CreviceLight.Value ? Tgpt.OptionGiVisibility : 0u) |
            (Caustics.Value ? 0u : Tgpt.OptionNoCaustics) |
            (SmoothSurfaces.Value ? 0u : Tgpt.OptionFaceted) |
            (OwnCameraRays.Value ? 0u : Tgpt.OptionGBufferSurfaces) |
            (ReflectionsTopMip.Value ? Tgpt.OptionReflectionTopMip : 0u) |
            (WetGround.Value ? 0u : Tgpt.OptionDryGround) |
            (CausticsSplit.Value ? Tgpt.OptionCausticsSplit : 0u) | (CausticsLit.Value ? Tgpt.OptionCausticsLit : 0u) |
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
        internal void Store(TracingMode mode, DenoiserChoice denoiser, float exposure, int darkAdaptation, int rays, int bounces,
                            bool radianceCache, float indirectColor, ReflectionChoice reflections, bool caustics, bool fillLightFog, int spatial,
                            bool boilingFilter, bool emissiveLights, bool creviceLight, float sky, float reflectionBoost,
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
                Exposure.Value = exposure; DarkAdaptation.Value = darkAdaptation;
                RaysPerPixel.Value = rays; Bounces.Value = bounces; RadianceCache.Value = radianceCache; IndirectColor.Value = indirectColor; Reflections.Value = ReflectionNames[(int)reflections];
                Caustics.Value = caustics; FillLightFog.Value = fillLightFog; BoilingFilter.Value = boilingFilter;
                EmissiveLights.Value = emissiveLights; CreviceLight.Value = creviceLight;
                SpatialSamples.Value = spatial;
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
