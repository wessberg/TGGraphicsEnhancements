// SPDX-License-Identifier: MIT
// Caustics (the Caustics row, TGPT_SetWaterCaustics): the sun's and the lamps' light traced onto HDRP's water as its waves shape
// it, reflected and refracted there and traced on (pt_caustics.hlsl): the tracer needs the water and its real waves.
//   - At each scene scan (Capture), the up to four water surfaces nearest the camera keep or take a slot, and their geometry goes
//     into the traced scene as water (TGPT_INSTANCE_WATER, which only the tracer's water query meets): a custom surface's own
//     meshes, the quad HDRP draws for the others, and for the open sea a large quad at its height under the camera. With its
//     extent and its highest and lowest points.
//   - Every frame HDRP fills each surface's constants (WaterSystem.UpdatePerSurfaceConstantBuffer, a Harmony finalizer here):
//     each band's scale, the current's offset and amplitude (_BandN_ScaleOffset_AmplitudeMultiplier), its fade with the distance
//     from the camera (_BandN_Fade), the waves' largest move. Those numbers, as HDRP set them, and its wave buffers (the
//     simulation's displacement and additional data arrays) go to the tracer (Tick), which copies the buffers every frame.
//   - WaterSurfaceRendering stops the gameplay camera's draw and lighting of the water that has a slot (the tracer draws it:
//     its waves as triangles every ray meets, scene_water.cpp), so HDRP's surface caustics and underwater fog variant are
//     absent there. Water past the slots keeps HDRP's own draw until the slots have no count (docs/agents/water-4.md): it
//     must not disappear.
// What the tracer does not follow yet (the F8 report names it per slot): HDRP's local currents (a current map bends the waves'
// direction over the surface), its water mask (a texture that damps the bands) and its deformers. A slot with them gets HDRP's
// waves without them.
using System;
using System.Collections.Generic;
using System.Reflection;
using HarmonyLib;
using UnityEngine;
using UnityEngine.Rendering.HighDefinition;
namespace TGPathTracing
{
    internal static class WaterCaustics
    {
        private static readonly WaterSurface[] slots = new WaterSurface[Tgpt.MaxWater];
        // Each slot's wave buffers and their native textures (GetNativeTexturePtr waits for the render thread: read when they change).
        private static readonly Texture[] displacements = new Texture[Tgpt.MaxWater], gradients = new Texture[Tgpt.MaxWater];
        private static readonly IntPtr[] displacementPointers = new IntPtr[Tgpt.MaxWater], gradientPointers = new IntPtr[Tgpt.MaxWater];
        private static readonly NativeWaterCaustics[] sent = new NativeWaterCaustics[Tgpt.MaxWater], next = new NativeWaterCaustics[Tgpt.MaxWater];
        private static int sentCount = -1;
        private static readonly int[] meshes = new int[Tgpt.MaxWater];
        // Each slot's water's highest and lowest points and its extent in the world, from its instances at the last scan.
        private static readonly float[] tops = new float[Tgpt.MaxWater], bottoms = new float[Tgpt.MaxWater];
        private static readonly Vector4[] extents = new Vector4[Tgpt.MaxWater];
        // HDRP's numbers for each slot's waves this frame (from its constant buffer), and the frame they were read in.
        private struct Waves { public Vector4 B0, B1, B2; public Vector2 F0, F1, F2; public float MaxMove; public int Frame; public Vector4 Foam, Mask, Flow; }
        private static readonly Waves[] waves = new Waves[Tgpt.MaxWater];
        private static Texture foamTexture;
        private static Mesh quad;
        private static bool overLogged;
        // Whether a scene scan found more water than the slots hold (those surfaces stay HDRP's to draw).
        internal static bool AnyUntraced { get; private set; }
        // Whether the tracer draws this surface: it has a slot and its water is in the traced scene.
        internal static bool Traced(WaterSurface s)
        {
            if (s == null) return false;
            for (int i = 0; i < Tgpt.MaxWater; i++) if (slots[i] == s) return meshes[i] > 0;
            return false;
        }
        // The map's water while the player is in an interior (PT-114, the rule since 2026-10-12). The game's interiors
        // float in the sky above the map they belong to (a tavern 470 m over the sea), and the map stays loaded: its
        // water took the slots, 762 pieces of the sea were posed every frame and the caustics' grid was laid over it
        // (4.2 ms a frame), for water no ray from the room can meet; the tavern's frame went from 49.6 to 43.7 ms with
        // the same picture to the decimal (run 20261012-085406). Such water is in no slot, and HDRP does not draw it
        // either (WaterSurfaceRendering): as rays there see no sky (SceneLights.Interior). An interior's own water (a
        // surface of an "Interior" scene: a bath, a well) is never one of these, nor a dungeon's (a scene of its own).
        private static readonly HashSet<int> outside = new HashSet<int>();
        private static bool outsideLogged;
        internal static bool OutsideAnInterior(WaterSurface s) => s != null && outside.Contains(s.GetInstanceID());
        private static bool LeftOutside(WaterSurface s) =>
            SceneLights.InteriorLoaded && !s.gameObject.scene.name.StartsWith("Interior", StringComparison.OrdinalIgnoreCase);
        private static readonly HashSet<int> wavesLogged = new HashSet<int>(), numbersLogged = new HashSet<int>();
        // Once a surface: what HDRP's simulation writes into the third and fourth numbers of its additional data buffer,
        // read back and counted. The tracer's foam reads the third as each band's squeeze (its Jacobian: 4 at rest, less
        // where the waves press the surface together) and adds the bands' up against the surface's threshold
        // (pt_water_foam.hlsli). Nobody had looked at the numbers themselves (PT-81): where the tracer's foam starts was set
        // from one picture of the game's sea. This line is what settles it: each band's lowest, 1 %, median, 99 % and
        // highest; their sum over points of the sea (each band's nearest texel: the tracer reads it smoothed, a little
        // narrower); and how much of the sea HDRP's own measure (threshold less sum) puts over 0, over 0.45 (where the
        // tracer's foam starts) and over 1 (HDRP's full foam).
        private static void LogSqueeze(string name, Texture gradient, Waves v, int bands)
        {
            if (gradient == null || bands < 1) return;
            try
            {
                int width = gradient.width, height = gradient.height;
                Vector4[] band = { v.B0, v.B1, v.B2 };
                float threshold = v.Foam.x;
                UnityEngine.Rendering.AsyncGPUReadback.Request(gradient, 0, 0, width, 0, height, 0, bands, TextureFormat.RGBAFloat, request =>
                {
                    try
                    {
                        if (request.hasError) { Session.Log.LogInfo("Water: \"" + name + "\" squeeze: the wave buffer could not be read back"); return; }
                        var c = System.Globalization.CultureInfo.InvariantCulture;
                        Func<float, string> f = x => x.ToString("0.###", c);
                        var third = new float[bands][];
                        string text = "Water: \"" + name + "\" squeeze, the wave buffer's third number a band (the tracer's foam reads it as the Jacobian, 4 at rest; lowest / 1 % / median / 99 % / highest): ";
                        for (int b = 0; b < bands; b++)
                        {
                            var data = request.GetData<Vector4>(b);
                            int n = data.Length;
                            var z = new float[n]; var w = new float[n];
                            for (int i = 0; i < n; i++) { z[i] = data[i].z; w[i] = data[i].w; }
                            third[b] = (float[])z.Clone();
                            Array.Sort(z); Array.Sort(w);
                            text += (b > 0 ? "; " : "") + "band " + b + " " + f(z[0]) + " / " + f(z[n / 100]) + " / " + f(z[n / 2]) + " / " + f(z[n - 1 - n / 100]) + " / " + f(z[n - 1]) +
                                " (its fourth number " + f(w[0]) + " / " + f(w[n / 2]) + " / " + f(w[n - 1]) + ")";
                        }
                        // The bands' sum at points of the sea, as the tracer's foam lays the bands over the water (a point's
                        // place times the band's scale, less its offset).
                        var random = new System.Random(7);
                        const int points = 20000;
                        var sum = new float[points];
                        for (int k = 0; k < points; k++)
                        {
                            float x = (float)(random.NextDouble() * 4000.0 - 2000.0), y = (float)(random.NextDouble() * 4000.0 - 2000.0);
                            float total = 0f;
                            for (int b = 0; b < bands; b++)
                            {
                                int ix = Mathf.Min((int)(Mathf.Repeat(x * band[b].x - band[b].y, 1f) * width), width - 1);
                                int iy = Mathf.Min((int)(Mathf.Repeat(y * band[b].x - band[b].z, 1f) * height), height - 1);
                                total += third[b][iy * width + ix];
                            }
                            sum[k] = total;
                        }
                        Array.Sort(sum);
                        Func<double, string> at = share => f(sum[Mathf.Clamp((int)(share * points), 0, points - 1)]);
                        Func<float, string> over = level => { int count = 0; foreach (var total in sum) if (threshold - total > level) count++; return (100.0 * count / points).ToString("0.##", c); };
                        text += "; the bands' sum over " + points + " points (0.1 % / 1 % / 5 % / 25 % / median / 75 % / 99 %): " + at(0.001) + " / " + at(0.01) + " / " + at(0.05) + " / " +
                            at(0.25) + " / " + at(0.5) + " / " + at(0.75) + " / " + at(0.99) + "; the surface's threshold " + f(threshold) + ": HDRP's measure (threshold less sum) is over 0 at " +
                            over(0f) + " % of the points, over 0.45 (where the tracer's foam starts) at " + over(0.45f) + " %, over 1 (HDRP's full foam) at " + over(1f) + " %";
                        Session.Log.LogInfo(text);
                    }
                    catch (Exception e) { Session.Log.LogInfo("Water: \"" + name + "\" squeeze not counted (" + e.Message + ")"); }
                });
            }
            catch (Exception e) { Session.Log.LogInfo("Water: \"" + name + "\" squeeze not read (" + e.Message + ")"); }
        }
        // A surface's numbers in one log line: what the game's level sets on it and what HDRP worked out of that for this
        // frame, as the tracer gets them. Sizes in metres, colours as the surface holds them.
        private static string Numbers(WaterSurface s, Waves v, NativeWaterCaustics d)
        {
            var c = System.Globalization.CultureInfo.InvariantCulture;
            Func<float, string> f = x => x.ToString("0.###", c);
            Func<float, string> size = scale => scale > 0f ? (1f / scale).ToString("0.##", c) : "?";
            Func<Color, string> rgb = x => f(x.r) + " / " + f(x.g) + " / " + f(x.b);
            int bands = Bands(s);
            string text = "Water: \"" + s.name + "\" numbers: " + s.surfaceType + ", " + s.geometryType + "; " + bands + " bands of " +
                size(v.B0.x) + (bands > 1 ? " / " + size(v.B1.x) : "") + (bands > 2 ? " / " + size(v.B2.x) : "") + " m, amplitudes " +
                f(v.B0.w) + (bands > 1 ? " / " + f(v.B1.w) : "") + (bands > 2 ? " / " + f(v.B2.w) : "") + ", fades (a, b) " +
                f(v.F0.x * 1000f) + "e-3, " + f(v.F0.y) + (bands > 1 ? " / " + f(v.F1.x * 1000f) + "e-3, " + f(v.F1.y) : "") +
                (bands > 2 ? " / " + f(v.F2.x * 1000f) + "e-3, " + f(v.F2.y) : "") + "; the waves move up to " + f(v.MaxMove) + " m";
            text += "; foam " + (s.foam ? "on" : "off") + ": the surface's amount " + f(s.simulationFoamAmount) + ", HDRP's threshold " + f(d.FoamAmount) +
                " (4 a band at rest), intensity " + f(d.FoamIntensity) + ", tiling " + f(d.FoamTiling) + ", smoothness " + f(s.foamSmoothness) +
                ", colour " + rgb(s.foamColor) + ", its texture " + (d.FoamTexture >= 0 ? "read by the tracer" : "NOT shared") +
                ", its mask " + (d.FoamMaskTexture >= 0 ? "read by the tracer" : "none");
            text += "; scattering colour " + rgb(s.scatteringColor) + ", refraction colour " + rgb(s.refractionColor) + ", absorption distance " +
                f(s.absorptionDistance) + " m (so it takes " + f(d.ExtinctionR) + " / " + f(d.ExtinctionG) + " / " + f(d.ExtinctionB) + " a metre)";
            text += "; smoothness " + f(s.startSmoothness) + " near, " + f(s.endSmoothness) + " far (from " + f(s.smoothnessFadeStart) + " m over " +
                f(s.smoothnessFadeDistance) + " m: the tracer's water has it)";
            text += "; HDRP's own scattering terms (not the tracer's): ambient " + f(s.ambientScattering) + ", height " + f(s.heightScattering) +
                ", displacement " + f(s.displacementScattering) + ", tips " + f(s.directLightTipScattering) + ", body " + f(s.directLightBodyScattering);
            return text;
        }
        private static FieldInfo constantsField, simulationField, surfaceIndexField;
        private static PropertyInfo bandsProperty;
        private static readonly Dictionary<string, FieldInfo> constantFields = new Dictionary<string, FieldInfo>();

