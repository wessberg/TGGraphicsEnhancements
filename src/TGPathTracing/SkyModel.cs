// SPDX-License-Identifier: MIT
// PT-112, the sky, ON TRIAL (docs/agents/sky-1.md, sky-2.md; the hidden "Sky trial", 0 by default): HDRP's sky moved into the
// path tracer. The owner, 2026-10-11, of HDRP work that runs beside the trace: "Basically we want to move over anything that
// could have run sequentially or block the GPU before, so we know there's no HDRP bottlenecks and any GPU/CPU waits will come
// from our own rendering"; of the sky: "definitely bring that over too".
// Outdoors the game's sky is HDRP's Physically Based Sky with its Cloud Layer. While the traced light is on screen and the
// trial asks, for the gameplay camera:
// - HDRP's sky environment (its cube map of the sky, the cube's blur, its ambient probes) is no longer brought up to date
//   every frame: the sky's update mode is OnDemand through a volume of the plugin's own (only that parameter overridden, never
//   Add<T>(true)). With the tracer's own sky (2, 3) HDRP is asked for one update each time its sun has turned a degree, for
//   what HDRP still lights itself (transparent surfaces, its fog's colour); the measuring switch (1) asks for none.
// - HDRP's sky and clouds drawn on the screen (HDRenderPipeline.RenderSky, RenderClouds) are left out by Harmony prefixes, and
//   the composite draws every pixel whose camera ray met nothing (pt_compose, pt_sky.hlsli): from the map the rays read now
//   (1), or the tracer's own sky per pixel (2, 3). In 2 at night (the game's star layer, its own shader graph
//   TG/Skybox/PhysicallyBasedSky_Wyrdness, is not made by the tracer yet: sky-1 step 4) HDRP draws its sky and clouds as before.
// - Each frame the tracer is handed what HDRP's sky is for the camera (TGPT_SetSkyModel): the Physically Based Sky's numbers
//   as HDRP's own methods work them out, the directional lights that interact with the sky (the game's Sun and Moon, HDRP's
//   celestial bodies), the Cloud Layer with the game's cloud map (opened by the tracer, no copy) and how far the wind has
//   moved it (HDRP adds that only while it draws its clouds: kept here meanwhile, from where HDRP left it).
// A line in the log counts HDRP's renders of its sky into a cube map every 10 s, whatever the trial (sky-1: "does it run every
// frame in play?").
using System;
using System.Collections.Generic;
using System.Reflection;
using HarmonyLib;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.HighDefinition;
namespace TGPathTracing
{
    internal static class SkyModel
    {
        // HDRP's sky passes for the gameplay camera are left out this frame (the prefixes read it).
        private static bool skip;
        private static int skipFrame = -1;
        internal static bool SkipHdrpSky => skip && GamePasses.Active && skipFrame >= Time.frameCount - 1;
        private static GameObject host;
        private static Volume volume;
        private static int saidState = -1;
        private static NativeSky record;
        private static bool sentHdrp = true;
        // The wind's move kept while HDRP's clouds are left out (from where HDRP's own scrollFactor stood), per layer.
        private static readonly float[] windKept = new float[2];
        private static float windTime = -1f;
        private static Vector3 lastRequestSun; private static bool haveRequestSun;
        // RenderSkyToCubemap's count: the sky's cube map, and everything else HDRP renders the sky into (ambient probes).
        private static int cubes, probes, frames0; private static float nextCount;
        private static readonly Dictionary<int, string> saidOnce = new Dictionary<int, string>();

        private static readonly FieldInfo PlanetField = AccessTools.Field(typeof(HDCamera), "planet");
        private static FieldInfo planetRadius, planetCenter;
        private static readonly FieldInfo ScrollFactor = AccessTools.Field(typeof(CloudLayer.CloudMap), "scrollFactor");
        private static readonly MethodInfo CloudBakingHash = AccessTools.Method(typeof(CloudLayer), "GetBakingHashCode", new[] { typeof(Light) });
        private static readonly FieldInfo SunColorField = AccessTools.Field(typeof(HDAdditionalLightData), "sunColor");
        private static readonly FieldInfo SunIntensityField = AccessTools.Field(typeof(HDAdditionalLightData), "sunIntensity");
        private static readonly Type LightDatabase = AccessTools.TypeByName("UnityEngine.Rendering.HighDefinition.HDLightRenderDatabase");
        private static readonly PropertyInfo LightDatabaseInstance = LightDatabase != null ? AccessTools.Property(LightDatabase, "instance") : null;
        private static readonly FieldInfo DirectionalLights = LightDatabase != null ? AccessTools.Field(LightDatabase, "directionalLights") : null;
        private static Func<PhysicallyBasedSky, float> airScaleHeight, maximumAltitude, aerosolScaleHeight, aerosolExtinction, ozoneWidth, ozoneStart;
        private static Func<PhysicallyBasedSky, Vector3> airExtinction, airScattering, aerosolScattering, ozoneExtinction;
        private static bool reflected, reflectionFailed;

