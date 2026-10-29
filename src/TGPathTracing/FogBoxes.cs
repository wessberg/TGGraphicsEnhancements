// SPDX-License-Identifier: MIT
// PT-112: the game's local fog volumes (HDRP's Local Volumetric Fog boxes: mist patches, a blizzard's, rain's) handed to the
// tracer every frame while the fog is traced (TGPT_SetFogBoxes; the medium in pt_medium.hlsli fog_box). Every enabled box
// in the world, as HDRP voxelizes it (LocalVolumetricFog.PrepareDrawCall, LocalVolumetricFogArtistParameters
// ConvertToEngineData, the default Fog Volume graph), whatever its distance: HDRP leaves out the ones past their own fade
// or the Fog volume's Depth Extent, the tracer none. The mask is the game's own 3D texture, opened with no copy. Sent every
// frame: the mask scrolls with the game's shader time (HDRP's HDCamera.time, Time.time while materials animate).
// Not traced, each said once in the log by name: a box with a fog material of its own (HDRP's Material mask mode: a shader
// graph the tracer cannot read; rain's), a blend other than Additive (the game's are all Additive), a mask that is not a 3D
// texture in a format whose most is 1.
using System;
using System.Collections.Generic;
using System.Reflection;
using HarmonyLib;
using UnityEngine;
using UnityEngine.Rendering.HighDefinition;
namespace TGPathTracing
{
    internal static class FogBoxes
    {
        private static bool looked;
        private static object manager;
        private static FieldInfo volumesField;
        private static NativeFogBox[] boxes = new NativeFogBox[16];
        private static readonly HashSet<int> skippedSaid = new HashSet<int>();
        private static int lastSent = -1, lastSkipped = -1;
        private static readonly System.Text.StringBuilder described = new System.Text.StringBuilder();
        private static bool saidMissing;
        // The 3D mask formats whose texels are 0 to 1 (unorm): a box's bound is its extinction then.
        private static readonly HashSet<TextureFormat> UnitFormats = new HashSet<TextureFormat> {
            TextureFormat.Alpha8, TextureFormat.R8, TextureFormat.R16, TextureFormat.RG16, TextureFormat.RGB24, TextureFormat.RGBA32,
            TextureFormat.ARGB32, TextureFormat.BGRA32, TextureFormat.DXT1, TextureFormat.DXT5, TextureFormat.BC4, TextureFormat.BC5, TextureFormat.BC7 };

        private static List<LocalVolumetricFog> Volumes()
        {
            if (!looked)
            {
                looked = true;
                var type = AccessTools.TypeByName("UnityEngine.Rendering.HighDefinition.LocalVolumetricFogManager");
                var property = type != null ? AccessTools.Property(type, "manager") : null;
                manager = property?.GetValue(null);
                volumesField = type != null ? AccessTools.Field(type, "m_Volumes") : null;
                if (manager == null || volumesField == null)
                    Session.Log.LogWarning("Traced fog: HDRP's list of local fog volumes was not found (LocalVolumetricFogManager.m_Volumes); the game's fog boxes are not traced.");
            }
            return manager != null && volumesField != null ? volumesField.GetValue(manager) as List<LocalVolumetricFog> : null;
        }

        private static void Skip(LocalVolumetricFog v, string why, ref int skipped)
        {
            ++skipped;
            if (skippedSaid.Add(v.GetInstanceID()))
                Session.Log.LogWarning("Traced fog: the fog box '" + v.name + "' is not traced (" + why + "); HDRP's fog would have drawn it.");
        }

