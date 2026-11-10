// SPDX-License-Identifier: MIT
// Characters in the traced scene: the game draws them with its own GPU skinning (Kandra), which writes every
// character's posed vertices, in world space, into one buffer each frame (its batch draws them with an identity
// object-to-world). Just before a frame goes to the native side, the renderers nearest the camera are listed with
// where their vertices are in that buffer; the native side copies them, updates each one's acceleration structure
// and traces it with the world, so characters shadow themselves and what is around them, and block the light that
// bounces there. Each renderer's triangles are read back from the game's index buffer once, without waiting; only
// the submeshes the game draws (one per material) are kept. Once, a few traced vertices are checked against the
// renderer's bounding sphere, and the log says whether they sit where the game draws them. Characters carry no
// texture coordinates to the tracer: their materials keep their own color (SceneCapture.CharacterMaterial); hair
// and transparent parts are left out (their triangles are not registered), other alpha-tested parts are traced solid.
using System;
using System.Collections.Generic;
using System.Reflection;
using Awaken.Kandra;
using HarmonyLib;
using Unity.Mathematics;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.HighDefinition;
namespace TGPathTracing
{
    internal static class CharacterCapture
    {
        // Every character in the traced scene (the Scene Radius row), the nearest first (PT-50: until 2026-10-07 those within
        // 100 m, 64 renderers and 524,288 vertices, which the harness's busiest scenes reached: the rest cast no shadow and
        // showed in no reflection). At 30 m their shadows popped in; past 45 m characters floated in the sun, where HDRP's
        // cascades used to shadow them. Refitted in between rebuilds (scene.cpp kCharacterRebuild), 64 cost a third of a
        // millisecond offline. The counts below are buffer sizes, far past what the game shows at once.
        private const int MaxRenderers = 256;        // the native side's limit (a character is several renderers): TGPT_MAX_SKINNED
        private const int MaxVertices = 4096 * 512;  // what the native side's texture holds a frame (runtime.hpp kSkinRows)
        private const int MaxSubmeshes = 8;          // TGPT_SKINNED_MATERIALS
        // Renderers drawn with more submeshes than that, once each (PT-50: the evidence whether the cap is ever met).
        private static readonly HashSet<string> overSubmeshes = new HashSet<string>();
        private sealed class Character
        {
            internal int Handle = -1;                // the native mesh, once its triangles are registered
            internal bool Pending, Failed, Textured; // Textured: registered with its texture coordinates
            internal long Key;                       // the mesh, index range and left-out submeshes the triangles came from
        }
        private static readonly Dictionary<int, Character> characters = new Dictionary<int, Character>();
        private static readonly FieldInfo IndicesBufferField = typeof(KandraRendererManager).Assembly.GetType("Awaken.Kandra.Managers.MeshBroker")?
            .GetField("_indicesBuffer", BindingFlags.NonPublic | BindingFlags.Instance);
        // The original vertices' extra data (AdditionalVertexData: a half2 texture coordinate, the tangent's sign).
        private static readonly FieldInfo AdditionalVerticesField = typeof(KandraRendererManager).Assembly.GetType("Awaken.Kandra.Managers.MeshManager")?
            .GetField("_additionalVerticesDataBuffer", BindingFlags.NonPublic | BindingFlags.Instance);
        private static int generation;               // readbacks requested before a Reset register nothing
        private static bool spaceChecked;
        private static float nextSpaceCheck;
        private static string lastProblem = "";
        // Kandra's output buffer is created once; its native pointer is asked for once (asking waits for the render thread).
        private static GraphicsBuffer outputBuffer;
        private static IntPtr outputPointer;
        private static readonly List<(KandraRenderer renderer, float distance, float4 sphere)> candidates = new List<(KandraRenderer, float, float4)>();
        private static readonly List<NativeSkinnedInstance> instances = new List<NativeSkinnedInstance>();
        internal static int Traced { get; private set; }
        // Renderers whose triangles are being read back from the game's buffers (registered when the read ends): the
        // arrival's timeline counts them as still to come (Arrival.cs, PT-98).
        internal static int Reading { get; private set; }
        internal static string Summary => Traced + " renderers" + (lastProblem.Length > 0 ? " (last problem: " + lastProblem + ")" : "");

        internal static void Reset()
        {
            characters.Clear();
            ++generation;
            Traced = 0; Reading = 0;
        }

        // Hair, eyebrows and lashes stay opaque while tracing, as the game draws hair far away:
        // alpha-tested, drawn forward or into the GBuffer, and so traced. Up close the game makes
        // them transparent (hair by distance, BodyFeatures.RefreshDistanceBand; eyebrows and lashes
        // always), which camera rays can't find in front of HDRP's surface, so HDRP drew them with
        // its own light over the traced image. Hair goes through the game's own
        // HairController.Refresh (patched: what the game asks for is kept, and given back when
        // tracing stops); eyebrows and lashes are switched on their materials, whose alpha test is
        // on already.
        private static readonly Type HairControllerType = AccessTools.TypeByName("Awaken.TG.Main.Character.Features.HairController");
        private static readonly MethodInfo HairRefresh = HairControllerType != null ? AccessTools.Method(HairControllerType, "Refresh", new[] { typeof(bool), typeof(bool) }) : null;
        private static readonly FieldInfo HairRenderer = HairControllerType != null ? AccessTools.Field(HairControllerType, "_kandraRenderer") : null;
        private static readonly Dictionary<int, bool> hairRequested = new Dictionary<int, bool>(); // by controller: transparent, as the game asked
        private static readonly Dictionary<Material, float> heldLayers = new Dictionary<Material, float>(); // eyebrows, lashes: their surface type
        private static readonly List<KandraRenderer> heldRenderers = new List<KandraRenderer>();
        private static readonly HashSet<int> heldRendererIds = new HashSet<int>();
        private static bool holdOpaque, refreshing, holdLogged;
        internal static bool PatchHair(Harmony harmony)
        {
            if (HairRefresh == null) return false;
            harmony.Patch(HairRefresh, prefix: new HarmonyMethod(typeof(CharacterCapture), nameof(BeforeHairRefresh)),
                postfix: new HarmonyMethod(typeof(CharacterCapture), nameof(AfterHairRefresh)));
            return true;
        }
        private static void BeforeHairRefresh(Component __instance, ref bool transparent, ref bool blendshapes)
        {
            if (__instance == null) return;
            if (!refreshing) hairRequested[__instance.GetInstanceID()] = transparent;
            if (holdOpaque) { RememberQueues(__instance); transparent = false; blendshapes = false; }
        }
        // HDRP's ValidateMaterial (which the game's Refresh calls) derives a material's render queue from the queue it has:
        // transparent "before refraction" (2750, as the game's hair is authored; its scalp mask 2749) made opaque and
        // transparent again comes back as plain transparent (3000, 2999), drawn in another pass than the game set. Found by
        // the in-game harness on 2026-10-06: after one On and Off, nine materials of a tavern's people had another queue
        // than before. Each material's queue from before it was held opaque is kept here, and given back with it.
        private static readonly Dictionary<Material, int> heldQueues = new Dictionary<Material, int>();
        private static void RememberQueues(Component controller)
        {
            var renderer = HairRenderer?.GetValue(controller) as KandraRenderer;
            var materials = renderer != null ? renderer.rendererData.RenderingMaterials : null;
            if (materials == null) return;
            foreach (var m in materials) if (m != null && !heldQueues.ContainsKey(m)) heldQueues[m] = m.renderQueue;
        }
        // Only where the material is transparent again and was before: what the game made opaque meanwhile (a character
        // that walked far away) keeps the queue the game's own Refresh gives it. True when a queue changed.
        private static bool GiveQueueBack(Material m)
        {
            if (m == null || !heldQueues.TryGetValue(m, out int queue)) return false;
            if (queue <= 2500 || !m.HasProperty("_SurfaceType") || !(m.GetFloat("_SurfaceType") > 0.5f) || m.renderQueue == queue) return false;
            m.renderQueue = queue;
            return true;
        }
        // The game's Refresh made the hair's materials opaque (its scalp mask too): each cut where it is half opaque.
        private static void AfterHairRefresh(Component __instance)
        {
            if (!holdOpaque || __instance == null) return;
            var renderer = HairRenderer?.GetValue(__instance) as KandraRenderer;
            var materials = renderer != null ? renderer.rendererData.RenderingMaterials : null;
            if (materials != null) foreach (var m in materials) RaiseCut(m);
        }

