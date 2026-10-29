// SPDX-License-Identifier: MIT
// PT-108 and PT-104, ON TRIAL (docs/agents/rain-1.md; the hidden "Rain wetness", 0 unless set): the rain wets the traced world.
//
// When it rains the game makes what stands under the open sky wet with one pass over the screen (its ScreenSpaceWetness
// custom pass, after HDRP's G-buffer): it rewrites how rough each pixel is, and nothing else. Its PrecipitationController
// holds the moisture (it rises while it rains and dries afterwards) and turns the pass on and off; the pass's shader lays a
// noise picture of its own over the world. The tracer makes the same roughness at every hit, for every kind of ray
// (pt_common rain_roughness), so here the game's own state is read each frame, where the game leaves it, and handed over
// (TGPT_SetRain): the moisture as the pass would apply it (0 while the pass is off: the game is dry at once then), the
// shader variant's own number for a soaked surface, and the noise texture itself, opened by the tracer with no copy
// (TextureShare, as every texture a ray reads).
//
// The one thing of the game's pass that hangs on the screen is its second reading of the noise, at screen coordinates
// times 0.1: a blotch that stays on the glass as the camera turns. Nothing the tracer shows may hang on the screen, so the
// mean of the part of the picture the screen shows stands for it: measured here from the game's texture.
//
// NOT BUILT YET: the shelter. The game keeps what stands under a roof dry by a top-down height array around the hero
// (TopDownDepthTexturesLoadingManager). Until the tracer has it, every hit counts as under the open sky, indoors too, which
// is why the setting stays 0.
using System;
using System.Reflection;
using Awaken.TG.Graphics;
using Awaken.TG.Graphics.VFX;
using UnityEngine;
using UnityEngine.Rendering.HighDefinition;
namespace TGPathTracing
{
    internal static class RainWetness
    {
        private const BindingFlags Own = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance;
        private static readonly FieldInfo MoistureField = typeof(PrecipitationController).GetField("_moisture", Own);
        private static readonly FieldInfo PassField = typeof(PrecipitationController).GetField("_screenSpaceWetness", Own);
        private static readonly FieldInfo VolumeField = typeof(PrecipitationController).GetField("screenSpaceWetnessVolume", Own);
        private static readonly FieldInfo MaterialField = typeof(ScreenSpaceWetness).GetField("wetnessMaterial", Own);
        private static readonly FieldInfo ShelterField = typeof(ScreenSpaceWetness).GetField("_depthTexturesLoadingManager", Own);
        // The game's shader's own numbers (TG/Weather/Rain/ScreenSpace_Wetness_2, read from its compiled code): how smooth a
        // soaked surface is at the camera, and in its snow variant (the material's keyword).
        private const float Soaked = 0.9f, SoakedSnow = 0.33f;
        private const string SnowKeyword = "_SNOWMAP";
        // Its noise picture is the one texture the shader carries itself (a Shader Graph texture asset: the property's name
        // starts so and ends with the node's own id); by its asset's name if the material does not hand it out.
        private const string NoiseProperty = "_Texture2DAsset_", NoiseName = "Tex_Noise_RGB_Clouds01";
        // The part of the noise the game's second reading shows: screen coordinates times this.
        private const float ScreenPart = 0.1f;

        private static PrecipitationController controller;
        private static float nextLookup;
        private static Material material;
        private static Texture noise;
        private static int measuredId, measuredMip = -1, openedId;
        private static float screenNoise, nextOpen;
        private static bool failed, sent, saidNative, saidClosed;
        private static int said = -1;
        private static float saidMoisture = -1f;