        internal static void Reset()
        {
            for (int i = 0; i < Tgpt.MaxWater; i++)
            {
                slots[i] = null; displacements[i] = gradients[i] = null; displacementPointers[i] = gradientPointers[i] = IntPtr.Zero; meshes[i] = 0;
                tops[i] = float.NegativeInfinity; bottoms[i] = float.PositiveInfinity; waves[i] = default;
            }
            sentCount = -1;
        }

        private static bool IsCustomMesh(WaterSurface s) => s.geometryType == WaterGeometryType.Custom && s.meshRenderers != null && s.meshRenderers.Count > 0;
        private static bool IsInfinite(WaterSurface s) => s.surfaceType == WaterSurfaceType.OceanSeaLake && s.geometryType == WaterGeometryType.Infinite;
        // How far the camera is from a surface's water: the sea is everywhere; a quad by its half diagonal round its centre.
        private static float Distance(WaterSurface s, Vector3 camera)
        {
            if (IsInfinite(s)) return 0f;
            if (IsCustomMesh(s))
            {
                float best = float.MaxValue;
                foreach (var r in s.meshRenderers) if (r != null) best = Mathf.Min(best, r.bounds.SqrDistance(camera));
                return best < float.MaxValue ? Mathf.Sqrt(best) : float.MaxValue;
            }
            var t = s.transform; var scale = t.lossyScale;
            return Mathf.Max(0f, Vector3.Distance(camera, t.position) - 0.5f * new Vector2(scale.x, scale.z).magnitude);
        }
        // The highest and lowest world y and the x, z extent of a mesh's bounds under a matrix (its eight corners).
        private static void Span(in Matrix4x4 world, Bounds b, int slot)
        {
            for (int c = 0; c < 8; c++)
            {
                var corner = new Vector3((c & 1) != 0 ? b.max.x : b.min.x, (c & 2) != 0 ? b.max.y : b.min.y, (c & 4) != 0 ? b.max.z : b.min.z);
                var p = world.MultiplyPoint3x4(corner);
                tops[slot] = Mathf.Max(tops[slot], p.y); bottoms[slot] = Mathf.Min(bottoms[slot], p.y);
                var e = extents[slot];
                extents[slot] = new Vector4(Mathf.Min(e.x, p.x), Mathf.Min(e.y, p.z), Mathf.Max(e.z, p.x), Mathf.Max(e.w, p.z));
            }
        }
        // A unit quad on XZ facing up, as HDRP's water quads are drawn (centred on the surface, its scale's x and z the size).
        private static Mesh Quad()
        {
            if (quad != null) return quad;
            quad = new Mesh { name = "TG Path Tracing water quad", hideFlags = HideFlags.HideAndDontSave };
            quad.vertices = new[] { new Vector3(-0.5f, 0f, -0.5f), new Vector3(0.5f, 0f, -0.5f), new Vector3(0.5f, 0f, 0.5f), new Vector3(-0.5f, 0f, 0.5f) };
            quad.normals = new[] { Vector3.up, Vector3.up, Vector3.up, Vector3.up };
            quad.triangles = new[] { 0, 2, 1, 0, 3, 2 };
            quad.UploadMeshData(false);
            return quad;
        }