        // A layer held opaque is cut where it is at least half opaque (HeldCut), not at the cut it blends from as a transparent
        // layer: RealHair's _AlphaClip 0.1, the lashes' _AlphaCutoff 0.067 and the scalp masks' 0.05. Opaque at those, the faint
        // fringe of every clump and card turned solid: hair like LEGO bricks, the brows and lashes black bars and a black band at
        // the hairline where a scalp mask's faint edge is near black (2026-09-28). Kept at 0.5, a brow keeps its dense 27 %
        // (78 % at 0.1), the lashes 48 % (89 %), a head of hair 73 % (97 %); 0.5 is also where the game's hair turns solid for
        // its depth prepass (_DepthPrepass). Given back when tracing stops.
        private const float HeldCut = 0.5f;
        private static readonly Dictionary<Material, KeyValuePair<string, float>> heldCuts = new Dictionary<Material, KeyValuePair<string, float>>();
        private static void RaiseCut(Material m)
        {
            if (m == null || heldCuts.ContainsKey(m)) return;
            // RealHair's Shader Graph cuts by _AlphaClip (its depth pass, read from the compiled shader); Kandra_Lit by _AlphaCutoff.
            string property = m.HasProperty("_AlphaClip") ? "_AlphaClip" : m.HasProperty("_AlphaCutoff") ? "_AlphaCutoff" : null;
            if (property == null) return;
            float was = m.GetFloat(property);
            if (!(was < HeldCut)) return;
            if (heldCuts.Count >= 256)
            {
                var gone = new List<Material>();
                foreach (var entry in heldCuts) if (entry.Key == null) gone.Add(entry.Key);
                foreach (var g in gone) heldCuts.Remove(g);
            }
            heldCuts[m] = new KeyValuePair<string, float>(property, was);
            m.SetFloat(property, HeldCut);
        }
        private static void RestoreCuts()
        {
            foreach (var entry in heldCuts)
                try { if (entry.Key != null) entry.Key.SetFloat(entry.Value.Key, entry.Value.Value); } catch { }
            heldCuts.Clear();
        }
        // A material's cut as the game set it (before HeldCut raised it, or a layer's hiding: LayerCut), for the tracer's own.
        internal static float GameCut(Material m, string property, float fallback)
        {
            if (m == null) return fallback;
            if (heldCuts.TryGetValue(m, out var held) && held.Key == property) return held.Value;
            int cut = Array.IndexOf(LayerCutProperties, property);
            if (cut >= 0 && layerCuts.TryGetValue(m, out var taken) && !float.IsNaN(taken[cut])) return taken[cut];
            return m.HasProperty(property) ? m.GetFloat(property) : fallback;
        }
        // Path tracing on or off (HdrpPasses): every hair the game has made opaque, or given back what the game last asked for;
        // eyebrows and lashes given back.
        internal static void HoldOpaque(bool on)
        {
            if (on == holdOpaque) return;
            holdOpaque = on;
            if (!on) { RestoreLayerHair(true); RestoreRenderers(true); RestoreCuts(); }
            nextDrawReport = Time.unscaledTime + 3f; drawReports = 0;
            int hairs = 0;
            if (HairRefresh != null)
            {
                refreshing = true;
                try
                {
                    foreach (var obj in UnityEngine.Object.FindObjectsByType(HairControllerType, FindObjectsInactive.Exclude, FindObjectsSortMode.None))
                    {
                        var controller = obj as Component;
                        if (controller == null) continue;
                        int id = controller.GetInstanceID();
                        if (!hairRequested.TryGetValue(id, out bool transparent)) hairRequested[id] = transparent = HairTransparent(controller);
                        bool now = !on && transparent;
                        try { HairRefresh.Invoke(controller, new object[] { now, now }); ++hairs; }
                        catch (Exception ex) { Problem("hair " + controller.name + " kept its state: " + (ex.InnerException ?? ex).Message); }
                        if (on) continue;
                        // The game's Refresh has set the queues anew: each one back as it was before the hold.
                        try
                        {
                            var renderer = HairRenderer?.GetValue(controller) as KandraRenderer;
                            var materials = renderer != null ? renderer.rendererData.RenderingMaterials : null;
                            bool changed = false;
                            if (materials != null) foreach (var m in materials) changed |= GiveQueueBack(m);
                            if (changed) renderer.MaterialsTransparencyChanged();
                        }
                        catch (Exception ex) { Problem("hair " + controller.name + " kept another render queue: " + ex.Message); }
                    }
                }
                finally { refreshing = false; }
            }
            if (!on)
            {
                foreach (var entry in heldLayers)
                    try { if (entry.Key != null) { entry.Key.SetFloat("_SurfaceType", entry.Value); HDMaterial.ValidateMaterial(entry.Key); GiveQueueBack(entry.Key); } } catch { }
                heldLayers.Clear();
                foreach (var r in heldRenderers) try { if (r != null) r.MaterialsTransparencyChanged(); } catch { }
                heldRenderers.Clear(); heldRendererIds.Clear();
                heldQueues.Clear();
            }
            if (!holdLogged && on)
            {
                holdLogged = true;
                Session.Log.LogInfo("Characters: hair, eyebrows and lashes held opaque while tracing, as the game draws hair far away, cut where at least " +
                    HeldCut.ToString("0.0") + " opaque (" + hairs + " hair renderers" +
                    (HairRefresh == null ? "; the game's HairController.Refresh was not found, so hair keeps the game's state" : "") + ")");
            }
        }
        private static bool HairTransparent(Component controller)
        {
            var renderer = HairRenderer?.GetValue(controller) as KandraRenderer;
            var materials = renderer != null ? renderer.rendererData.RenderingMaterials : null;
            if (materials != null) foreach (var m in materials) if (m != null && m.HasProperty("_SurfaceType") && m.GetFloat("_SurfaceType") > 0.5f) return true;
            return false;
        }
        // Eyebrows and lashes the game draws transparent, alpha-tested too (the eyes' cornea stays as it is).
        private static bool SoftLayer(Material m) =>
            m.HasProperty("_SurfaceType") && m.GetFloat("_SurfaceType") > 0.5f && m.HasProperty("_AlphaCutoffEnable") && m.GetFloat("_AlphaCutoffEnable") > 0.5f &&
            (m.name.IndexOf("Brow", StringComparison.OrdinalIgnoreCase) >= 0 || m.name.IndexOf("Lash", StringComparison.OrdinalIgnoreCase) >= 0);
        // This renderer's eyebrows and lashes opaque while tracing, and the renderer told (Kandra sorts its materials by it); its
        // cornea's HDRP draw is hidden; its mesh and dielectric material are traced above the iris.
        private static void HoldLayersOpaque(KandraRenderer renderer, Material[] materials, int count)
        {
            bool uses = false;
            for (int s = 0; s < count; ++s)
            {
                var m = materials[s];
                if (m == null) continue;
                if (m.name.IndexOf("Cornea", StringComparison.OrdinalIgnoreCase) >= 0) { HideLayer(m); continue; }
                if (SceneCapture.LayerBrow(m)) continue; // Brows, once the frame's materials are read
                if (!heldLayers.ContainsKey(m) && !SoftLayer(m)) continue;
                HoldLayerOpaque(m);
                uses = true;
            }
            if (uses && heldRendererIds.Add(renderer.GetInstanceID())) { heldRenderers.Add(renderer); renderer.MaterialsTransparencyChanged(); }
        }
        private static void HoldLayerOpaque(Material m)
        {
            if (!heldLayers.ContainsKey(m))
            {
                heldLayers[m] = m.GetFloat("_SurfaceType");
                if (!heldQueues.ContainsKey(m)) heldQueues[m] = m.renderQueue;
                m.SetFloat("_SurfaceType", 0f);
                HDMaterial.ValidateMaterial(m);
            }
            RaiseCut(m);
        }
        // Eyebrows (SceneCapture.LayerBrow) the tracer draws as a layer stay transparent while tracing, as the game draws them:
        // HDRP draws a transparent surface only after the tracer has taken its depth and GBuffer, and its transparent passes
        // (the builds the game itself draws brows with) cut them by the cuts HideLayerHair raises past 1, so it draws nothing
        // of them. Held opaque (until 2026-10-06, round 4) HDRP drew their whole cards into its depth and normals before the
        // tracer took them: RealHair has no opaque build that cuts (pt_prepare's note of 2026-09-28, "HDRP drew each card whole
        // whatever its cut"), so the passes and cuts the plugin switched never held them, and the tracer found no brow in front
        // of that depth and nothing lit the card (black bars; the Surfaces view magenta over the brows). Brows without a detail
        // tile (no layer) are held opaque as before and traced where HDRP draws them. Given back when tracing stops (HoldOpaque).
        private static bool browsLogged;
        private static void Brows(KandraRenderer renderer, Material[] materials, int count, bool textured)
        {
            bool changed = false;
            for (int s = 0; s < count; ++s)
            {
                var m = materials[s];
                if (!SceneCapture.LayerBrow(m)) continue;
                if (SceneCapture.IsHairLayer(m, textured))
                {
                    if (heldLayers.TryGetValue(m, out float surface))
                    {
                        heldLayers.Remove(m);
                        try { m.SetFloat("_SurfaceType", surface); HDMaterial.ValidateMaterial(m); GiveQueueBack(m); changed = true; } catch { }
                    }
                    if (!browsLogged)
                    {
                        browsLogged = true;
                        Session.Log.LogInfo("Characters: eyebrows \"" + m.name + "\" drawn by the tracer as a layer stay transparent while tracing, as the game draws them " +
                            "(_SurfaceType " + (m.HasProperty("_SurfaceType") ? m.GetFloat("_SurfaceType").ToString("0") : "none") + ", queue " + m.renderQueue +
                            "): HDRP's opaque passes leave them out, its transparent ones come after the tracer's capture and cut them away");
                    }
                }
                else if (!heldLayers.ContainsKey(m)) { HoldLayerOpaque(m); changed = true; }
            }
            if (!changed || renderer == null) return;
            if (heldRendererIds.Add(renderer.GetInstanceID())) heldRenderers.Add(renderer);
            try { renderer.MaterialsTransparencyChanged(); } catch { }
        }