        private static T Method<T>(string name) where T : Delegate
        {
            var m = AccessTools.Method(typeof(PhysicallyBasedSky), name, Type.EmptyTypes);
            return m != null ? (T)Delegate.CreateDelegate(typeof(T), m) : null;
        }
        private static bool Reflect()
        {
            if (reflected) return !reflectionFailed;
            reflected = true;
            try
            {
                airScaleHeight = Method<Func<PhysicallyBasedSky, float>>("GetAirScaleHeight");
                maximumAltitude = Method<Func<PhysicallyBasedSky, float>>("GetMaximumAltitude");
                aerosolScaleHeight = Method<Func<PhysicallyBasedSky, float>>("GetAerosolScaleHeight");
                aerosolExtinction = Method<Func<PhysicallyBasedSky, float>>("GetAerosolExtinctionCoefficient");
                ozoneWidth = Method<Func<PhysicallyBasedSky, float>>("GetOzoneLayerWidth");
                ozoneStart = Method<Func<PhysicallyBasedSky, float>>("GetOzoneLayerMinimumAltitude");
                airExtinction = Method<Func<PhysicallyBasedSky, Vector3>>("GetAirExtinctionCoefficient");
                airScattering = Method<Func<PhysicallyBasedSky, Vector3>>("GetAirScatteringCoefficient");
                aerosolScattering = Method<Func<PhysicallyBasedSky, Vector3>>("GetAerosolScatteringCoefficient");
                ozoneExtinction = Method<Func<PhysicallyBasedSky, Vector3>>("GetOzoneExtinctionCoefficient");
                var planetType = PlanetField?.FieldType;
                planetRadius = planetType != null ? AccessTools.Field(planetType, "radius") : null;
                planetCenter = planetType != null ? AccessTools.Field(planetType, "center") : null;
                reflectionFailed = airScaleHeight == null || maximumAltitude == null || aerosolScaleHeight == null || aerosolExtinction == null ||
                    ozoneWidth == null || ozoneStart == null || airExtinction == null || airScattering == null || aerosolScattering == null ||
                    ozoneExtinction == null || planetRadius == null || planetCenter == null || ScrollFactor == null || DirectionalLights == null || LightDatabaseInstance == null;
            }
            catch (Exception e) { reflectionFailed = true; Session.Log.LogWarning("Sky trial: HDRP's sky could not be read in this game build: " + e.Message); }
            if (reflectionFailed) Session.Log.LogWarning("Sky trial: HDRP's Physically Based Sky, its planet or its lights were not found in this game build; the sky stays HDRP's.");
            return !reflectionFailed;
        }

        internal static void Patch(Harmony harmony)
        {
            var sky = AccessTools.Method(typeof(HDRenderPipeline), "RenderSky", new[] {
                typeof(UnityEngine.Rendering.RenderGraphModule.RenderGraph), typeof(HDCamera),
                typeof(UnityEngine.Rendering.RenderGraphModule.TextureHandle), typeof(UnityEngine.Rendering.RenderGraphModule.TextureHandle) });
            var clouds = AccessTools.Method(typeof(HDRenderPipeline), "RenderClouds");
            if (sky == null || clouds == null) throw new MissingMethodException("HDRenderPipeline.RenderSky or RenderClouds");
            harmony.Patch(sky, prefix: new HarmonyMethod(typeof(SkyModel), nameof(BeforeHdrpSky)));
            harmony.Patch(clouds, prefix: new HarmonyMethod(typeof(SkyModel), nameof(BeforeHdrpSky)));
            var manager = AccessTools.TypeByName("UnityEngine.Rendering.HighDefinition.SkyManager");
            var cube = manager != null ? AccessTools.Method(manager, "RenderSkyToCubemap") : null;
            if (cube != null) harmony.Patch(cube, prefix: new HarmonyMethod(typeof(SkyModel), nameof(CountCube)));
            else Session.Log.LogWarning("Sky: HDRP's SkyManager.RenderSkyToCubemap was not found; its renders of the sky are not counted.");
        }
        private static bool BeforeHdrpSky(HDCamera hdCamera)
        {
            if (!SkipHdrpSky || hdCamera == null) return true;
            var main = Camera.main;
            return main == null || hdCamera.camera == null || main.GetInstanceID() != hdCamera.camera.GetInstanceID();
        }
        private static void CountCube(object[] __args)
        {
            // Its last argument names what the cube is for: HDRP's sky cube map itself, or a low resolution one for a probe.
            string what = __args != null && __args.Length > 6 && __args[6] != null ? __args[6].ToString() : "";
            if (what == "RenderSkyToCubemap") ++cubes; else ++probes;
        }