        // At each scene scan: the up to four water surfaces nearest the camera keep their slots
        // or take free ones, and their water goes into the traced scene (addWater: a mesh, its world matrix, its slot). Every
        // surface counts, its caustics setting or not: the light that meets water is reflected and refracted by it either way.
        internal static int Capture(Vector3 camera, Func<Mesh, Matrix4x4, int, bool> addWater)
        {
            var near = new List<KeyValuePair<float, WaterSurface>>();
            outside.Clear();
            foreach (var s in UnityEngine.Object.FindObjectsByType<WaterSurface>(FindObjectsSortMode.None))
            {
                if (s == null || !s.isActiveAndEnabled) continue;
                if (LeftOutside(s))
                {
                    outside.Add(s.GetInstanceID());
                    if (!outsideLogged) { outsideLogged = true; Session.Log.LogInfo("Water: \"" + s.name + "\" of the scene \"" + s.gameObject.scene.name + "\" is not traced while the player is in an interior (the map's water: nothing in the room can meet it)"); }
                    continue;
                }
                float d = Distance(s, camera);
                near.Add(new KeyValuePair<float, WaterSurface>(d, s));
            }
            near.Sort((a, b) => a.Key.CompareTo(b.Key));
            // Once a session (PT-50): the evidence whether a scene ever has more water surfaces in reach than the tracer's slots.
            if (near.Count > Tgpt.MaxWater && !overLogged) { overLogged = true; Session.Log.LogInfo("Water: " + near.Count + " water surfaces within the traced scene; the nearest " + Tgpt.MaxWater + " are traced as water"); }
            AnyUntraced = near.Count > Tgpt.MaxWater;
            if (near.Count > Tgpt.MaxWater) near.RemoveRange(Tgpt.MaxWater, near.Count - Tgpt.MaxWater);
            var wanted = new List<WaterSurface>(); foreach (var n in near) wanted.Add(n.Value);
            for (int i = 0; i < Tgpt.MaxWater; i++) if (slots[i] != null && !wanted.Remove(slots[i])) slots[i] = null; // kept, or freed
            for (int i = 0; i < Tgpt.MaxWater && wanted.Count > 0; i++) if (slots[i] == null) { slots[i] = wanted[0]; wanted.RemoveAt(0); waves[i] = default; }
            int count = 0;
            for (int i = 0; i < Tgpt.MaxWater; i++)
            {
                var s = slots[i]; meshes[i] = 0; tops[i] = float.NegativeInfinity; bottoms[i] = float.PositiveInfinity;
                extents[i] = new Vector4(float.PositiveInfinity, float.PositiveInfinity, float.NegativeInfinity, float.NegativeInfinity);
                if (s == null) continue;
                if (IsCustomMesh(s))
                {
                    foreach (var r in s.meshRenderers)
                    {
                        var filter = r != null ? r.GetComponent<MeshFilter>() : null;
                        if (filter == null || filter.sharedMesh == null) continue;
                        var world = r.transform.localToWorldMatrix;
                        if (!addWater(filter.sharedMesh, world, i)) continue;
                        ++meshes[i];
                        Span(world, filter.sharedMesh.bounds, i);
                    }
                }
                else
                {
                    Matrix4x4 world;
                    // A sea without an end is traced as far as the camera draws (its far plane): no distance of the tracer's own. Its
                    // quad stands on a kilometre's grid and reaches that much farther, so it stays where it is from scan to scan:
                    // the tracer cuts the water's surface along a lattice on its base triangles (scene_water.cpp), which a quad
                    // moved with the camera would shift under every wave.
                    if (IsInfinite(s))
                    {
                        const float grid = 1024f;
                        var p = s.transform.position; float far = Camera.main != null ? Camera.main.farClipPlane : 10000f;
                        world = Matrix4x4.TRS(new Vector3(Mathf.Round(camera.x / grid) * grid, p.y, Mathf.Round(camera.z / grid) * grid), Quaternion.identity, new Vector3(2f * far + grid, 1f, 2f * far + grid));
                    }
                    else { var t = s.transform; var scale = t.lossyScale; world = Matrix4x4.TRS(t.position, t.rotation, new Vector3(Mathf.Abs(scale.x), 1f, Mathf.Abs(scale.z))); }
                    if (addWater(Quad(), world, i)) { ++meshes[i]; Span(world, Quad().bounds, i); }
                }
                count += meshes[i];
            }
            return count;
        }