        // Hair the tracer draws as a layer over HDRP's surface (TGPT_MATERIAL_HAIR_LAYER, pt_hair: RealHair's hair, beards and
        // brows, with their strands at their texture's own size): every HDRP pass of its material but the shadow caster hidden
        // while a frame's characters hold its renderer, so that HDRP's depth and GBuffer hold the skin, the scalp or the wall
        // behind it. Held opaque instead, HDRP drew each card whole and the tracer cut it at a quarter of its texture's size:
        // fat, blurred clumps, shimmering tips and brows with their cards' outlines on the skin (2026-09-28). Given back when a
        // frame's characters leave its renderer out (too far, too many, characters off) and when tracing stops.
        // Hidden two ways, every frame: its passes switched off (the game's HDMaterial.ValidateMaterial, which its hair refresh
        // calls, switches some back on), and its own cut raised past 1 (LayerCut), so that the game's shader discards every
        // texel of it in each of its passes, whatever the character renderer's batches make of the switched-off passes: with
        // the passes alone (12:45), HDRP went on drawing the hair (12:52: HDRP's own woolly hair over the traced layer). Given
        // back after LayerGrace frames without its renderer among the traced characters, or when tracing stops.
        private const float LayerCut = 1.1f;
        private const int LayerGrace = 30;
        private static readonly Dictionary<Material, List<string>> layerHidden = new Dictionary<Material, List<string>>();
        // RealHair's three cuts, each raised past 1 while hidden: its alpha clip, which its forward pass discards by, and the two
        // its depth passes discard by instead (read from the compiled shader: some depth variants cut at _DepthPrepass alone).
        // With the clip alone at 1.1 (until 2026-10-06, round 3) HDRP went on writing a brow's depth and normals where its
        // alpha reached _DepthPrepass (0.95), and nothing lit them: thick black bars (Kandra's batches draw a material's passes
        // whether they are switched off or not; the head's renderer cannot be taken out of its culling as a head of hair is).
        private static readonly string[] LayerCutProperties = { "_AlphaClip", "_DepthPrepass", "_DepthPostpass" };
        private static readonly Dictionary<Material, float[]> layerCuts = new Dictionary<Material, float[]>(); // the game's (NaN: not taken)
        private static readonly Dictionary<Material, int> layerSeen = new Dictionary<Material, int>();       // the last frame it was traced
        private static readonly HashSet<int> layerRenderers = new HashSet<int>();
        private static bool layerLogged;
        private static int layerAgainLogged;
        // The game's RealHair names its shadow pass SHADOWCASTER (the log, 2026-10-06): compared as written, it was switched off
        // with the rest, and the hair cast no HDRP shadow while tracing.
        private static bool IsShadowCaster(string mode) => string.Equals(mode, "ShadowCaster", StringComparison.OrdinalIgnoreCase);
        // A head of hair and a beard (TGPT_MATERIAL_HAIR_LAYER) are hidden from HDRP as brows are. From 2026-10-07 (PT-57) to
        // 2026-10-11 HDRP drew their depth while tracing (HoldLayerDepth), so that the depth and motion vectors at a hair pixel
        // were the hair's for DLSS and Ray Reconstruction; since PT-112 (A) HDRP draws no opaque object while tracing and the
        // tracer writes the frame's depth and motion itself, the hair's where its camera ray stopped at a card (pt_primary).
        private static void HideLayerHair(KandraRenderer renderer, Material m)
        {
            if (m == null || m.shader == null) return;
            layerSeen[m] = Time.frameCount;
            bool first = !layerHidden.TryGetValue(m, out var passes);
            if (first) { passes = new List<string>(); layerHidden[m] = passes; }
            bool changed = false;
            List<string> again = null;
            for (int p = 0; p < m.shader.passCount; ++p)
            {
                string mode = m.shader.FindPassTagValue(p, LightMode).name;
                if (string.IsNullOrEmpty(mode) || IsShadowCaster(mode) || !m.GetShaderPassEnabled(mode)) continue;
                m.SetShaderPassEnabled(mode, false);
                if (!passes.Contains(mode)) passes.Add(mode);
                if (!first) { if (again == null) again = new List<string>(); if (!again.Contains(mode)) again.Add(mode); }
                changed = true;
            }
            float cutWas = -1f;
            string cutName = "";
            for (int c = 0; c < LayerCutProperties.Length; ++c)
            {
                string property = LayerCutProperties[c];
                if (!m.HasProperty(property) || !(m.GetFloat(property) < LayerCut - 0.01f)) continue;
                float was = m.GetFloat(property);
                if (!layerCuts.TryGetValue(m, out var taken)) { taken = new[] { float.NaN, float.NaN, float.NaN }; layerCuts[m] = taken; }
                if (float.IsNaN(taken[c])) taken[c] = was;
                m.SetFloat(property, LayerCut);
                if (cutWas < 0f) { cutWas = was; cutName = property; }
                changed = true;
            }
            // Hidden before and found drawn by HDRP again (the harness's pictures of 2026-10-06: HDRP's hair depth and normals
            // under the layer while its passes were said off): what turned back on, the first few times.
            if (!first && (again != null || cutWas >= 0f) && layerAgainLogged < 12)
            {
                ++layerAgainLogged;
                Session.Log.LogInfo("Characters: layer hair \"" + m.name + "\" #" + m.GetInstanceID() + " was HDRP's again (" +
                    (again != null ? "passes " + string.Join(", ", again.ToArray()) + " on" : "no pass on") + (cutWas >= 0f ? ", " + cutName + " " + cutWas.ToString("0.##") : "") +
                    ", _SurfaceType " + (m.HasProperty("_SurfaceType") ? m.GetFloat("_SurfaceType").ToString("0") : "none") + ", queue " + m.renderQueue +
                    "): hidden again");
            }
            // The character renderer told once (it registers its materials with its batches again).
            if (changed && renderer != null && layerRenderers.Add(renderer.GetInstanceID()))
                try { renderer.MaterialsTransparencyChanged(); } catch { }
            if (!layerLogged)
            {
                layerLogged = true;
                Session.Log.LogInfo("Characters: hair drawn by the tracer as a layer over HDRP's surface (material \"" + m.name + "\": HDRP's passes " +
                    string.Join(", ", passes.ToArray()) + " switched off and its cuts (" + string.Join(", ", LayerCutProperties) + ") raised to " +
                    LayerCut.ToString("0.0") + " while tracing)");
            }
        }
        // The hair traced no more for LayerGrace frames (all: every one) drawn by HDRP again: its passes and its cut as before.
        private static void RestoreLayerHair(bool all)
        {
            if (layerHidden.Count == 0) return;
            var back = new List<Material>();
            foreach (var entry in layerHidden)
                if (all || entry.Key == null || !layerSeen.TryGetValue(entry.Key, out int seen) || Time.frameCount - seen > LayerGrace) back.Add(entry.Key);
            foreach (var m in back)
            {
                try
                {
                    if (m != null)
                    {
                        foreach (var pass in layerHidden[m]) m.SetShaderPassEnabled(pass, true);
                        if (layerCuts.TryGetValue(m, out var taken))
                            for (int c = 0; c < LayerCutProperties.Length; ++c) if (!float.IsNaN(taken[c])) m.SetFloat(LayerCutProperties[c], taken[c]);
                    }
                }
                catch { }
                layerHidden.Remove(m); layerCuts.Remove(m); layerSeen.Remove(m);
            }
            if (all) layerRenderers.Clear();
        }
        // For the F8 report: how a material the tracer draws as a layer is hidden from HDRP now.
        internal static string LayerState(Material m)
        {
            if (m == null || !layerHidden.ContainsKey(m)) return "";
            var on = new List<string>();
            if (m.shader != null)
                for (int p = 0; p < m.shader.passCount; ++p)
                {
                    string mode = m.shader.FindPassTagValue(p, LightMode).name;
                    if (!string.IsNullOrEmpty(mode) && m.GetShaderPassEnabled(mode) && !on.Contains(mode)) on.Add(mode);
                }
            return ", drawn by the tracer as a layer (HDRP's passes switched off: " + string.Join(", ", layerHidden[m].ToArray()) + "; on now: " +
                string.Join(", ", on.ToArray()) + "; _AlphaClip " + (m.HasProperty("_AlphaClip") ? m.GetFloat("_AlphaClip").ToString("0.##") : "none") +
                ", the game's " + GameCut(m, "_AlphaClip", 0f).ToString("0.##") + "; traced " + (Time.frameCount - (layerSeen.TryGetValue(m, out int s) ? s : 0)) + " frames ago)";
        }