        // Every frame the fog is traced, before the frame is submitted. traced false: none (sent once).
        internal static void Send(bool traced)
        {
            var tracer = Session.Tracer;
            if (tracer == null || !tracer.Connected || !tracer.HasFogBoxes)
            {
                if (traced && tracer != null && tracer.Connected && !saidMissing) { saidMissing = true; Session.Log.LogWarning("Traced fog: this native side has no TGPT_SetFogBoxes (an older build): the game's fog boxes are not traced."); }
                return;
            }
            if (!traced) { if (lastSent != 0) { tracer.SetFogBoxes(boxes, 0); lastSent = 0; } return; }
            var volumes = Volumes();
            int count = 0, skipped = 0;
            float time = Time.time;
            if (volumes != null)
            {
                foreach (var v in volumes)
                {
                    if (v == null || !v.isActiveAndEnabled) continue;
                    var p = v.parameters;
                    if (p.maskMode != LocalVolumetricFogMaskMode.Texture) { Skip(v, "a fog material of its own, " + (p.materialMask != null ? p.materialMask.name : "none"), ref skipped); continue; }
                    if (p.blendingMode != LocalVolumetricFogBlendingMode.Additive) { Skip(v, "blend mode " + p.blendingMode, ref skipped); continue; }
                    var box = new NativeFogBox { Mask = -1, MaskMost = 1f };
                    if (p.volumeMask != null)
                    {
                        var mask = p.volumeMask as Texture3D;
                        if (mask == null) { Skip(v, "its mask '" + p.volumeMask.name + "' is not a 3D texture", ref skipped); continue; }
                        if (!UnitFormats.Contains(mask.format)) { Skip(v, "its mask '" + mask.name + "' is " + mask.format + ", not a format of texels from 0 to 1", ref skipped); continue; }
                        box.Mask = TextureShare.Handle(mask);
                        if (box.Mask < 0) { ++skipped; continue; } // not open (TextureShare says why): left out this frame
                        if (mask.format == TextureFormat.Alpha8) box.Flags |= Tgpt.FogBoxAlphaOnly;
                        if (mask.wrapMode == TextureWrapMode.Clamp) box.Flags |= Tgpt.FogBoxClamp;
                        if (mask.filterMode == FilterMode.Point) box.Flags |= Tgpt.FogBoxPoint;
                    }
                    // The box as HDRP orients it (OrientedBBox of TRS(position, rotation, scaled size)).
                    var t = v.transform;
                    Vector3 scale = p.scaleMode == LocalVolumetricFogScaleMode.InheritFromHierarchy ? t.lossyScale : Vector3.one;
                    Vector3 size = Vector3.Max(Vector3.one * 0.001f, Vector3.Scale(p.size, scale));
                    var trs = Matrix4x4.TRS(t.position, t.rotation, size);
                    Vector3 c0 = trs.GetColumn(0), c1 = trs.GetColumn(1), c2 = trs.GetColumn(2), c3 = trs.GetColumn(3);
                    Vector3 right = c0 / c0.magnitude, up = c1 / c1.magnitude;
                    box.CenterX = c3.x; box.CenterY = c3.y; box.CenterZ = c3.z;
                    box.RightX = right.x; box.RightY = right.y; box.RightZ = right.z;
                    box.UpX = up.x; box.UpY = up.y; box.UpZ = up.z;
                    box.ExtentX = 0.5f * c0.magnitude; box.ExtentY = 0.5f * c1.magnitude; box.ExtentZ = 0.5f * c2.magnitude;
                    // ConvertToEngineData's numbers.
                    box.Extinction = 1f / Mathf.Max(0.05f, p.meanFreePath);
                    box.AlbedoR = p.albedo.r; box.AlbedoG = p.albedo.g; box.AlbedoB = p.albedo.b;
                    box.PosFadeX = Mathf.Min(1f / p.positiveFade.x, float.MaxValue); box.PosFadeY = Mathf.Min(1f / p.positiveFade.y, float.MaxValue); box.PosFadeZ = Mathf.Min(1f / p.positiveFade.z, float.MaxValue);
                    box.NegFadeX = Mathf.Min(1f / p.negativeFade.x, float.MaxValue); box.NegFadeY = Mathf.Min(1f / p.negativeFade.y, float.MaxValue); box.NegFadeZ = Mathf.Min(1f / p.negativeFade.z, float.MaxValue);
                    float fadeLength = Mathf.Max(p.distanceFadeEnd - p.distanceFadeStart, 1.526E-05f);
                    box.RcpDistFadeLen = 1f / fadeLength; box.EndTimesRcpDistFadeLen = p.distanceFadeEnd * box.RcpDistFadeLen;
                    if (p.falloffMode == LocalVolumetricFogFalloffMode.Exponential) box.Flags |= Tgpt.FogBoxExponential;
                    if (p.invertFade) box.Flags |= Tgpt.FogBoxInvert;
                    box.TilingX = p.textureTiling.x; box.TilingY = p.textureTiling.y; box.TilingZ = p.textureTiling.z;
                    // The default Fog Volume graph: uvw = box * tiling + shader time * scroll speed.
                    box.ScrollX = p.textureScrollingSpeed.x * time; box.ScrollY = p.textureScrollingSpeed.y * time; box.ScrollZ = p.textureScrollingSpeed.z * time;
                    if (count == boxes.Length) Array.Resize(ref boxes, boxes.Length * 2);
                    boxes[count++] = box;
                    // (Each box's size and density for the log when the set changes: the tracer's tracking through a box costs
                    // about its extinction times the way's length in it, a step each; PT-129, the GPU hang of 2026-10-11.)
                    described.Append(" '").Append(v.name).Append("' ").Append((2f * box.ExtentX).ToString("0.#")).Append(" x ")
                        .Append((2f * box.ExtentY).ToString("0.#")).Append(" x ").Append((2f * box.ExtentZ).ToString("0.#"))
                        .Append(" m, extinction ").Append(box.Extinction.ToString("0.###")).Append("/m").Append(p.volumeMask != null ? ", masked" : "").Append(';');
                }
            }
            int result = tracer.SetFogBoxes(boxes, count);
            if (count != lastSent || skipped != lastSkipped)
            {
                Session.Log.LogInfo("Traced fog: " + count + " of the game's fog boxes sent to the tracer" + (skipped != 0 ? ", " + skipped + " not traced" : "") +
                    (result != 0 ? " (TGPT_SetFogBoxes returned " + result + ")" : "") + "." + (count != 0 ? " The boxes:" + described : ""));
                lastSent = count; lastSkipped = skipped;
            }
            described.Length = 0;
        }

        internal static void Reset() { lastSent = -1; lastSkipped = -1; skippedSaid.Clear(); }
    }
}