        // Each frame the scene is captured (Session.Tick, outside HDRP's rendering): the game's controller, its pass's
        // material and noise found, the noise opened by the tracer, the screen's part of it measured.
        internal static void Tick(float now)
        {
            var settings = Session.Settings;
            if (settings == null || settings.RainWetness.Value != 1 || failed) return;
            try
            {
                if (MoistureField == null || PassField == null || VolumeField == null || MaterialField == null || ShelterField == null)
                    throw new MissingFieldException("the game's weather code is not as this build knows it");
                if (controller == null)
                {
                    if (now < nextLookup) return;
                    nextLookup = now + 2f;
                    controller = UnityEngine.Object.FindAnyObjectByType<PrecipitationController>();
                    material = null; noise = null;
                    if (controller == null) return;
                }
                var pass = PassField.GetValue(controller) as ScreenSpaceWetness;
                if (pass == null) return; // the controller has not started, or this place has no wetness pass
                if (material == null) { material = MaterialField.GetValue(pass) as Material; noise = null; }
                if (material == null) return;
                if (noise == null)
                {
                    string how;
                    noise = FindNoise(material, out how);
                    if (noise == null) throw new InvalidOperationException("the wetness shader's noise texture was not found on its material \"" + material.name + "\"");
                    Session.Log.LogInfo("Rain wetness on trial: the game's wetness pass draws with \"" + material.name + "\" (" + (material.shader != null ? material.shader.name : "no shader") +
                        "); its noise is \"" + noise.name + "\" " + noise.width + "x" + noise.height + ", " + how + ".");
                }
                // Opened by the tracer with no copy, and followed when the game's streaming makes it anew. Asked of the native
                // side first: a texture the tracer cannot open is a line in the log and a dry trial (asked again every 10 s),
                // never the pause TextureShare holds path tracing in for a material's texture it cannot open.
                if (openedId != noise.GetInstanceID())
                {
                    if (now < nextOpen) return;
                    nextOpen = now + 10f;
                    IntPtr native = noise.GetNativeTexturePtr();
                    var info = new NativeSharedTexture();
                    int number = native != IntPtr.Zero && Session.Tracer != null && Session.Tracer.Connected ? Session.Tracer.RegisterSharedTexture(native, out info) : -1;
                    if (number < 0)
                    {
                        if (!saidClosed) Session.Log.LogWarning("Rain wetness on trial: the path tracer cannot open the game's noise texture \"" + noise.name + "\" (answer " + number + "): dry until it can.");
                        saidClosed = true;
                        return;
                    }
                    TextureShare.Register(noise, native);
                    openedId = noise.GetInstanceID();
                }
                TextureShare.Handle(noise); TextureShare.WatchStandalone(noise);
                int mip = noise is Texture2D streamed && streamed.streamingMipmaps ? streamed.loadedMipmapLevel : 0;
                if (measuredId != noise.GetInstanceID() || measuredMip != mip)
                {
                    screenNoise = ScreenPartMean(noise);
                    measuredId = noise.GetInstanceID(); measuredMip = mip;
                    Session.Log.LogInfo("Rain wetness on trial: the part of the noise the game lays over the screen (a tenth of it each way) reads " +
                        screenNoise.ToString("0.000") + " on average (the texture's level " + mip + " loaded): that stands for it at every hit.");
                }
            }
            catch (Exception ex) { Fail(ex); }
        }

        // Just before the frame is submitted (HdrpPasses): the game's moisture as its pass applies it this frame.
        internal static void Send(NativeTracer tracer, Settings settings)
        {
            if (tracer == null || !tracer.Connected) return;
            bool wanted = settings.RainWetness.Value == 1;
            if (!wanted && !sent) return; // never asked for: the native side's record is dry as it is
            if (!tracer.HasRain)
            {
                if (wanted && !saidNative) { saidNative = true; Session.Log.LogWarning("Rain wetness on trial: this native side has no TGPT_SetRain (an older build): nothing changes."); }
                return;
            }
            var rain = new NativeRain { Size = 64, Abi = Tgpt.Abi, Noise = -1 };
            // 0 off; 1 the game's controller, pass, material or noise not found or not opened (yet); 2 found, the pass off or no
            // moisture: dry; 3 wet
            int state = 0;
            bool global = false;
            string shelterText = "";
            if (wanted && !failed)
            {
                try
                {
                    state = 1;
                    var pass = controller != null ? PassField.GetValue(controller) as ScreenSpaceWetness : null;
                    if (pass != null && material != null && noise != null && openedId == noise.GetInstanceID())
                    {
                        state = 2;
                        // What the game's pass needs to run at all (its own Execute): on, its volume alive, its shelter array there.
                        var volume = VolumeField.GetValue(controller) as CustomPassVolume;
                        var shelter = ShelterField.GetValue(pass) as TopDownDepthTexturesLoadingManager;
                        bool on = pass.enabled && volume != null && volume.isActiveAndEnabled && shelter != null && shelter.IsInitialized;
                        global = volume != null && volume.isGlobal;
                        float moisture = on ? (float)MoistureField.GetValue(controller) : 0f;
                        if (moisture > 0f)
                        {
                            state = 3;
                            rain.Moisture = Mathf.Clamp01(moisture);
                            rain.Soaked = material.IsKeywordEnabled(SnowKeyword) ? SoakedSnow : Soaked;
                            rain.ScreenNoise = screenNoise;
                            rain.Noise = TextureShare.Handle(noise);
                            if (said != 3) shelterText = ShelterText(shelter);
                        }
                    }
                }
                catch (Exception ex) { Fail(ex); rain = new NativeRain { Size = 64, Abi = Tgpt.Abi, Noise = -1 }; state = 0; }
            }
            int result = tracer.SetRain(ref rain);
            sent = wanted;
            if (said == state && (state != 3 || Mathf.Abs(rain.Moisture - saidMoisture) < 0.1f)) return;
            said = state; saidMoisture = rain.Moisture;
            Session.Log.LogInfo("Rain wetness on trial: " + (state == 0 ? "off (the path tracer knows nothing of the rain)" :
                state == 1 ? "the game's weather, its wetness pass or its noise is not found or not opened here (yet): dry" :
                state == 2 ? "the game's wetness pass is off or its moisture is 0: dry, as the game draws it" :
                "wet. Moisture " + rain.Moisture.ToString("0.00") + ", a soaked surface's smoothness at the camera " + rain.Soaked.ToString("0.00") +
                (rain.Soaked == SoakedSnow ? " (the snow variant)" : "") + ", noise handle " + rain.Noise + (rain.Noise < 0 ? " (not open to the tracer: dry until it is)" : "") +
                ", the game's wetness volume is " + (global ? "global" : "local (taken as covering the camera)") +
                "; no shelter yet: everything counts as under the open sky" + shelterText) + (result != 0 ? " (the native side answered " + result + ")" : "") + ".");
        }