        private static void EnsureVolume()
        {
            if (host != null) return;
            host = new GameObject("TG Path Tracing sky") { hideFlags = HideFlags.HideAndDontSave };
            host.SetActive(false);
            UnityEngine.Object.DontDestroyOnLoad(host);
            volume = host.AddComponent<Volume>();
            volume.isGlobal = true;
            volume.priority = 1000002f;
            volume.weight = 1f;
            var profile = ScriptableObject.CreateInstance<VolumeProfile>();
            profile.hideFlags = HideFlags.HideAndDontSave;
            // Only the update mode: the component's every other parameter is left to the game's volumes.
            profile.Add<PhysicallyBasedSky>().updateMode.Override(EnvironmentUpdateMode.OnDemand);
            volume.sharedProfile = profile;
        }

        // Main thread, every frame (Session): what this frame does with HDRP's sky, and the count's line.
        internal static void Tick(float now)
        {
            var settings = Session.Settings;
            int trial = settings != null ? Mathf.Clamp(settings.SkyTrial.Value, 0, 3) : 0;
            var tracer = Session.Tracer;
            var camera = Camera.main;
            PhysicallyBasedSky pbs = null; HDCamera hd = null;
            if (trial != 0 && camera != null && GamePasses.Active)
            {
                hd = HDCamera.GetOrCreate(camera);
                var environment = hd.volumeStack.GetComponent<VisualEnvironment>();
                if (environment != null && environment.skyType.value == (int)SkyType.PhysicallyBased) pbs = hd.volumeStack.GetComponent<PhysicallyBasedSky>();
            }
            bool active = pbs != null && tracer != null && tracer.Connected && tracer.HasSkyModel && Reflect();
            bool night = active && trial == 2 && Night(pbs);
            skip = active && !night; skipFrame = Time.frameCount;
            if (active) EnsureVolume();
            if (host != null && host.activeSelf != active) host.SetActive(active);
            // With the tracer's own sky: one update of HDRP's environment each time its sun turned a degree, for what HDRP still
            // lights with it (transparent surfaces, the fog's colour). The measuring switch asks for none.
            if (active && trial >= 2)
            {
                var pipeline = RenderPipelineManager.currentPipeline as HDRenderPipeline;
                var sun = pipeline != null ? pipeline.GetMainLight() : null;
                if (sun != null && (!haveRequestSun || Vector3.Angle(sun.transform.forward, lastRequestSun) >= 1f))
                {
                    pipeline.RequestSkyEnvironmentUpdate();
                    lastRequestSun = sun.transform.forward; haveRequestSun = true;
                }
            }
            else haveRequestSun = false;
            int state = !active ? 0 : trial == 1 ? 1 : night ? 2 : 3;
            if (state != saidState)
            {
                saidState = state;
                Session.Log.LogInfo(state == 0 ? (trial != 0 ? "Sky trial " + trial + ": HDRP's sky as before (" + (tracer != null && !tracer.HasSkyModel ? "the path tracer's native side has no TGPT_SetSkyModel" : pbs == null ? "no Physically Based Sky for the camera now, or the traced light is not on screen" : "HDRP's sky could not be read") + ")" : "Sky: HDRP's, as before") :
                    state == 1 ? "Sky trial 1 (measuring): HDRP's sky environment no longer updated, its sky and clouds left out; the path tracer draws the camera's misses from the sky its rays read" :
                    state == 2 ? "Sky trial 2 at night: the path tracer's own sky for its rays; HDRP draws its sky and clouds on the screen (the game's stars)" :
                    "Sky trial " + trial + ": the path tracer's own sky for its rays and the screen; HDRP's sky and clouds left out, its environment updated each degree its sun turns");
            }
            if (now >= nextCount)
            {
                if (nextCount > 0f) Session.Log.LogInfo("Sky: HDRP rendered its sky cube map " + cubes + " times and the sky into its ambient probes " + probes + " times in the last 10 s (" +
                    (Time.frameCount - frames0) + " frames; sky trial " + trial + (skip ? ", HDRP's sky passes left out" : "") + ")");
                cubes = probes = 0; frames0 = Time.frameCount; nextCount = now + 10f;
            }
        }