        // A surface's simulation buffers (HDRP's WaterSurface.simulation.gpuBuffers): the displacement array, the additional data's.
        // HDRP keeps both as RTHandles (WaterSimulationResourcesGPU, the game's HDRP 17), which are no Texture: "as Texture" gave
        // null, so no slot ever got its waves and the caustics traced still water (GPT-6-astra's review, 2026-10-07; the type
        // read with ilspycmd from the game's own DLL). The handle's texture is its "rt".
        private static void Buffers(WaterSurface s, out Texture displacement, out Texture gradient)
        {
            displacement = s.GetDisplacementBuffer(); gradient = null;
            try
            {
                var simulation = simulationField != null ? simulationField.GetValue(s) : null;
                var gpu = simulation != null ? AccessTools.Field(simulation.GetType(), "gpuBuffers")?.GetValue(simulation) : null;
                var handle = gpu != null ? AccessTools.Field(gpu.GetType(), "additionalDataBuffer")?.GetValue(gpu) : null;
                gradient = handle as Texture ?? (handle != null ? AccessTools.Property(handle.GetType(), "rt")?.GetValue(handle, null) as Texture : null);
            }
            catch (Exception) { gradient = null; }
        }
        private static int Bands(WaterSurface s)
        {
            try { if (bandsProperty != null) return Mathf.Clamp((int)bandsProperty.GetValue(s), 1, 3); } catch (Exception) { }
            return 3;
        }