        // Hair up close is two layers: an alpha-tested first pass (FirstPass_*), which the tracer finds along camera rays
        // and lights, and a transparent layer HDRP draws over it without depth, which the tracer can't find, so HDRP lit
        // it with its own lights alone: black outside dialogue, white in a dialogue light. While tracing, a renderer that
        // has both loses the transparent layer (every HDRP pass of its material but the shadow caster), and its traced
        // first pass shows; eyebrows, which are only a transparent layer, keep theirs. Put back when tracing stops.
        private static readonly Dictionary<Material, List<string>> hiddenLayers = new Dictionary<Material, List<string>>();
        private static bool hideLayers, hideLogged;
        private static readonly ShaderTagId LightMode = new ShaderTagId("LightMode");
        internal static void HideHairLayers(bool on)
        {
            if (on == hideLayers) return;
            hideLayers = on;
            if (!on) RestoreHairLayers();
        }
        private static void RestoreHairLayers()
        {
            foreach (var entry in hiddenLayers)
                try { if (entry.Key != null) foreach (var pass in entry.Value) entry.Key.SetShaderPassEnabled(pass, true); } catch { }
            hiddenLayers.Clear();
        }
        // Every HDRP pass of a material but its shadow caster, until tracing stops (RestoreHairLayers).
        private static void HideLayer(Material m)
        {
            if (m == null || m.shader == null || hiddenLayers.ContainsKey(m)) return;
            var passes = new List<string>();
            for (int p = 0; p < m.shader.passCount; ++p)
            {
                string mode = m.shader.FindPassTagValue(p, LightMode).name;
                if (string.IsNullOrEmpty(mode) || mode == "ShadowCaster" || passes.Contains(mode) || !m.GetShaderPassEnabled(mode)) continue;
                m.SetShaderPassEnabled(mode, false);
                passes.Add(mode);
            }
            hiddenLayers[m] = passes;
        }
        private static void HideTransparentHair(Material[] materials, int count)
        {
            bool firstPass = false;
            for (int s = 0; s < count; ++s)
            {
                var m = materials[s];
                if (m != null && m.shader != null && m.shader.name.IndexOf("Hair", StringComparison.OrdinalIgnoreCase) >= 0 && !SceneCapture.CharacterLeftOut(m)) firstPass = true;
            }
            if (!firstPass) return;
            for (int s = 0; s < count; ++s)
            {
                var m = materials[s];
                if (m == null || m.shader == null || hiddenLayers.ContainsKey(m)) continue;
                if (m.shader.name.IndexOf("Hair", StringComparison.OrdinalIgnoreCase) < 0 || !SceneCapture.CharacterLeftOut(m)) continue;
                var passes = new List<string>();
                for (int p = 0; p < m.shader.passCount; ++p)
                {
                    string mode = m.shader.FindPassTagValue(p, LightMode).name;
                    if (string.IsNullOrEmpty(mode) || mode == "ShadowCaster" || passes.Contains(mode) || !m.GetShaderPassEnabled(mode)) continue;
                    m.SetShaderPassEnabled(mode, false);
                    passes.Add(mode);
                }
                hiddenLayers[m] = passes;
                if (!hideLogged)
                {
                    hideLogged = true;
                    Session.Log.LogInfo("Characters: hair's transparent layer (material \"" + m.name + "\", passes " + string.Join(", ", passes.ToArray()) +
                        ") is hidden while tracing; its alpha-tested first pass shows, lit by the tracer");
                }
            }
        }

        // This frame's characters to the native side, just before the frame itself.
        internal static void Capture(Vector3 camera)
        {
            var tracer = Session.Tracer;
            if (tracer == null || !tracer.Connected) return;
            Traced = 0;
            if (!Session.Settings.Characters.Value) { RestoreLayerHair(true); RestoreRenderers(true); tracer.SetSkinned(IntPtr.Zero, 0, null, 0); return; }
            captured.Clear();
            try { CaptureCharacters(tracer, camera); }
            catch (Exception ex)
            {
                Problem(ex.GetType().Name + ": " + ex.Message);
                tracer.SetSkinned(IntPtr.Zero, 0, null, 0);
            }
            RestoreLayerHair(false);
            RestoreRenderers(false);
            if (holdOpaque && drawReports < 2 && drawReportsTotal < 16 && Time.unscaledTime >= nextDrawReport)
            {
                ++drawReports; ++drawReportsTotal;
                nextDrawReport = Time.unscaledTime + 20f;
                try { Session.Log.LogInfo("Characters: what Kandra hands HDRP for the characters nearest the camera, while tracing:" + DescribeDrawn(camera, 2)); }
                catch (Exception ex) { Session.Log.LogInfo("Characters: what Kandra hands HDRP could not be read (" + ex.GetType().Name + ": " + ex.Message + ")"); }
            }
        }