        // The game's night sky (its star layer) shows: its sky material's day-to-night blend (DayNightSystem's _SkyBlend: 0 by
        // day, the night's emission from above 0) or the Wyrdness.
        private static readonly int SkyBlend = Shader.PropertyToID("_SkyBlend"), SkyWyrdness = Shader.PropertyToID("_SkyWyrdness");
        private static bool Night(PhysicallyBasedSky pbs)
        {
            var m = pbs.renderingMode.value == PhysicallyBasedSky.RenderingMode.Material ? pbs.material.value : null;
            if (m == null) return false;
            return (m.HasProperty(SkyBlend) && m.GetFloat(SkyBlend) > 0.001f) || (m.HasProperty(SkyWyrdness) && m.GetFloat(SkyWyrdness) > 0.001f);
        }

        private static void Once(int key, string text) { if (saidOnce.ContainsKey(key)) return; saidOnce[key] = text; Session.Log.LogInfo(text); }
        private static Vector3 LightColor(Light light)
        {
            var c = light.color.linear * light.intensity;
            if (light.useColorTemperature) c *= Mathf.CorrelatedColorTemperatureToRGB(light.colorTemperature);
            return new Vector3(c.r, c.g, c.b);
        }
        private static Vector3 Rgb(Color c) => new Vector3(c.r, c.g, c.b);

        // The capture pass, every traced frame, before the frame is submitted (HdrpPasses.Capture).
        internal static void Send(HDCamera hd)
        {
            var tracer = Session.Tracer;
            if (tracer == null || !tracer.Connected || !tracer.HasSkyModel) return;
            int trial = Session.Settings != null ? Mathf.Clamp(Session.Settings.SkyTrial.Value, 0, 3) : 0;
            PhysicallyBasedSky pbs = null; CloudLayer clouds = null; VisualEnvironment environment = null;
            if (trial != 0 && hd != null && host != null && host.activeSelf && Reflect())
            {
                environment = hd.volumeStack.GetComponent<VisualEnvironment>();
                if (environment != null && environment.skyType.value == (int)SkyType.PhysicallyBased) pbs = hd.volumeStack.GetComponent<PhysicallyBasedSky>();
                if (environment != null && environment.cloudType.value == (int)CloudType.CloudLayer) clouds = hd.volumeStack.GetComponent<CloudLayer>();
            }
            if (pbs == null)
            {
                windTime = -1f;
                if (sentHdrp) return;
                record = new NativeSky { Size = 816, Abi = Tgpt.Abi, Mode = Tgpt.SkyHdrp };
                tracer.SetSkyModel(ref record); sentHdrp = true;
                return;
            }
            try { Fill(hd, pbs, clouds, trial); }
            catch (Exception e)
            {
                Once(1, "Sky trial: HDRP's sky could not be handed to the path tracer (" + e.Message + "); the sky stays HDRP's.");
                record = new NativeSky { Size = 816, Abi = Tgpt.Abi, Mode = Tgpt.SkyHdrp };
            }
            int result = tracer.SetSkyModel(ref record);
            if (result != 0) Once(2, "Sky trial: TGPT_SetSkyModel said " + result + ".");
            sentHdrp = record.Mode == Tgpt.SkyHdrp;
        }