        // Every frame while tracing: each slot's water, its wave buffers and HDRP's numbers for them, sent when anything changed
        // (the waves' offsets move every frame).
        internal static void Tick()
        {
            var tracer = Session.Tracer;
            if (tracer == null || !tracer.Connected) return;
            int count = 0;
            for (int i = 0; i < Tgpt.MaxWater; i++)
            {
                var s = slots[i];
                var d = new NativeWaterCaustics { Size = 288, Abi = Tgpt.Abi, FoamTexture = -1, FoamMaskTexture = -1 };
                if (s != null && s.isActiveAndEnabled && !float.IsNegativeInfinity(tops[i]))
                {
                    Buffers(s, out var displacement, out var gradient);
                    if (displacement != displacements[i]) { displacements[i] = displacement; displacementPointers[i] = displacement != null ? displacement.GetNativeTexturePtr() : IntPtr.Zero; }
                    if (gradient != gradients[i]) { gradients[i] = gradient; gradientPointers[i] = gradient != null ? gradient.GetNativeTexturePtr() : IntPtr.Zero; }
                    var t = s.transform;
                    var w = IsCustomMesh(s) ? Matrix4x4.identity : Matrix4x4.TRS(t.position, t.rotation, Vector3.one).inverse;
                    d.W00 = w.m00; d.W01 = w.m01; d.W02 = w.m02; d.W03 = w.m03; d.W10 = w.m10; d.W11 = w.m11; d.W12 = w.m12; d.W13 = w.m13;
                    d.W20 = w.m20; d.W21 = w.m21; d.W22 = w.m22; d.W23 = w.m23;
                    // HDRP's numbers from this frame or the last (its constants are filled before the tracer's frame); still water
                    // without them, or without both buffers.
                    var v = waves[i];
                    if (v.Frame >= Time.frameCount - 1 && displacementPointers[i] != IntPtr.Zero && gradientPointers[i] != IntPtr.Zero)
                    {
                        d.Displacement = unchecked((ulong)displacementPointers[i].ToInt64()); d.Normals = unchecked((ulong)gradientPointers[i].ToInt64());
                        d.B0Scale = v.B0.x; d.B0OffsetX = v.B0.y; d.B0OffsetZ = v.B0.z; d.B0Amplitude = v.B0.w;
                        d.B1Scale = v.B1.x; d.B1OffsetX = v.B1.y; d.B1OffsetZ = v.B1.z; d.B1Amplitude = v.B1.w;
                        d.B2Scale = v.B2.x; d.B2OffsetX = v.B2.y; d.B2OffsetZ = v.B2.z; d.B2Amplitude = v.B2.w;
                        d.F0A = v.F0.x; d.F0B = v.F0.y; d.F1A = v.F1.x; d.F1B = v.F1.y; d.F2A = v.F2.x; d.F2B = v.F2.y;
                        d.BandCount = (uint)Bands(s);
                        d.MaxDisplacement = v.MaxMove;
                    }
                    // Once a surface, in the log: whether its waves reach the tracer. A silent fall back to still water hid that
                    // none ever did (2026-10-07).
                    if (wavesLogged.Add(s.GetInstanceID()))
                        Session.Log.LogInfo("Water: \"" + s.name + "\" " + (d.Displacement != 0 ?
                            "has its waves in the tracer (" + d.BandCount + " bands, " + (displacement != null ? displacement.width + "x" + displacement.height : "?") +
                                ", the waves move up to " + v.MaxMove.ToString("0.###") + " m)" :
                            "is traced as STILL water: " + (displacementPointers[i] == IntPtr.Zero ? "no displacement buffer" : gradientPointers[i] == IntPtr.Zero ? "no normals buffer" : "no wave numbers from HDRP this frame")));
                    d.FoamAmount = v.Foam.x; d.FoamIntensity = v.Foam.y; d.FoamRoughness = v.Foam.z; d.FoamTiling = v.Foam.w;
                    d.FoamMaskScaleX = v.Mask.x; d.FoamMaskScaleZ = v.Mask.y; d.FoamMaskOffsetX = v.Mask.z; d.FoamMaskOffsetZ = v.Mask.w;
                    d.FoamFlowX = v.Flow.x; d.FoamFlowZ = v.Flow.y; d.FoamTime = v.Flow.z; d.FoamCurrent = v.Flow.w;
                    d.FoamTexture = TextureShare.Handle(foamTexture);
                    d.FoamMaskTexture = TextureShare.Handle(s.simulationFoamMask);
                    var foamColor = s.foamColor;
                    d.FoamColorR = foamColor.r; d.FoamColorG = foamColor.g; d.FoamColorB = foamColor.b;
                    var scattering = s.scatteringColor;
                    d.ScatteringR = scattering.r; d.ScatteringG = scattering.g; d.ScatteringB = scattering.b;
                    // The surface's own smoothness (HDRP's Smoothness: its start and end values and the range of distances from
                    // the camera it goes from the one to the other over): the tracer's water reflects and lets light through by
                    // it (TGPT_WATER_SMOOTHNESS; until 2026-10-08 the tracer's water was a clear mirror facet).
                    d.Flags |= 1u;
                    d.SmoothnessNear = s.startSmoothness; d.SmoothnessFar = s.endSmoothness;
                    d.SmoothnessFadeStart = s.smoothnessFadeStart; d.SmoothnessFadeDistance = s.smoothnessFadeDistance;
                    float k = -Mathf.Log(0.02f) / Mathf.Max(s.absorptionDistance, 1e-4f);
                    var r = s.refractionColor;
                    d.ExtinctionR = k * Mathf.Max(1f - r.r, 0.01f); d.ExtinctionG = k * Mathf.Max(1f - r.g, 0.01f); d.ExtinctionB = k * Mathf.Max(1f - r.b, 0.01f);
                    d.Top = tops[i]; d.Bottom = bottoms[i];
                    var e = extents[i]; d.MinX = e.x; d.MinZ = e.y; d.MaxX = e.z; d.MaxZ = e.w;
                    // Once a surface, when HDRP's constants for it have been read: the game's own numbers as they go to the
                    // tracer (PT-81: no log held them, and the water was built and tested on numbers that were not the game's).
                    if (v.Frame >= Time.frameCount - 1 && numbersLogged.Add(s.GetInstanceID()))
                    {
                        try { Session.Log.LogInfo(Numbers(s, v, d)); } catch (Exception) { }
                        if (d.Normals != 0) LogSqueeze(s.name, gradients[i], v, (int)d.BandCount);
                    }
                    count = i + 1;
                }
                next[i] = d;
            }
            bool same = count == sentCount;
            for (int i = 0; i < count && same; i++) same = next[i].Equals(sent[i]);
            if (same) return;
            if (tracer.SetWaterCaustics(count > 0 ? next : null, (uint)count) == 0) { Array.Copy(next, sent, Tgpt.MaxWater); sentCount = count; }
        }