        // Hair the tracer draws as a layer, hidden from HDRP by its renderer too (2026-10-06, round 2). With every HDRP pass of its
        // material switched off and its cut raised past 1, HDRP still drew Ron's hair and beard (the harness's views: their depth
        // and normals in HDRP's buffers, the layer finding only the faint strands in front of them), while the same switches hid
        // his brows. A renderer whose every material is such hair or a shell under it (a scalp or beard mask) is taken out of
        // Kandra's culling instead: its slot's layer mask 0, so no camera and no light of HDRP draws it. Kandra still skins it
        // (its skinning runs over every vertex), so the tracer has it. The mask the game had comes back when tracing stops, and
        // after LayerGrace frames without the renderer among the traced characters. Side effects while hidden: no HDRP shadow of
        // that hair (only what HDRP still shades sees one: fog, transparents), and the game copies the face's blend shapes to a
        // beard only while Kandra calls it visible (CopyBlendshapes), so a hidden beard keeps its last mouth shape in dialogue.
        private sealed class MaskHold { internal KandraRenderer Renderer; internal uint Slot, Original; internal int Seen; }
        private static readonly Dictionary<int, MaskHold> maskHolds = new Dictionary<int, MaskHold>();
        private static readonly HashSet<int> captured = new HashSet<int>(); // renderers handed to the tracer this frame
        private static bool maskLogged;
        private static int maskAgainLogged;
        private static float nextDrawReport;
        private static int drawReports, drawReportsTotal; // a tracing session's, and the game's
        // Shells under hair: the scalp and beard masks (Kandra_Lit, a dark hair color painted on the skin).
        private static bool IsShell(Material m) => m != null &&
            (m.name.IndexOf("ScalpMask", StringComparison.OrdinalIgnoreCase) >= 0 || m.name.IndexOf("Stubble", StringComparison.OrdinalIgnoreCase) >= 0);
        private static bool LayerHairOnly(Material[] materials, int count, bool textured)
        {
            bool hair = false;
            for (int s = 0; s < count; ++s)
            {
                var m = materials[s];
                if (m == null) continue;
                if (IsShell(m)) continue;
                if (SceneCapture.IsHairLayer(m, textured)) { hair = true; continue; }
                return false;
            }
            return hair;
        }
        private static void HideRenderer(KandraRendererManager manager, KandraRenderer renderer, Material[] materials, int count)
        {
            var culling = manager.VisibilityCullingManager;
            if (culling == null || KandraRendererManager.IsInvalidId(renderer.RenderingId)) return;
            uint slot = KandraRendererManager.USlot(renderer.RenderingId);
            if (slot >= culling.layerMasks.Length) return;
            int id = renderer.GetInstanceID();
            uint now = culling.layerMasks[slot];
            if (!maskHolds.TryGetValue(id, out var hold) || hold.Slot != slot)
            {
                if (now == 0) return; // not registered for culling: nothing of the game's to take
                hold = new MaskHold { Renderer = renderer, Slot = slot, Original = now };
                maskHolds[id] = hold;
                if (!maskLogged)
                {
                    maskLogged = true;
                    var names = new List<string>();
                    for (int s = 0; s < count; ++s) if (materials[s] != null) names.Add("\"" + materials[s].name + "\"");
                    Session.Log.LogInfo("Characters: hair renderer \"" + renderer.name + "\" (" + string.Join(", ", names.ToArray()) + ") left out of Kandra's culling " +
                        "while tracing (its layer mask " + now + " to 0): HDRP draws none of it, the tracer's layer does");
                }
            }
            else if (now != 0)
            {
                // Kandra wrote the mask again (the renderer registered anew in the same slot): the game's, taken again.
                hold.Original = now;
                if (maskAgainLogged++ < 6) Session.Log.LogInfo("Characters: hair renderer \"" + renderer.name + "\" had its culling mask " + now + " again: hidden again");
            }
            hold.Seen = Time.frameCount;
            culling.layerMasks[slot] = 0u;
        }
        // What Kandra hands HDRP to draw for the characters nearest the camera (the log, a few times a tracing session): every
        // Kandra renderer under each character, whether its culling lets HDRP draw it, and per submesh the material Kandra's batch
        // renderer group registered for it (what HDRP draws) beside the one the renderer's RenderingMaterials hold (what the mod
        // switches): their passes, cut, surface and queue. To find what draws hair the mod has hidden (2026-10-06, round 2).
        private static readonly FieldInfo BrgField = typeof(KandraRendererManager).Assembly.GetType("Awaken.Kandra.Managers.SkinnedBatchRenderGroup")?
            .GetField("_brg", BindingFlags.NonPublic | BindingFlags.Instance);
        private static readonly FieldInfo BySlotField = typeof(KandraRendererManager).Assembly.GetType("Awaken.Kandra.Managers.SkinnedBatchRenderGroup")?
            .GetField("_rendererBySlot", BindingFlags.NonPublic | BindingFlags.Instance);
        private static readonly FieldInfo RenderersField = typeof(KandraRendererManager).Assembly.GetType("Awaken.Kandra.Managers.SkinnedBatchRenderGroup")?
            .GetField("_renderers", BindingFlags.NonPublic | BindingFlags.Instance);
        private static readonly string[] DrawPasses = { "DepthForwardOnly", "ForwardOnly", "MotionVectors", "TransparentDepthPrepass", "TransparentDepthPostpass",
            "TransparentBackface", "GBuffer", "DepthOnly", "ShadowCaster" };
        private static string DescribeMaterial(Material m, bool textured)
        {
            if (m == null) return "none";
            var on = new List<string>();
            foreach (var p in DrawPasses) if (m.GetShaderPassEnabled(p)) on.Add(p);
            string cut = m.HasProperty("_AlphaClip") ? "_AlphaClip " + m.GetFloat("_AlphaClip").ToString("0.##") +
                    (m.HasProperty("_DepthPrepass") ? ", _DepthPrepass " + m.GetFloat("_DepthPrepass").ToString("0.##") : "") :
                m.HasProperty("_AlphaCutoff") ? "_AlphaCutoff " + m.GetFloat("_AlphaCutoff").ToString("0.##") : "no cut";
            string traced = SceneCapture.IsHairLayer(m, textured) ? "traced as a layer" : SceneCapture.CharacterLeftOut(m) ? "left out of the trace" : "traced";
            return "\"" + m.name + "\" #" + m.GetInstanceID() + " (" + (m.shader != null ? m.shader.name : "no shader") + "; surface " +
                (m.HasProperty("_SurfaceType") ? m.GetFloat("_SurfaceType").ToString("0") : "-") + ", queue " + m.renderQueue + ", " + cut +
                "; passes on: " + (on.Count > 0 ? string.Join(" ", on.ToArray()) : "none") + "; " + traced +
                (layerHidden.ContainsKey(m) ? ", hidden by the mod" : "") + ")";
        }
        // The material Kandra's batch renderer group registered for a renderer's submesh, or null (and why, in note).
        private static unsafe Material RegisteredMaterial(KandraRendererManager manager, uint slot, int submesh, out string note)
        {
            note = "";
            var group = manager.SkinnedBatchRenderGroup;
            if (group == null || BrgField == null || BySlotField == null || RenderersField == null) { note = "Kandra's batch internals not found"; return null; }
            var brg = BrgField.GetValue(group) as BatchRendererGroup;
            var bySlot = (Awaken.Utility.LowLevel.Collections.UnsafeArray<Awaken.Utility.LowLevel.Collections.UnsafeArray<ushort>>)BySlotField.GetValue(group);
            if (brg == null || !bySlot.IsCreated || slot >= bySlot.Length) { note = "no batch slot"; return null; }
            var indices = bySlot[slot];
            if (!indices.IsCreated || (uint)submesh >= indices.Length) { note = "no batch entry for the submesh"; return null; }
            ushort entry = indices[(uint)submesh];
            object list = RenderersField.GetValue(group);
            var listType = list.GetType();
            var element = listType.GetGenericArguments()[0];
            var ptrProperty = listType.GetProperty("Ptr");
            var lengthProperty = listType.GetProperty("Length");
            if (ptrProperty == null || lengthProperty == null) { note = "Kandra's submesh list unreadable"; return null; }
            int length = (int)lengthProperty.GetValue(list);
            if (entry >= length) { note = "batch entry " + entry + " past " + length; return null; }
            byte* data = (byte*)Pointer.Unbox(ptrProperty.GetValue(list));
            int size = System.Runtime.InteropServices.Marshal.SizeOf(element);
            int offset = (int)System.Runtime.InteropServices.Marshal.OffsetOf(element, "materialID");
            var id = *(BatchMaterialID*)(data + (long)entry * size + offset);
            note = "batch entry " + entry;
            return brg.GetRegisteredMaterial(id);
        }
        // A character: the transform of the Kandra rig its renderer is posed by (characters may share a scene root).
        private static Transform CharacterRoot(KandraRenderer r)
        {
            var rig = r.rendererData.rig;
            return rig != null ? rig.transform : r.transform.root;
        }
        private static string DescribeDrawn(Vector3 camera, int count)
        {
            var manager = KandraRendererManager.Instance;
            var renderers = manager?.ActiveRenderers;
            if (manager == null || renderers == null) return " no Kandra";
            var roots = new List<(Transform root, float distance)>();
            foreach (var r in renderers)
            {
                if (r == null || !r.isActiveAndEnabled || !manager.IsRegistered(r.RenderingId)) continue;
                manager.GetBoundsAndRootBone(r.RenderingId, out float4 sphere, out float4x4 _);
                float d = Mathf.Max(0f, Vector3.Distance(camera, new Vector3(sphere.x, sphere.y, sphere.z)) - sphere.w);
                var root = CharacterRoot(r);
                int at = roots.FindIndex(x => x.root == root);
                if (at < 0) roots.Add((root, d)); else if (d < roots[at].distance) roots[at] = (root, d);
            }
            roots.Sort((a, b) => a.distance.CompareTo(b.distance));
            var sb = new System.Text.StringBuilder();
            var culling = manager.VisibilityCullingManager;
            for (int k = 0; k < roots.Count && k < count; ++k)
            {
                var root = roots[k].root;
                sb.Append("\n  character \"" + root.name + "\", " + roots[k].distance.ToString("0.0") + " m:");
                int listed = 0;
                foreach (var kr in root.GetComponentsInChildren<KandraRenderer>(true))
                {
                    if (kr == null || listed++ >= 16) continue;
                    bool registered = !KandraRendererManager.IsInvalidId(kr.RenderingId) && manager.IsRegistered(kr.RenderingId);
                    uint slot = KandraRendererManager.USlot(kr.RenderingId);
                    string mask = registered && culling != null && slot < culling.layerMasks.Length ? culling.layerMasks[slot].ToString() : "-";
                    bool held = maskHolds.TryGetValue(kr.GetInstanceID(), out var hold);
                    sb.Append("\n    Kandra \"" + kr.name + "\" " + (kr.isActiveAndEnabled ? "active" : "inactive") + (registered ? ", slot " + slot +
                        ", seen by the camera last cull: " + (manager.IsCameraVisible(kr.RenderingId) ? "yes" : "no") + ", culling mask " + mask +
                        (held ? " (the game's " + hold.Original + ", taken by the mod)" : "") : ", not registered") +
                        (captured.Contains(kr.GetInstanceID()) ? ", traced" : ", not traced") + (HairControllerType != null && kr.GetComponentInParent(HairControllerType) != null ? ", has a HairController" : ""));
                    var mats = kr.rendererData.RenderingMaterials;
                    if (mats == null) continue;
                    bool textured = characters.TryGetValue(kr.GetInstanceID(), out var known) && known.Textured;
                    for (int s = 0; s < mats.Length; ++s)
                    {
                        sb.Append("\n      [" + s + "] its material: " + DescribeMaterial(mats[s], textured));
                        if (!registered) continue;
                        Material drawn = null; string note;
                        try { drawn = RegisteredMaterial(manager, slot, s, out note); } catch (Exception ex) { note = ex.GetType().Name + ": " + ex.Message; }
                        sb.Append("\n          what Kandra registered for HDRP to draw (" + note + "): " + (drawn == null ? "none" :
                            ReferenceEquals(drawn, mats[s]) ? "the same material" : DescribeMaterial(drawn, textured)));
                    }
                }
                int unity = 0;
                foreach (var ur in root.GetComponentsInChildren<Renderer>(true))
                    if (ur != null && ur.enabled && ur.gameObject.activeInHierarchy && unity++ < 8)
                        sb.Append("\n    Unity " + ur.GetType().Name + " \"" + ur.name + "\"" + (ur.isVisible ? ", visible" : "") + ", materials " + MaterialNames(ur.sharedMaterials));
            }
            return sb.ToString();
        }
        // The game's masks back (all: every one; else those not traced for LayerGrace frames), where the renderer still holds the
        // slot the mask was taken from and nothing wrote it since.
        private static void RestoreRenderers(bool all)
        {
            if (maskHolds.Count == 0) return;
            var back = new List<int>();
            foreach (var entry in maskHolds)
                if (all || entry.Value.Renderer == null || Time.frameCount - entry.Value.Seen > LayerGrace) back.Add(entry.Key);
            if (back.Count == 0) return;
            var culling = KandraRendererManager.Instance?.VisibilityCullingManager;
            foreach (int id in back)
            {
                var hold = maskHolds[id];
                maskHolds.Remove(id);
                try
                {
                    var r = hold.Renderer;
                    if (culling == null || r == null || KandraRendererManager.IsInvalidId(r.RenderingId)) continue;
                    if (KandraRendererManager.USlot(r.RenderingId) != hold.Slot || hold.Slot >= culling.layerMasks.Length) continue;
                    if (culling.layerMasks[hold.Slot] == 0u) culling.layerMasks[hold.Slot] = hold.Original;
                }
                catch { }
            }
        }