        private static void Fill(HDCamera hd, PhysicallyBasedSky pbs, CloudLayer clouds, int trial)
        {
            var r = new NativeSky { Size = 816, Abi = Tgpt.Abi };
            r.Mode = trial == 1 ? Tgpt.SkyMeasure : Tgpt.SkyOwn;
            if (trial == 2 && Night(pbs)) r.Flags |= Tgpt.SkyHdrpPixels;
            // HDRP's planet for this camera (HDCamera.planet: its VisualEnvironment's, in metres).
            object planet = PlanetField.GetValue(hd);
            float radius = (float)planetRadius.GetValue(planet);
            Vector3 center = (Vector3)planetCenter.GetValue(planet);
            if (!(radius > 1e4f && radius < 1e9f)) throw new InvalidOperationException("HDRP's planet radius is " + radius);
            r.PlanetCenterX = center.x; r.PlanetCenterY = center.y; r.PlanetCenterZ = center.z; r.PlanetRadius = radius;
            // The atmosphere as HDRP's own methods work it out from the volume's parameters (PhysicallyBasedSky.Get*).
            Vector3 v = airExtinction(pbs); r.AirExtinctionR = v.x; r.AirExtinctionG = v.y; r.AirExtinctionB = v.z; r.AirScaleHeight = airScaleHeight(pbs);
            v = airScattering(pbs); r.AirScatteringR = v.x; r.AirScatteringG = v.y; r.AirScatteringB = v.z; r.AtmosphereDepth = maximumAltitude(pbs);
            v = aerosolScattering(pbs); r.AerosolScatteringR = v.x; r.AerosolScatteringG = v.y; r.AerosolScatteringB = v.z; r.AerosolExtinction = aerosolExtinction(pbs);
            v = ozoneExtinction(pbs); r.OzoneExtinctionR = v.x; r.OzoneExtinctionG = v.y; r.OzoneExtinctionB = v.z; r.AerosolScaleHeight = aerosolScaleHeight(pbs);
            Color c = pbs.groundTint.value; r.GroundR = c.r; r.GroundG = c.g; r.GroundB = c.b; r.AerosolAnisotropy = pbs.aerosolAnisotropy.value;
            c = pbs.horizonTint.value; r.HorizonR = c.r; r.HorizonG = c.g; r.HorizonB = c.b; r.OzoneStart = ozoneStart(pbs);
            c = pbs.zenithTint.value; r.ZenithR = c.r; r.ZenithG = c.g; r.ZenithB = c.b; r.OzoneWidth = ozoneWidth(pbs);
            r.ColorSaturation = pbs.colorSaturation.value; r.AlphaSaturation = pbs.alphaSaturation.value; r.AlphaMultiplier = pbs.alphaMultiplier.value;
            r.HorizonZenithShift = pbs.horizonZenithShift.value;
            r.Intensity = pbs.GetIntensityFromSettings();
            if (pbs.renderingMode.value != PhysicallyBasedSky.RenderingMode.Material && (pbs.spaceEmissionTexture.value != null || pbs.groundColorTexture.value != null || pbs.groundEmissionTexture.value != null))
                Once(3, "Sky trial: the Physically Based Sky's space or ground cube maps are not read by the path tracer yet (cube maps cannot be opened yet).");
            if (pbs.renderingMode.value == PhysicallyBasedSky.RenderingMode.Material)
                Once(4, "Sky trial: the sky's own shader graph ('" + (pbs.material.value != null ? pbs.material.value.shader.name : "none") + "': the game's stars, night sky and day colour) is not made by the path tracer yet (sky-1 step 4); its atmosphere, clouds, sun and moon are.");
            // The celestial bodies: every enabled directional light that interacts with the sky, those that light it first.
            var pipeline = RenderPipelineManager.currentPipeline as HDRenderPipeline;
            var lights = LightDatabaseInstance.GetValue(null) is object database ? DirectionalLights.GetValue(database) as List<HDAdditionalLightData> : null;
            var bodies = new NativeSkyBody[Tgpt.SkyMaxBodies]; int count = 0, found = 0;
            if (lights != null)
            {
                for (int pass = 0; pass < 2; ++pass)
                    foreach (var data in lights)
                    {
                        var light = data != null ? data.GetComponent<Light>() : null;
                        if (light == null || !light.enabled || !data.interactsWithSky) continue;
                        if ((light.intensity != 0f) != (pass == 0)) continue;
                        ++found;
                        if (count < bodies.Length) bodies[count++] = Body(data, light, lights);
                    }
            }
            if (found > Tgpt.SkyMaxBodies) Once(5, "Sky trial: " + found + " celestial bodies; the path tracer takes " + Tgpt.SkyMaxBodies + " (the game has two).");
            r.BodyCount = (uint)count;
            r.Body0 = bodies[0]; r.Body1 = bodies[1]; r.Body2 = bodies[2]; r.Body3 = bodies[3];
            // HDRP's sun (its main light: the game's shadow caster) bakes and lights the clouds.
            var sun = pipeline != null ? pipeline.GetMainLight() : null;
            Vector3 sunColor = Vector3.zero;
            if (sun != null)
            {
                var toSun = -sun.transform.forward; r.HdrpSunX = toSun.x; r.HdrpSunY = toSun.y; r.HdrpSunZ = toSun.z;
                var sunData = sun.GetComponent<HDAdditionalLightData>();
                sunColor = LightColor(sun) * (sunData != null ? sunData.lightDimmer : 1f);
            }
            if (clouds != null && clouds.opacity.value > 0f) FillClouds(ref r, hd, clouds, sun, sunColor);
            else windTime = -1f;
            record = r;
        }

