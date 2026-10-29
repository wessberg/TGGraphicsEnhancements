// SPDX-License-Identifier: MIT
// The game's wind on its grass, leaves and trunks (PT-63, the owner: "of course our shadows and reflections should move
// alongside the grass and leaves swaying in the wind. Otherwise what's the point of realtime ray tracing"). Three of the
// game's shaders move their vertices in the wind (TG/Vegetation/Vegetation_Grass, Vegetation_Leaves and Vegetation_Trunk;
// their maths, read from the compiled shaders, is in docs/agents/world.md 4.1). Each such material's values are read
// here when the material is first traced, its noise map is read once from the game's own texture at its own size, and
// every frame the wind the game set (its WindController's globals) goes to the tracer, which keeps every instance with
// such a material in the pose HDRP draws it (TGPT_SetPlantWind, pt_sway.hlsl). Until 2026-10-07 they were traced standing
// still, and rays were told not to trust them near their start (TGPT_MATERIAL_SWAYS).
using System;
using System.Collections.Generic;
using UnityEngine;
namespace TGPathTracing
{
    internal static class PlantWind
    {
        internal const string GrassShader = "TG/Vegetation/Vegetation_Grass", LeavesShader = "TG/Vegetation/Vegetation_Leaves", TrunkShader = "TG/Vegetation/Vegetation_Trunk";
        // The grass shader's own gust noise (a texture the shader carries, hidden from the material's inspector): the red
        // channel. Leaves and trunks read the alpha of the material's _WindMaskMap.
        internal const string GrassNoise = "_SubSGVegetationWindGrass_13b0f6e2d7d04f7ebbcc5d63f6f2e39a_NoiseTexture2D_1371333998_Texture2D";
        internal const string MaskMap = "_WindMaskMap";
        internal const uint Grass = 1, Leaves = 2;          // TGPT_PLANT_GRASS, TGPT_PLANT_LEAVES
        internal const uint Bend = 1;                        // TGPT_PLANT_BEND
        internal const uint ClampU = 1, ClampV = 2, Point = 4; // TGPT_NOISE_*
        private const int MaxWinds = 4096;                   // TGPT_MAX_PLANT_WIND
        private static readonly List<NativePlantWind> winds = new List<NativePlantWind>();
        private static NativePlantWind[] windArray = new NativePlantWind[0];
        // A noise map's key by its texture and channel (the key is the texture's instance id and the channel, never 0).
        private static readonly Dictionary<long, uint> noiseKeys = new Dictionary<long, uint>();
        private static uint nextNoiseKey = 1;
        private static readonly HashSet<string> unread = new HashSet<string>();
        private static bool fullLogged;
        internal static int Count => winds.Count;

        internal static void Reset() { winds.Clear(); windArray = new NativePlantWind[0]; noiseKeys.Clear(); nextNoiseKey = 1; }

        // Whether a shader moves its vertices in the game's vegetation wind, and the tracer poses it.
        internal static bool Posed(string shaderName) => shaderName == GrassShader || shaderName == LeavesShader || shaderName == TrunkShader;

        // A material just given a traced index: its wind, if its shader has one the tracer poses.
        internal static void Add(Material m, string shaderName, uint index)
        {
            if (!Posed(shaderName))
            {
                // Another of the game's vegetation shaders: not read, so traced still. Named once, for the list of shaders left.
                if (shaderName.StartsWith("TG/Vegetation/", StringComparison.OrdinalIgnoreCase) && shaderName.IndexOf("Billboard", StringComparison.OrdinalIgnoreCase) < 0 && unread.Add(shaderName))
                    Session.Log.LogInfo("Plants in the wind: shader \"" + shaderName + "\" (material \"" + m.name + "\") is not one whose wind the tracer has read: traced where it stands still");
                return;
            }
            if (winds.Count >= MaxWinds)
            {
                if (!fullLogged) { fullLogged = true; Session.Log.LogWarning("Plants in the wind: more than " + MaxWinds + " materials sway; the rest are traced standing still"); }
                return;
            }
            var w = new NativePlantWind { Material = index };
            Texture noise = null; int channel = 0;
            if (shaderName == GrassShader)
            {
                w.Kind = Grass;
                w.Strength = m.HasProperty("_Wind_Strength") ? m.GetFloat("_Wind_Strength") : 0f;
                noise = m.HasProperty(GrassNoise) ? m.GetTexture(GrassNoise) : null;
            }
            else
            {
                // Leaves and trunks bend the same way; a trunk's height is its _MaskHeight, with no pivot offset, and its fade
                // is called _WindDistanceFade.
                bool trunk = shaderName == TrunkShader;
                w.Kind = Leaves;
                w.Strength = m.HasProperty("_WindBendStrength") ? m.GetFloat("_WindBendStrength") : 0f;
                w.BendHeight = trunk ? (m.HasProperty("_MaskHeight") ? m.GetFloat("_MaskHeight") : 0f) : (m.HasProperty("_WindBendHeight") ? m.GetFloat("_WindBendHeight") : 0f);
                w.PivotOffsetY = !trunk && m.HasProperty("_WindBendPivotOffset") ? m.GetVector("_WindBendPivotOffset").y : 0f;
                string fadeName = trunk ? "_WindDistanceFade" : "_WindCameraFade";
                Vector4 fade = m.HasProperty(fadeName) ? m.GetVector(fadeName) : new Vector4(0f, 150f, 0f, 0f);
                w.FadeX = fade.x; w.FadeY = fade.y;
                w.Flags = m.HasProperty("_WindBend") && m.GetFloat("_WindBend") != 0f ? Bend : 0u;
                noise = m.HasProperty(MaskMap) ? m.GetTexture(MaskMap) : null;
                channel = 3;
            }
            string map = "no noise map (read as 1)";
            if (noise != null)
            {
                try { w.Noise = NoiseKey(noise, channel, out map); }
                catch (Exception ex) { map = "its noise map \"" + noise.name + "\" could not be read (" + ex.Message + "): read as 1"; }
            }
            winds.Add(w);
            windArray = winds.ToArray();
            if (winds.Count <= 60)
                Session.Log.LogInfo("Plants in the wind: \"" + m.name + "\" (material " + index + ", " + shaderName + "): " +
                    (w.Kind == Grass ? "strength " + w.Strength.ToString("0.###")
                        : "bend " + ((w.Flags & Bend) != 0 ? "on" : "off") + ", strength " + w.Strength.ToString("0.###") + ", height " + w.BendHeight.ToString("0.###") +
                          ", pivot offset " + w.PivotOffsetY.ToString("0.###") + ", fade " + w.FadeX.ToString("0.#") + " to " + w.FadeY.ToString("0.#") + " m") +
                    ", " + map + ": traced in the pose HDRP draws it");
        }