        // For the F8 report: the characters along the crosshair's ray (within 1.5 m of it, nearest first, four at most), each
        // renderer's materials as the game draws them now (the hair color the game gave it, transparent or opaque, whether the
        // tracer hides its passes) and what the tracer made of each. A far character's hair came out grey while HDRP's was
        // colored: this names the material each of them uses.
        internal static string DescribeAlongRay(Vector3 eye, Vector3 forward)
        {
            var sb = new System.Text.StringBuilder();
            try
            {
                var manager = KandraRendererManager.Instance;
                var renderers = manager?.ActiveRenderers;
                if (manager == null || renderers == null) return "none (no Kandra renderers)";
                var near = new List<(KandraRenderer renderer, float along, float off)>();
                foreach (var r in renderers)
                {
                    if (r == null || !r.isActiveAndEnabled) continue;
                    uint slot = r.RenderingId;
                    if (!manager.IsRegistered(slot)) continue;
                    manager.GetBoundsAndRootBone(slot, out float4 sphere, out float4x4 _);
                    var centre = new Vector3(sphere.x, sphere.y, sphere.z) - eye;
                    float along = Vector3.Dot(centre, forward);
                    if (along <= 0f) continue;
                    float off = (centre - forward * along).magnitude - sphere.w;
                    if (off > 1.5f) continue;
                    near.Add((r, along, off));
                }
                near.Sort((a, b) => a.along.CompareTo(b.along));
                if (near.Count == 0) return "none within 1.5 m of the crosshair's ray";
                int shown = 0;
                var seenRoots = new List<Transform>();
                foreach (var n in near)
                {
                    if (shown >= 12) break;
                    var root = n.renderer.transform.root;
                    if (!seenRoots.Contains(root) && seenRoots.Count >= 4) continue;
                    seenRoots.Add(root);
                    ++shown;
                    var rd = n.renderer.rendererData;
                    var materials = rd.RenderingMaterials;
                    sb.Append("\n  \"" + n.renderer.name + "\" (character \"" + root.name + "\") " + n.along.ToString("0.0") + " m along the ray, " +
                        Mathf.Max(0f, n.off).ToString("0.00") + " m off it; " + (materials != null ? materials.Length : 0) + " materials:");
                    if (materials == null) continue;
                    for (int i = 0; i < materials.Length; i++)
                    {
                        var m = materials[i];
                        if (m == null) { sb.Append("\n    [" + i + "] none"); continue; }
                        sb.Append("\n    [" + i + "] \"" + m.name + "\" #" + m.GetInstanceID() + " (" + (m.shader != null ? m.shader.name : "no shader") + ")" +
                            (m.HasProperty("_DiffuseColor") ? ", _DiffuseColor " + m.GetColor("_DiffuseColor").ToString("0.00") : "") +
                            (m.HasProperty("_SurfaceType") ? (m.GetFloat("_SurfaceType") > 0.5f ? ", transparent" : ", opaque") : "") +
                            (hiddenLayers.ContainsKey(m) ? ", its passes hidden while tracing" : "") + LayerState(m) + "; traced: " + SceneCapture.DescribeCharacterMaterial(m));
                    }
                }
                // And every renderer in each of those characters, whatever draws it (Kandra or Unity's, active or not): a far
                // character's hair was drawn by HDRP while no captured renderer held it.
                foreach (var root in seenRoots)
                {
                    sb.Append("\n  every renderer of \"" + root.name + "\":");
                    int listed = 0;
                    foreach (var kr in root.GetComponentsInChildren<KandraRenderer>(true))
                    {
                        if (kr == null || listed++ >= 16) continue;
                        uint slot = kr.RenderingId;
                        var mats = kr.rendererData.RenderingMaterials;
                        sb.Append("\n    Kandra \"" + kr.name + "\" " + (kr.gameObject.activeInHierarchy ? "active" : "inactive") + (kr.enabled ? "" : ", disabled") +
                            (manager.IsRegistered(slot) ? ", registered" : ", not registered") + (mats != null ? ", materials " + MaterialNames(mats) : ""));
                    }
                    foreach (var ur in root.GetComponentsInChildren<Renderer>(true))
                    {
                        if (ur == null || listed++ >= 32) continue;
                        sb.Append("\n    " + ur.GetType().Name + " \"" + ur.name + "\" " + (ur.gameObject.activeInHierarchy ? "active" : "inactive") + (ur.enabled ? "" : ", disabled") +
                            (ur.isVisible ? ", visible" : "") + ", materials " + MaterialNames(ur.sharedMaterials));
                    }
                }
            }
            catch (Exception ex) { sb.Append(" (" + ex.GetType().Name + ": " + ex.Message + ")"); }
            return sb.ToString();
        }
        private static string MaterialNames(Material[] materials)
        {
            var parts = new List<string>();
            foreach (var m in materials)
            {
                if (m == null) { parts.Add("none"); continue; }
                parts.Add("\"" + m.name + "\"" + (m.HasProperty("_DiffuseColor") ? " " + m.GetColor("_DiffuseColor").ToString("0.00") : "") +
                    (m.HasProperty("_SurfaceType") && m.GetFloat("_SurfaceType") > 0.5f ? " transparent" : ""));
            }
            return string.Join(", ", parts.ToArray());
        }