        private static NativeSkyBody Body(HDAdditionalLightData data, Light light, List<HDAdditionalLightData> lights)
        {
            var b = new NativeSkyBody();
            var t = light.transform;
            var toward = -t.forward; b.DirectionX = toward.x; b.DirectionY = toward.y; b.DirectionZ = toward.z;
            float diameter = data.diameterMultiplerMode ? data.diameterMultiplier * data.angularDiameter : data.diameterOverride;
            float angularRadius = diameter * 0.5f * Mathf.Deg2Rad; b.AngularRadius = angularRadius;
            var color = LightColor(light); b.ColorR = color.x; b.ColorG = color.y; b.ColorB = color.z;
            b.Flags = light.intensity != 0f ? Tgpt.SkyBodyLights : 0u;
            b.Distance = data.distance;
            var right = t.right.normalized; var up = t.up.normalized;
            b.RightX = right.x; b.RightY = right.y; b.RightZ = right.z; b.UpX = up.x; b.UpY = up.y; b.UpZ = up.z;
            b.FlareSize = Mathf.Max(data.flareSize * Mathf.Deg2Rad, 5.960464478e-8f); b.FlareFalloff = data.flareFalloff;
            b.Earthshine = data.earthshine * 0.01f;
            b.SurfaceTexture = data.surfaceTexture != null ? TextureShare.Handle(data.surfaceTexture) : -1;
            Vector3 surface = Rgb(data.surfaceTint.linear), flare = Rgb(data.flareTint.linear) * data.flareMultiplier;
            if (data.celestialBodyShadingSource == HDAdditionalLightData.CelestialBodyShadingSource.Emission)
            {
                // Its disc's radiance: its light spread over the disc's solid angle (its flare likewise), as HDRP's sky shows it.
                float perSolidAngle = 1f / (2f * Mathf.PI * (1f - Mathf.Cos(angularRadius)));
                surface = Vector3.Scale(surface * perSolidAngle, color); flare = Vector3.Scale(flare * perSolidAngle, color);
            }
            else
            {
                // A moon: lit by its sun (the light named, or the brightest other directional light), or by a sun of its own.
                Vector3 sunColor, travel;
                if (data.celestialBodyShadingSource == HDAdditionalLightData.CelestialBodyShadingSource.Manual)
                {
                    var rotation = Quaternion.AngleAxis(data.moonPhaseRotation, t.forward);
                    var remap = Quaternion.FromToRotation(Vector3.right, t.forward);
                    float phase = data.moonPhase * 2f * Mathf.PI;
                    Color own = SunColorField != null ? (Color)SunColorField.GetValue(data) : Color.white;
                    float intensity = SunIntensityField != null ? (float)SunIntensityField.GetValue(data) : 130000f;
                    sunColor = Rgb(own) * intensity;
                    travel = rotation * remap * new Vector3(Mathf.Cos(phase), 0f, Mathf.Sin(phase));
                }
                else
                {
                    Light source = data.sunLightOverride;
                    if (source == null || source == light || source.type != LightType.Directional)
                    {
                        source = null; float most = 0f;
                        foreach (var other in lights)
                        {
                            var l = other != null ? other.GetComponent<Light>() : null;
                            if (l != null && l != light && l.intensity > most) { most = l.intensity; source = l; }
                        }
                    }
                    sunColor = source != null ? LightColor(source) : Vector3.zero;
                    travel = source != null ? source.transform.forward : Vector3.forward;
                }
                b.Flags |= Tgpt.SkyBodyReflects;
                b.SunTravelX = travel.x; b.SunTravelY = travel.y; b.SunTravelZ = travel.z;
                surface = Vector3.Scale(surface, sunColor); flare = Vector3.Scale(flare, sunColor);
            }
            b.SurfaceR = surface.x; b.SurfaceG = surface.y; b.SurfaceB = surface.z;
            b.FlareR = flare.x; b.FlareG = flare.y; b.FlareB = flare.z;
            return b;
        }