        // A texture's channel as the wind's noise map: read once at the texture's own size and handed to the tracer.
        private static uint NoiseKey(Texture texture, int channel, out string what)
        {
            long id = ((long)texture.GetInstanceID() << 2) | (long)(uint)channel;
            if (noiseKeys.TryGetValue(id, out uint key)) { what = "noise map \"" + texture.name + "\""; return key; }
            int w = Mathf.Min(texture.width, 4096), h = Mathf.Min(texture.height, 4096);
            var bytes = new byte[w * h];
            // Through a render target of the same size (the game's textures are not readable from scripts): each texel as it
            // is stored, with no color conversion (a linear target; alpha never has one).
            var target = RenderTexture.GetTemporary(w, h, 0, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Linear);
            var previous = RenderTexture.active;
            Texture2D copy = null;
            try
            {
                Graphics.Blit(texture, target);
                RenderTexture.active = target;
                copy = new Texture2D(w, h, TextureFormat.RGBA32, false, true);
                copy.ReadPixels(new Rect(0, 0, w, h), 0, 0, false);
                var pixels = copy.GetPixels32();
                for (int i = 0; i < bytes.Length && i < pixels.Length; i++) bytes[i] = channel == 3 ? pixels[i].a : channel == 1 ? pixels[i].g : channel == 2 ? pixels[i].b : pixels[i].r;
            }
            finally
            {
                RenderTexture.active = previous;
                RenderTexture.ReleaseTemporary(target);
                if (copy != null) UnityEngine.Object.Destroy(copy);
            }
            uint flags = (texture.wrapModeU == TextureWrapMode.Clamp ? ClampU : 0u) | (texture.wrapModeV == TextureWrapMode.Clamp ? ClampV : 0u) |
                         (texture.filterMode == FilterMode.Point ? Point : 0u);
            key = nextNoiseKey++;
            var tracer = Session.Tracer;
            int result = tracer != null && tracer.Connected ? tracer.SetWindNoise(key, (uint)w, (uint)h, flags, bytes) : -100;
            int low = 255, high = 0;
            foreach (byte b in bytes) { if (b < low) low = b; if (b > high) high = b; }
            what = "noise map \"" + texture.name + "\" " + w + "x" + h + " (" + (channel == 3 ? "alpha" : "red") + " " + low + " to " + high + " of 255" +
                (texture.wrapModeU == TextureWrapMode.Clamp || texture.wrapModeV == TextureWrapMode.Clamp ? ", clamped" : "") + (texture.filterMode == FilterMode.Point ? ", point" : "") +
                (result != 0 ? "; the tracer refused it (" + result + ")" : "") + ")";
            if (result != 0) return 0;
            noiseKeys[id] = key;
            return key;
        }

        // This frame's wind: the shader time (HDRP's _TimeParameters.x is Time.time), the game's wind speed and direction (its
        // WindController sets them every frame) and the camera's forward direction (leaves' bend fades along it).
        internal static void Send(NativeTracer tracer)
        {
            if (tracer == null || !tracer.Connected) return;
            var cam = Camera.main;
            var forward = cam != null ? cam.transform.forward : Vector3.forward;
            var frame = new NativePlantWindFrame
            {
                Time = Time.time, Speed = Shader.GetGlobalFloat("_VegetationWindSpeed"), Direction = Shader.GetGlobalFloat("_VegetationWindDirection"),
                ForwardX = forward.x, ForwardY = forward.y, ForwardZ = forward.z
            };
            tracer.SetPlantWind(ref frame, windArray, (uint)windArray.Length);
        }
    }
}