        private static void CaptureCharacters(NativeTracer tracer, Vector3 camera)
        {
            var manager = KandraRendererManager.Instance;
            var output = manager?.SkinningManager?.OutputVerticesBuffer;
            var renderers = manager?.ActiveRenderers;
            if (manager == null || output == null || !output.IsValid() || renderers == null) { tracer.SetSkinned(IntPtr.Zero, 0, null, 0); return; }
            if (!ReferenceEquals(output, outputBuffer) || outputPointer == IntPtr.Zero) { outputBuffer = output; outputPointer = output.GetNativeBufferPtr(); }
            if (outputPointer == IntPtr.Zero) { tracer.SetSkinned(IntPtr.Zero, 0, null, 0); return; }
            candidates.Clear();
            float3 eye = new float3(camera.x, camera.y, camera.z);
            foreach (var r in renderers)
            {
                if (r == null || !r.isActiveAndEnabled) continue;
                uint slot = r.RenderingId;
                if (!manager.IsRegistered(slot)) continue;
                manager.GetBoundsAndRootBone(slot, out float4 sphere, out float4x4 _);
                float distance = math.max(0f, math.distance(eye, sphere.xyz) - sphere.w);
                if (!math.all(math.isfinite(sphere))) continue;
                candidates.Add((r, distance, sphere));
            }
            candidates.Sort((a, b) => a.distance.CompareTo(b.distance));
            if (MissingWorld.Enabled) foreach (var candidate in candidates)
                MissingWorld.Decision("kandra:" + candidate.renderer.GetInstanceID(), "CharacterCapture.MaxRenderers (256) reached before this candidate");
            instances.Clear();
            int vertices = 0;
            Transform firstPerson = FirstPersonRoot();
            foreach (var c in candidates)
            {
                if (instances.Count >= MaxRenderers) break;
                // One renderer's trouble leaves that renderer out, not the others.
                try
                {
                    if (!manager.TryGetInstanceData(c.renderer, out var data))
                    { if (MissingWorld.Enabled) MissingWorld.Decision("kandra:" + c.renderer.GetInstanceID(), "CharacterCapture.TryGetInstanceData returned false"); continue; }
                    var rd = c.renderer.rendererData;
                    var mesh = rd.mesh;
                    if (mesh == null || mesh.vertexCount == 0 || vertices + mesh.vertexCount > MaxVertices)
                    { if (MissingWorld.Enabled) MissingWorld.Decision("kandra:" + c.renderer.GetInstanceID(), mesh == null ? "CharacterCapture: null mesh" : mesh.vertexCount == 0 ? "CharacterCapture: zero vertices" : "CharacterCapture.MaxVertices (2097152): " + vertices + " already used + " + mesh.vertexCount); continue; }
                    var materials = rd.RenderingMaterials;
                    // The game draws one submesh per material (the fewer of the two); more than eight are left out.
                    int drawn = Math.Min(mesh.submeshes != null ? mesh.submeshes.Length : 1, materials != null ? materials.Length : 0);
                    if (drawn <= 0)
                    { if (MissingWorld.Enabled) MissingWorld.Decision("kandra:" + c.renderer.GetInstanceID(), "CharacterCapture: no drawn submeshes (minimum of mesh/material counts)"); continue; }
                    int n = Math.Min(drawn, MaxSubmeshes);
                    if (drawn > MaxSubmeshes && overSubmeshes.Count < 40 && overSubmeshes.Add(mesh.name))
                        Session.Log.LogInfo("Characters: \"" + mesh.name + "\" draws " + drawn + " submeshes; the tracer takes the first " + MaxSubmeshes);
                    if (holdOpaque) HoldLayersOpaque(c.renderer, materials, n);
                    var leftOut = new bool[n];
                    // (A scalp or beard mask the tracer draws under the hair keeps its triangles: SceneCapture.LayerShell.)
                    for (int s = 0; s < n; ++s) leftOut[s] = SceneCapture.CharacterLeftOut(materials[s]) && !SceneCapture.LayerShell(materials[s]);
                    if (hideLayers) HideTransparentHair(materials, n);
                    var character = Find(c.renderer, mesh, leftOut, data.sharedStartVertex);
                    if (character.Handle <= 0)
                    { if (MissingWorld.Enabled) MissingWorld.Decision("kandra:" + c.renderer.GetInstanceID(), "CharacterCapture.Find: handle " + character.Handle + ", pending " + character.Pending + ", failed " + character.Failed); continue; }
                    var instance = new NativeSkinnedInstance { Mesh = character.Handle, FirstVertex = data.instanceStartVertex, VertexCount = mesh.vertexCount, RenderingLayerMask = c.renderer.rendererData.filteringSettings.renderingLayersMask };
                    SetTransform(ref instance, float4x4.identity);
                    instance.SubmeshCount = (uint)n;
                    // Their textures where the texture coordinates arrived with the triangles, their colors otherwise. The skin of the
                    // hero's first-person body is marked: its stand-in for a head, around the camera, is passed by reflections (PT-46).
                    bool heroBody = firstPerson != null && c.renderer.transform.IsChildOf(firstPerson);
                    uint material = 0;
                    for (int s = 0; s < MaxSubmeshes; ++s)
                    {
                        if (s < n)
                        {
                            material = SceneCapture.CharacterMaterial(materials[s], character.Textured);
                            if (heroBody && IsSkin(materials[s])) material = SceneCapture.FirstPersonMaterial(material);
                        }
                        instance.SetMaterial(s, material);
                    }
                    // Its hair the tracer draws as a layer, hidden from HDRP while tracing: by its materials, and a renderer that holds
                    // nothing else (a head of hair, a beard, with their shells) by Kandra's culling as well.
                    if (holdOpaque)
                    {
                        Brows(c.renderer, materials, n, character.Textured);
                        // (A shell the tracer draws under it has no pass of HDRP's to hide: its renderer is.)
                        for (int s = 0; s < n; ++s)
                            if (!IsShell(materials[s]) && SceneCapture.IsHairLayer(materials[s], character.Textured))
                            {
                                HideLayerHair(c.renderer, materials[s]);
                            }
                    }
                    captured.Add(c.renderer.GetInstanceID());
                    if (MissingWorld.Enabled) MissingWorld.Decision("kandra:" + c.renderer.GetInstanceID(), "", instances.Count);
                    instances.Add(instance);
                    vertices += mesh.vertexCount;
                    CheckSpace(output, data.instanceStartVertex, c.sphere);
                }
                catch (Exception ex) { if (MissingWorld.Enabled) MissingWorld.Decision("kandra:" + c.renderer.GetInstanceID(), "CharacterCapture exception: " + ex.GetType().Name + " " + ex.Message); Problem(c.renderer.name + ": " + ex.GetType().Name + " " + ex.Message); }
            }
            SceneCapture.FlushMaterials();
            var list = instances.ToArray();
            // ON TRIAL (PT-92, the hidden "Character normals"): with the stride, where a vertex holds its skinned normal.
            uint stride = (uint)output.stride | (SkinnedNormalAt(output.stride) << Tgpt.SkinnedNormalShift);
            int result = tracer.SetSkinned(outputPointer, stride, list, (uint)list.Length);
            if (result != 0) { Problem("the native side refused this frame's characters (" + result + ")"); return; }
            Traced = list.Length;
        }

        // ON TRIAL (PT-92): the byte at which the game's skinning leaves a vertex's skinned normal, or 0 (none: characters are
        // shaded by their flat triangles, as before). Kandra writes each posed vertex as its CompressedVertex: a float3 position,
        // then a uint2 holding the skinned normal and the skinned tangent. Nothing here comes from a table: the byte is that
        // field's own offset in the game's type, and the packing the native side reads (a unit vector as an octahedral map, its
        // two coordinates half floats from 0 to 1, x in the low 16 bits: pt_skin.hlsl) is checked once against the game's own
        // encoder with six directions. After a game update that moves the field or packs it another way, characters go back to
        // flat triangles and the log says so once. The switch is read every frame; the look-up is made once.
        private static int normalAt = -1; // not looked for yet
        private static uint SkinnedNormalAt(int stride)
        {
            if (Session.Settings.CharacterNormals.Value <= 0) return 0;
            if (normalAt < 0)
            {
                try { normalAt = FindSkinnedNormal(stride); }
                catch (Exception ex)
                {
                    normalAt = 0;
                    Session.Log.LogWarning("Characters: the game's skinned normals were not found (" + ex.GetType().Name + ": " + ex.Message + "); characters are shaded by their flat triangles");
                }
            }
            return (uint)normalAt;
        }
        // (On its own, and never inlined: if the game's type is gone, the failure to load it is the caller's to catch.)
        [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]
        private static int FindSkinnedNormal(int stride)
        {
            var type = typeof(Awaken.Kandra.Data.CompressedVertex);
            var field = type.GetField("normalAndTangent", BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance);
            int size = Unity.Collections.LowLevel.Unsafe.UnsafeUtility.SizeOf(type);
            int at = field != null && field.FieldType == typeof(uint2) ? Unity.Collections.LowLevel.Unsafe.UnsafeUtility.GetFieldOffset(field) : -1;
            if (size != stride || at < 12 || at % 4 != 0 || at + 4 > stride || at > 255)
            {
                Session.Log.LogWarning("Characters: the game's skinned vertex is not laid out as expected (" + size + " bytes against the buffer's " + stride +
                    ", its normal at byte " + at + "); characters are shaded by their flat triangles");
                return 0;
            }
            var probes = new[] { new float3(0f, 1f, 0f), new float3(1f, 0f, 0f), new float3(0f, 0f, 1f), new float3(0f, 0f, -1f),
                math.normalize(new float3(0.3f, -0.5f, -0.8f)), math.normalize(new float3(-0.6f, 0.2f, 0.77f)) };
            float worst = 0f;
            foreach (var n in probes)
            {
                var vertex = new Awaken.Kandra.Data.CompressedVertex(float3.zero, n, new float3(1f, 0f, 0f));
                worst = math.max(worst, math.distance(n, DecodeSkinnedNormal(vertex.normalAndTangent.x)));
            }
            if (!(worst < 0.01f))
            {
                Session.Log.LogWarning("Characters: the game packs its skinned normals another way than the tracer reads them (off by " + worst.ToString("0.000") +
                    " at worst); characters are shaded by their flat triangles");
                return 0;
            }
            Session.Log.LogInfo("Characters: the game's skinned normals are taken with their positions (byte " + at + " of its " + size +
                "-byte vertices; its packing read back to within " + worst.ToString("0.0000") + "): characters are shaded smooth (on trial)");
            return at;
        }
        // The game's packed normal as the native side decodes it (pt_skin.hlsl skinned_normal).
        private static float3 DecodeSkinnedNormal(uint packed)
        {
            float2 e = math.saturate(math.f16tof32(new uint2(packed & 0xFFFFu, packed >> 16))) * 2f - 1f;
            var n = new float3(e.x, e.y, 1f - math.abs(e.x) - math.abs(e.y));
            float t = math.saturate(-n.z);
            n.x += n.x >= 0f ? -t : t;
            n.y += n.y >= 0f ? -t : t;
            return math.normalize(n);
        }

