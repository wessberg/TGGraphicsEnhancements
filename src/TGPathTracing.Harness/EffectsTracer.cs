// SPDX-License-Identifier: MIT
// Diagnostics of the installed tracer, without asking it to scan/register/change its caches.
using System;
using System.Collections;
using Newtonsoft.Json.Linq;
using UnityEngine;
using UnityEngine.Rendering.HighDefinition;
using R = TGPathTracing.Harness.EffectsRead;

namespace TGPathTracing.Harness
{
    internal static class EffectsTracer
    {
        private static Type Type(string name) => R.Type("TGPathTracing." + name);
        private static object Cache(string type, string field, int id)
        {
            var cache = R.Get(Type(type), field) as IDictionary;
            if (cache == null || !cache.Contains(id)) throw new InvalidOperationException(type + "." + field + " has no record for instance " + id);
            return cache[id];
        }
        internal static JToken Material(Material m)
        {
            return R.Try(() =>
            {
                if (m == null) return new JObject { ["rejected"] = false, ["reason"] = "null material uses neutral material" };
                string shader = m.shader != null ? m.shader.name : "";
                // Exactly the early rejection gates of SceneCapture.ReadMaterial, called by MaterialIndex.
                // LayerBrow and IsGlass are pure predicates; MaterialIndex/ReadMaterial allocate resources and must NEVER be called here.
                bool brow = (bool)R.Call(Type("SceneCapture"), "LayerBrow", m);
                bool unlit = shader.IndexOf("Unlit", StringComparison.OrdinalIgnoreCase) >= 0;
                bool noRender = shader.IndexOf("NoRender", StringComparison.OrdinalIgnoreCase) >= 0;
                bool transparent = !brow && ((m.HasProperty("_SurfaceType") && m.GetFloat("_SurfaceType") > 0.5f) || m.renderQueue >= 2750 || noRender);
                bool cameraOnly = unlit && !transparent && shader == "HDRP/Unlit";
                bool glass = transparent && !unlit && (bool)R.Call(Type("GlassFinish"), "IsGlass", m) && !noRender;
                bool water = m.FindPass("WaterGBuffer") >= 0;
                bool rejected = water || (transparent && !glass) || (unlit && !cameraOnly);
                return new JObject
                {
                    ["rejected"] = rejected, ["transparent"] = transparent, ["unlit"] = unlit,
                    ["reason"] = water ? "WaterGBuffer pass: ReadMaterial returns Invisible" :
                        rejected ? "ReadMaterial returns Invisible: " + (transparent && !glass ? "transparent without glass exception; " : "") + (unlit && !cameraOnly ? "unlit without HDRP opaque Unlit exception" : "") :
                        cameraOnly ? "accepted as HDRP/Unlit camera-only surface" : glass ? "accepted glass exception" : brow ? "accepted LayerBrow exception" : "passes material gates; renderer/geometry capture is separate",
                    ["cachedMaterialIndex"] = R.Try(() => Cache("SceneCapture", "materials", m.GetInstanceID())),
                    ["evidence"] = "ReadMaterial gates in harness source; pure predicates from installed plugin. Cached index is measured, gate prediction is not proof of geometry capture."
                };
            });
        }
        internal static JToken Effect(Renderer r)
        {
            var result = new JObject
            {
                ["geometry"] = r is MeshRenderer ? R.Unknown("a material entry does not establish renderer capture") : "not captured: SceneCapture has no particle, VFX, trail or line adapter",
                ["capturedParticleCount"] = r is MeshRenderer ? (JToken)"not applicable" : 0,
                ["missingAttributes"] = "No traced particle positions, sizes, colours, lifetimes, flipbooks, ribbons or volume density",
                ["glowMaterials"] = new JArray()
            };
            if (r != null) foreach (var m in r.sharedMaterials) if (m != null)
                ((JArray)result["glowMaterials"]).Add(new JObject { ["materialId"] = m.GetInstanceID(), ["cachedGlowDetection"] = R.Try(() =>
                {
                    object entry = Cache("SceneLights", "effectMaterials", m.GetInstanceID());
                    return R.Members(entry, "Item1", "Item2");
                }) });
            result["note"] = "Glow recognition is evidence used to choose source lights; it is not particle geometry or emitted-shape capture. No cache entry means unknown, not false.";
            return result;
        }
        internal static JToken Light(Light light, bool traced)
        {
            return R.Try(() =>
            {
                int id = light.GetInstanceID();
                var hd = light.GetComponent<HDAdditionalLightData>();
                var r = new JObject { ["tracerCompositing"] = traced,
                    ["cachedClassification"] = R.Try(() => R.Members(Cache("SceneLights", "placedCache", id), "placed", "effect", "glow", "noEmitter", "parent", "kind", "distance", "thing")),
                    ["cachedNameEvidence"] = R.Try(() => R.Members(Cache("SceneLights", "namedCache", id), "placed", "flame", "kept", "kind", "parent")),
                    ["originalHdrpState"] = R.Try(() => R.Members(Cache("SceneLights", "originals", id), "Item2", "Item3")),
                    ["selectedSlot"] = R.Try(() => Cache("SceneLights", "slots", id)),
                    ["status"] = R.Unknown("no observed selection or drop decision") };
                if (r["cachedClassification"] is JObject classification)
                    classification["thing"] = R.Try(() => R.Members(R.Get(Cache("SceneLights", "placedCache", id), "thing"), "Name", "Key", "Kind", "Claimed", "X", "Y", "Z", "HalfX", "HalfY", "HalfZ", "R", "G", "B"));
                var slots = R.Get(Type("SceneLights"), "slots") as IDictionary;
                if (!traced) { r["status"] = "off"; r["reason"] = "tracer not compositing; cached decisions may precede this scene"; return r; }
                if (slots != null && slots.Contains(id))
                {
                    int slot = (int)slots[id];
                    r["status"] = "selected"; r["reason"] = "instance ID in SceneLights.slots";
                    r["submittedLight"] = R.Try(() => R.Members(((Array)R.Get(Type("SceneLights"), "buffer")).GetValue(slot),
                        "Id", "Type", "PositionX", "PositionY", "PositionZ", "ColorR", "ColorG", "ColorB", "Range", "Radius", "Flags"));
                }
                else if (!light.isActiveAndEnabled) { r["status"] = "dropped"; r["reason"] = "inactive or disabled"; }
                else if (light.type != LightType.Point && light.type != LightType.Spot)
                { r["status"] = light.type == LightType.Directional ? R.Unknown("directional uses separate Sun record, not local slots") : "dropped"; r["reason"] = "local-light capture accepts only Point and Spot"; }
                else if (hd != null && !hd.affectDiffuse) { r["status"] = "dropped"; r["reason"] = "LightsWorld: affectDiffuse false"; }
                else if (r["cachedClassification"] is JObject cls && (bool?)cls["placed"] == true &&
                    (int?)cls["parent"] == (light.transform.parent != null ? light.transform.parent.GetInstanceID() : 0))
                {
                    r["status"] = "dropped"; r["reason"] = (bool?)cls["noEmitter"] == true ? "cached classification: no emitter at named source" : "cached classification: placed fill/fake light";
                }
                else if (light.intensity <= 0f && light.name.IndexOf("TGConversationLightingOverhaul", StringComparison.OrdinalIgnoreCase) < 0)
                { r["status"] = "dropped"; r["reason"] = "non-positive intensity (not the retained dialogue light)"; }
                else r["reason"] = R.Unknown("not in slots; light-layer gate, effect reach, unscanned/new light or changed runtime implementation; no classifier was invoked");
                return r;
            });
        }
    }
}