        // For the F8 report: each slot's surface, its water in the traced scene, its waves, and what of HDRP's the tracer doesn't follow.
        internal static string Text
        {
            get
            {
                var parts = new List<string>();
                for (int i = 0; i < Tgpt.MaxWater; i++)
                {
                    var s = slots[i];
                    if (s == null) continue;
                    var missing = new List<string>();
                    if ((s.largeCurrentMap != null && s.largeCurrentMapInfluence > 0f) || (s.ripplesCurrentMap != null && s.ripplesCurrentMapInfluence > 0f)) missing.Add("current maps");
                    if (s.waterMask != null) missing.Add("water mask");
                    if (s.deformation) missing.Add("deformation");
                    if (s.foam) missing.Add("decal foam buffer (not shared)");
                    if (sent[i].FoamIntensity > 0f && sent[i].FoamTexture < 0) missing.Add("foam erosion texture (not shared)");
                    bool waved = i < sentCount && sent[i].Displacement != 0;
                    parts.Add(i + ": \"" + s.name + "\" " + (IsCustomMesh(s) ? meshes[i] + " meshes" : IsInfinite(s) ? "sea" : "quad") +
                        (waved ? ", waves " + sent[i].BandCount + " bands " + (displacements[i] != null ? displacements[i].width + "px" : "?") : ", still (no waves yet)") +
                        // Its foam (PT-42): HDRP makes it from the wind (whitecaps, the simulation's amount) and from the water decals and
                        // foam generators the level places; none from the water's depth at a shore or rock.
                        (s.foam ? ", foam: whitecaps " + s.simulationFoamAmount.ToString("0.##") + (s.supportSimulationFoamMask && s.simulationFoamMask != null ? " (masked)" : "") +
                            ", decals and generators (" + s.foamResolution + ")" : ", no foam") +
                        (missing.Count > 0 ? ", not followed: " + string.Join(", ", missing.ToArray()) : ""));
                }
                return parts.Count > 0 ? string.Join("; ", parts.ToArray()) + (Session.WantsFrames ? "; traced water surfaces" : "; HDRP water surfaces") : "none near";
            }
        }