        // The hero's first-person rig (VHeroController.fppParent), whose body is the game's first-person one: no head of its own
        // (the face, hair and eyes come only with the third-person body), a stand-in around the camera instead. Null in third
        // person, before the hero is made, or if the game's types moved (then once in the log, and nothing is marked).
        private static bool firstPersonFailed;
        private static Transform FirstPersonRoot()
        {
            if (firstPersonFailed) return null;
            try
            {
                var controller = Awaken.TG.Main.Heroes.Hero.Current?.VHeroController;
                var root = controller == null ? null : controller.fppParent;
                return root == null ? null : root.transform;
            }
            catch (Exception ex) { firstPersonFailed = true; Problem("the hero's first-person rig: " + ex.GetType().Name + " " + ex.Message); return null; }
        }
        private static bool IsSkin(Material m) => m != null && m.shader != null && m.shader.name.IndexOf("Skin", StringComparison.OrdinalIgnoreCase) >= 0;

        // The renderer's triangles and texture coordinates, registered once both have been read back (without the
        // texture coordinates, if those cannot be read). Submeshes left out of the traced scene (transparent parts)
        // get no triangles.
        private sealed class Reads { internal int Waiting; internal bool Failed; internal uint[] Indices, SubmeshStarts; internal float[] Uvs; }
        private static Character Find(KandraRenderer renderer, KandraMesh mesh, bool[] leftOut, uint sharedStart)
        {
            int submeshes = leftOut.Length;
            int id = renderer.GetInstanceID();
            var rendering = renderer.rendererData.RenderingMesh;
            long key = ((long)mesh.GetInstanceID() << 32) ^ rendering.IndexStart(0);
            for (int s = 0; s < submeshes; ++s) if (leftOut[s]) key ^= 1L << (24 + s);
            if (!characters.TryGetValue(id, out var character) || character.Key != key)
            {
                character = new Character { Key = key };
                characters[id] = character;
            }
            if (character.Handle > 0 || character.Pending || character.Failed) return character;
            var manager = KandraRendererManager.Instance;
            var indices = IndicesBufferField?.GetValue(manager.MeshBroker) as GraphicsBuffer;
            if (indices == null || !indices.IsValid()) { character.Failed = true; Problem("the game's index buffer was not found"); return character; }
            uint first = uint.MaxValue, end = 0;
            var starts = new uint[submeshes]; var counts = new uint[submeshes];
            for (uint s = 0; s < submeshes; ++s)
            {
                starts[s] = rendering.IndexStart(s); counts[s] = leftOut[s] ? 0u : rendering.IndexCount(s);
                if (counts[s] == 0) continue;
                first = Math.Min(first, starts[s]); end = Math.Max(end, starts[s] + counts[s]);
            }
            int stride = indices.stride;
            if (end <= first || (stride != 2 && stride != 4) || (long)end * stride > (long)indices.count * indices.stride) { character.Failed = true; return character; }
            character.Pending = true; ++Reading;
            ushort vertexCount = mesh.vertexCount;
            int requested = generation;
            var reads = new Reads { Waiting = 1 };
            void Finish()
            {
                if (--reads.Waiting > 0) return;
                character.Pending = false;
                if (requested != generation) return; // the scene was cleared meanwhile
                if (Reading > 0) --Reading;
                if (reads.Failed || reads.Indices == null || Session.Tracer == null || !Session.Tracer.Connected) { character.Failed = true; return; }
                character.Handle = Session.Tracer.RegisterSkinnedMesh(key, reads.Indices, vertexCount, reads.SubmeshStarts, reads.Uvs);
                character.Textured = character.Handle > 0 && reads.Uvs != null;
                if (character.Handle <= 0) character.Failed = true;
            }
            // Texture coordinates: the mesh's original vertices, at its place in the game's shared vertex buffers.
            var extra = AdditionalVerticesField?.GetValue(manager.MeshManager) as GraphicsBuffer;
            if (extra != null && extra.IsValid() && extra.stride >= 4 && extra.stride % 4 == 0 && (long)sharedStart + vertexCount <= extra.count)
            {
                ++reads.Waiting;
                int extraStride = extra.stride;
                AsyncGPUReadback.Request(extra, vertexCount * extraStride, (int)sharedStart * extraStride, request =>
                {
                    if (!request.hasError)
                    {
                        var words = request.GetData<uint>();
                        var uvs = new float[vertexCount * 2];
                        int step = extraStride / 4;
                        for (int v = 0; v < vertexCount; ++v)
                        {
                            uint packed = words[v * step];
                            float u = Mathf.HalfToFloat((ushort)(packed & 0xFFFFu)), w = Mathf.HalfToFloat((ushort)(packed >> 16));
                            uvs[v * 2] = float.IsNaN(u) || float.IsInfinity(u) ? 0f : u;
                            uvs[v * 2 + 1] = float.IsNaN(w) || float.IsInfinity(w) ? 0f : w;
                        }
                        reads.Uvs = uvs;
                    }
                    Finish();
                });
            }
            AsyncGPUReadback.Request(indices, (int)(end - first) * stride, (int)first * stride, request =>
            {
                if (request.hasError) reads.Failed = true;
                else
                {
                    try { Decode(request, stride, first, starts, counts, vertexCount, reads); }
                    catch (Exception ex) { reads.Failed = true; Problem("triangles: " + ex.Message); }
                }
                Finish();
            });
            return character;
        }

        private static void Decode(AsyncGPUReadbackRequest request, int stride, uint first, uint[] starts, uint[] counts, ushort vertexCount, Reads reads)
        {
            var list = new List<uint>(); var submeshStarts = new uint[starts.Length];
            var shorts = stride == 2 ? request.GetData<ushort>() : default;
            var ints = stride == 4 ? request.GetData<uint>() : default;
            for (int s = 0; s < starts.Length; ++s)
            {
                submeshStarts[s] = (uint)list.Count;
                uint count = counts[s] - counts[s] % 3;
                for (uint i = 0; i < count; ++i)
                {
                    int at = (int)(starts[s] - first + i);
                    uint index = stride == 2 ? shorts[at] : ints[at];
                    if (index >= vertexCount) throw new InvalidOperationException("an index (" + index + ") beyond the renderer's " + vertexCount + " vertices");
                    list.Add(index);
                }
            }
            if (list.Count < 3) { reads.Failed = true; return; }
            reads.Indices = list.ToArray(); reads.SubmeshStarts = submeshStarts;
        }

        // Once, for the log: a few vertices of a renderer that is already traced, against its world bounding sphere.
        // A readback of vertices not yet skinned (all zero) is not an answer; it is asked again later.
        private static void CheckSpace(GraphicsBuffer output, uint firstVertex, float4 sphere)
        {
            if (spaceChecked || Time.unscaledTime < nextSpaceCheck) return;
            nextSpaceCheck = Time.unscaledTime + 5f;
            int stride = output.stride, count = 32;
            if (stride < 12 || stride % 4 != 0 || (long)(firstVertex + count) * stride > (long)output.count * output.stride) return;
            AsyncGPUReadback.Request(output, count * stride, (int)firstVertex * stride, request =>
            {
                if (request.hasError || spaceChecked) return;
                var floats = request.GetData<float>();
                float3 mean = 0; int n = 0;
                for (int v = 0; v < count; ++v)
                {
                    var p = new float3(floats[v * stride / 4], floats[v * stride / 4 + 1], floats[v * stride / 4 + 2]);
                    if (!math.all(math.isfinite(p)) || math.lengthsq(p) < 1e-8f) continue;
                    mean += p; ++n;
                }
                if (n < count / 2) return;
                mean /= n;
                spaceChecked = true;
                float off = math.distance(mean, sphere.xyz);
                if (off <= sphere.w * 1.5f + 0.5f)
                    Session.Log.LogInfo("Characters: their traced vertices sit where the game draws them (" + off.ToString("0.00") + " m from the bounding sphere's centre, radius " + sphere.w.ToString("0.00") + " m)");
                else
                    Session.Log.LogWarning("Characters: their traced vertices are " + off.ToString("0.00") + " m from the centre of where the game draws them (radius " + sphere.w.ToString("0.00") + " m); they may be traced in the wrong place");
            });
        }

        private static void SetTransform(ref NativeSkinnedInstance instance, float4x4 m)
        {
            instance.T0 = m.c0.x; instance.T1 = m.c1.x; instance.T2 = m.c2.x; instance.T3 = m.c3.x;
            instance.T4 = m.c0.y; instance.T5 = m.c1.y; instance.T6 = m.c2.y; instance.T7 = m.c3.y;
            instance.T8 = m.c0.z; instance.T9 = m.c1.z; instance.T10 = m.c2.z; instance.T11 = m.c3.z;
        }
        private static void Problem(string what)
        {
            if (what == lastProblem) return;
            lastProblem = what;
            Session.Log.LogWarning("A character is left out of the traced scene: " + what);
        }
    }
}