        private static void FillClouds(ref NativeSky r, HDCamera hd, CloudLayer clouds, Light sun, Vector3 sunColor)
        {
            bool upper = clouds.upperHemisphereOnly.value, two = clouds.layers.value == CloudMapMode.Double;
            r.Flags |= Tgpt.SkyClouds | (upper ? Tgpt.SkyCloudsUpper : 0u);
            r.CloudOpacity = clouds.opacity.value;
            int width = (int)clouds.resolution.value;
            r.CloudWidth = (uint)Mathf.Max(width, 0); r.CloudHeight = (uint)Mathf.Max(upper ? width / 2 : width, 0);
            // HDRP's baking hash (its sun's turn by whole degrees with the layers' maps and lighting): a new bake when it changes.
            int hash = 17;
            try { if (CloudBakingHash != null) hash = (int)CloudBakingHash.Invoke(clouds, new object[] { sun }); }
            catch { hash = 17; }
            if (hash == 17 && sun != null) { var e = sun.transform.rotation.eulerAngles; hash = ((int)e.x * 397 ^ (int)e.y) * 397 ^ (int)e.z; }
            r.CloudsHash = unchecked((uint)hash);
            // The wind: HDRP adds its move only while it draws the clouds (CloudLayerRenderer: speed in km/h times the frame's
            // time, in metres); while they are left out the move is kept here from where HDRP's stood, and HDRP, drawing them
            // again, adds the whole time it missed itself.
            float now = hd.time, dt = windTime >= 0f ? Mathf.Max(now - windTime, 0f) : 0f;
            windTime = now;
            r.Layer0 = Layer(clouds.layerA, 0, hd, sunColor, SkipHdrpSky, dt);
            r.Layer1 = two ? Layer(clouds.layerB, 1, hd, sunColor, SkipHdrpSky, dt) : default;
        }

        private static NativeSkyLayer Layer(CloudLayer.CloudMap map, int index, HDCamera hd, Vector3 sunColor, bool kept, float dt)
        {
            var l = new NativeSkyLayer { On = 1 };
            l.OpacityR = map.opacityR.value; l.OpacityG = map.opacityG.value; l.OpacityB = map.opacityB.value; l.OpacityA = map.opacityA.value;
            l.Rotation = map.rotation.value / 360f;
            l.Steps = map.lighting.value ? map.steps.value : 0;
            l.Sigma = map.thickness.value * 0.095f + 0.005f;
            l.Altitude = map.altitude.value;
            var light = Vector3.Scale(Rgb(map.tint.value * ColorUtils.ConvertEV100ToExposure(-map.exposure.value)), sunColor);
            l.LightR = light.x; l.LightG = light.y; l.LightB = light.z;
            l.Map = map.cloudMap.value != null ? TextureShare.Handle(map.cloudMap.value) : -1;
            float angle = Mathf.Deg2Rad * map.scrollOrientation.GetValue(hd);
            l.ScrollX = -Mathf.Cos(angle); l.ScrollZ = -Mathf.Sin(angle);
            var mode = map.distortionMode.value;
            l.Distortion = mode == CloudDistortionMode.Procedural ? 1u : mode == CloudDistortionMode.Flowmap ? 2u : 0u;
            l.Flowmap = mode == CloudDistortionMode.Flowmap && map.flowmap.value != null ? TextureShare.Handle(map.flowmap.value) : -1;
            float hdrpMove = (float)ScrollFactor.GetValue(map);
            if (kept && l.Distortion != 0u) windKept[index] += map.scrollSpeed.GetValue(hd) * dt * 0.277778f;
            else windKept[index] = 0f;
            l.Scrolled = hdrpMove + windKept[index];
            l.AmbientDimmer = map.ambientProbeDimmer.value;
            if (l.Map < 0) Once(10 + index, "Sky trial: the cloud layer's map '" + (map.cloudMap.value != null ? map.cloudMap.value.name : "none") + "' is not open to the path tracer yet; that layer draws no clouds.");
            return l;
        }
    }
}