        internal static bool Patch(Harmony harmony)
        {
            var system = AccessTools.TypeByName("UnityEngine.Rendering.HighDefinition.WaterSystem");
            var constants = system != null ? AccessTools.Method(system, "UpdatePerSurfaceConstantBuffer", new[] { typeof(WaterSurface) }) : null;
            if (constants == null) return false;
            constantsField = AccessTools.Field(system, "m_ShaderVariablesPerSurfaceArray");
            simulationField = AccessTools.Field(typeof(WaterSurface), "simulation");
            bandsProperty = AccessTools.Property(typeof(WaterSurface), "numActiveBands");
            surfaceIndexField = AccessTools.Field(typeof(WaterSurface), "surfaceIndex");
            if (constantsField == null || simulationField == null || surfaceIndexField == null) Session.Log.LogWarning("The water's waves can't be read in this game build: caustics off still water only.");
            harmony.Patch(constants, finalizer: new HarmonyMethod(typeof(WaterCaustics), nameof(AfterSurfaceConstants)));
            return true;
        }
        // A field of HDRP's per-surface constants (boxed), by name.
        private static object Constant(object cb, string name)
        {
            if (!constantFields.TryGetValue(name, out var f)) { f = AccessTools.Field(cb.GetType(), name); constantFields[name] = f; }
            return f != null ? f.GetValue(cb) : null;
        }
        private static Vector2 Pair(object v)
        {
            if (v == null) return Vector2.zero;
            if (v is Vector2 u) return u;
            var t = v.GetType(); var x = AccessTools.Field(t, "x"); var y = AccessTools.Field(t, "y");
            return x != null && y != null ? new Vector2((float)x.GetValue(v), (float)y.GetValue(v)) : Vector2.zero;
        }
        private static Exception AfterSurfaceConstants(object __instance, WaterSurface currentWater, Exception __exception)
        {
            if (__exception != null || currentWater == null || constantsField == null) return __exception;
            try
            {
                int slot = Array.IndexOf(slots, currentWater);
                if (slot < 0 || !(constantsField.GetValue(__instance) is Array all)) return __exception;
                int index = surfaceIndexField != null ? (int)surfaceIndexField.GetValue(currentWater) : -1;
                if (index < 0 || index >= all.Length) return __exception;
                object cb = all.GetValue(index);
                if (foamTexture == null) {
                    var resources = AccessTools.Field(__instance.GetType(), "m_RuntimeResources")?.GetValue(__instance);
                    // (A property in the game's HDRP: asked as a field first, Harmony wrote "Could not find field ... foamMask" into
                    // the log before the property was read, PT-81.)
                    if (resources != null) foamTexture = (AccessTools.Property(resources.GetType(), "foamMask")?.GetValue(resources, null) ?? AccessTools.Field(resources.GetType(), "foamMask")?.GetValue(resources)) as Texture;
                }
                var maskScale = Pair(Constant(cb, "_SimulationFoamMaskScale"));
                var maskOffset = Pair(Constant(cb, "_SimulationFoamMaskOffset"));
                var orientations = Constant(cb, "_PatchOrientation");
                float angle = Pair(orientations).x;
                float simTime = Convert.ToSingle(Constant(cb, "_SimulationTime") ?? 0f);
                var v = new Waves
                {
                    B0 = (Vector4)Constant(cb, "_Band0_ScaleOffset_AmplitudeMultiplier"), B1 = (Vector4)Constant(cb, "_Band1_ScaleOffset_AmplitudeMultiplier"),
                    B2 = (Vector4)Constant(cb, "_Band2_ScaleOffset_AmplitudeMultiplier"),
                    F0 = Pair(Constant(cb, "_Band0_Fade")), F1 = Pair(Constant(cb, "_Band1_Fade")), F2 = Pair(Constant(cb, "_Band2_Fade")),
                    MaxMove = Mathf.Max((float)Constant(cb, "_MaxWaveHeight"), (float)Constant(cb, "_MaxWaveDisplacement")),
                    Frame = Time.frameCount,
                    Foam = new Vector4(Convert.ToSingle(Constant(cb, "_SimulationFoamAmount") ?? 0f),
                        Convert.ToSingle(Constant(cb, "_SimulationFoamIntensity") ?? 0f), 1f - currentWater.foamSmoothness, currentWater.foamTextureTiling),
                    Mask = new Vector4(maskScale.x, maskScale.y, 0.5f - maskOffset.x * maskScale.x, 0.5f - maskOffset.y * maskScale.y),
                    Flow = new Vector4(Mathf.Cos(angle), Mathf.Sin(angle), simTime, currentWater.foamCurrentInfluence),
                };
                waves[slot] = v;
            }
            catch (Exception) { }
            return __exception;
        }
    }
}