        // The game's shelter array as it is now, for the log alone (the tracer does not read it yet): what a choice of how
        // to bring the shelter into the tracer needs to know of it.
        private static readonly FieldInfo ChunkUnitsField = typeof(TopDownDepthTexturesLoadingManager).GetField("_chunkTextureSizeInUnits", Own);
        private static string ShelterText(TopDownDepthTexturesLoadingManager shelter)
        {
            try
            {
                var array = shelter.DepthTexturesArray;
                var layers = shelter.DepthTexturesLayers;
                object units = ChunkUnitsField != null ? ChunkUnitsField.GetValue(shelter) : null;
                return " (the game's own shelter array, not read by the tracer: " + (array != null ? array.width + "x" + array.height + " texels, " + array.volumeDepth + " layers, " + array.graphicsFormat : "none") +
                    "; a chunk is " + (units != null ? units + " m" : "? m") + " a side; dry from " + shelter.MaxHeightDiff.ToString("0.##") + " m under a roof; the map's top at " +
                    shelter.CameraWorldPosY.ToString("0.#") + " m, " + shelter.FarPlane.ToString("0.#") + " m deep; chunks loaded in layers " +
                    layers.x.ToString("0") + ", " + layers.y.ToString("0") + ", " + layers.z.ToString("0") + ", " + layers.w.ToString("0") + " (-1: none))";
            }
            catch (Exception ex) { return " (the game's shelter array could not be described: " + ex.Message + ")"; }
        }

        private static void Fail(Exception ex)
        {
            failed = true;
            Session.Log.LogWarning("Rain wetness on trial: stopped, the rain wets nothing (" + (ex.InnerException ?? ex).Message + ").");
        }

        private static Texture FindNoise(Material m, out string how)
        {
            how = "handed out by the material";
            foreach (var name in m.GetTexturePropertyNames())
            {
                if (!name.StartsWith(NoiseProperty, StringComparison.Ordinal)) continue;
                var t = m.GetTexture(name);
                if (t != null) return t;
            }
            how = "found by its name among the game's loaded textures (the material does not hand it out)";
            foreach (var t in Resources.FindObjectsOfTypeAll<Texture2D>()) if (t != null && t.name == NoiseName) return t;
            return null;
        }

        // The mean of the noise's red over the part the game's second reading shows (texture coordinates 0 to ScreenPart each
        // way), read with the texture's own filter and wrap: drawn small and read back, once a texture and loaded level.
        private static float ScreenPartMean(Texture texture)
        {
            const int size = 32;
            var target = RenderTexture.GetTemporary(size, size, 0, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Linear);
            var before = RenderTexture.active;
            Texture2D read = null;
            try
            {
                Graphics.Blit(texture, target, new Vector2(ScreenPart, ScreenPart), Vector2.zero);
                RenderTexture.active = target;
                read = new Texture2D(size, size, TextureFormat.RGBA32, false, true);
                read.ReadPixels(new Rect(0, 0, size, size), 0, 0);
                var pixels = read.GetPixels32();
                double red = 0;
                foreach (var c in pixels) red += c.r;
                return (float)(red / (255.0 * pixels.Length));
            }
            finally
            {
                RenderTexture.active = before;
                RenderTexture.ReleaseTemporary(target);
                if (read != null) UnityEngine.Object.Destroy(read);
            }
        }
    }
}
