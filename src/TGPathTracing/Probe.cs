// SPDX-License-Identifier: MIT
// What the game has under the crosshair, for finding out why a surface is lit or shadowed wrongly: the physics
// ray's hit, every GameObject renderer, Drake entity and Medusa instance around that point (and whether the scene
// capture takes it: its shadow mode, LOD, loading), and the lights that reach it, strongest first, each with the
// first collider between the point and the light (the game's own geometry, not the traced scene: a light the
// tracer lets through a wall the colliders close shows as blocked here). One report per key press, in the log.
using System.Collections.Generic;
using System.Text;
using UnityEngine;
using UnityEngine.Rendering;
namespace TGPathTracing
{
    internal static class Probe
    {
        private const float Margin = 0.15f; // metres around the point that count as "at" it
        private const int MaxLines = 14;

        internal static void Report() { Session.Log.LogInfo(Text(8)); }

        // The report, with up to maxLights lights reaching the point.
        internal static string Text(int maxLights)
        {
            var cam = Camera.main;
            if (cam == null) return "Probe: no camera.";
            var eye = cam.transform.position;
            var ray = new Ray(eye, cam.transform.forward);
            if (!Physics.Raycast(ray, out var hit, 300f, ~0, QueryTriggerInteraction.Ignore)) return "Probe: no collider under the crosshair within 300 m.";
            var point = hit.point;
            var sb = new StringBuilder();
            sb.Append("Probe at (" + point.x.ToString("0.00") + ", " + point.y.ToString("0.00") + ", " + point.z.ToString("0.00") + "), " + hit.distance.ToString("0.0") +
                " m away: collider \"" + PathOf(hit.collider.transform) + "\" (layer " + LayerMask.LayerToName(hit.collider.gameObject.layer) + ")");
            // GameObject renderers there.
            int objects = 0, cullingMask = cam.cullingMask;
            foreach (var r in Object.FindObjectsByType<MeshRenderer>(FindObjectsSortMode.None))
            {
                if (r == null || r.bounds.SqrDistance(point) > Margin * Margin) continue;
                if (++objects > MaxLines) continue;
                var filter = r.GetComponent<MeshFilter>();
                sb.Append("\n  object \"" + PathOf(r.transform) + "\", mesh \"" + (filter != null && filter.sharedMesh != null ? filter.sharedMesh.name : "none") + "\", " + r.shadowCastingMode +
                    (r.enabled && r.gameObject.activeInHierarchy ? "" : ", disabled") + (r.forceRenderingOff ? ", rendering forced off" : "") +
                    ((cullingMask & (1 << r.gameObject.layer)) == 0 ? ", layer " + LayerMask.LayerToName(r.gameObject.layer) + " (the camera leaves it out)" : "") +
                    (r.isPartOfStaticBatch ? ", statically batched (not traced)" : ""));
            }
            sb.Append("\n  objects there: " + objects);
            SceneCapture.ProbeDrake(point, eye, sb, Margin, MaxLines);
            SceneCapture.ProbeMedusa(point, eye, sb, Margin, MaxLines);
            Lights(point, hit.normal, sb, maxLights);
            return sb.ToString();
        }

        private static void Lights(Vector3 point, Vector3 normal, StringBuilder sb, int maxLights)
        {
            var origin = point + normal * 0.05f;
            var reaching = new List<(Light light, float strength)>();
            foreach (var light in SceneLights.AllLights())
            {
                if (light == null || !light.isActiveAndEnabled || light.intensity <= 0f) continue;
                if (light.type == LightType.Directional) continue;
                var to = light.transform.position - point;
                float d = to.magnitude;
                if (d > (SceneLights.IsPlacedLight(light) ? light.range : SceneLights.Reach(light, light.GetComponent<UnityEngine.Rendering.HighDefinition.HDAdditionalLightData>()))) continue;
                float facing = Vector3.Dot(normal, to / Mathf.Max(d, 1e-4f));
                if (facing <= 0f) continue;
                reaching.Add((light, light.intensity * facing / Mathf.Max(d * d, 0.01f)));
            }
            reaching.Sort((a, b) => b.strength.CompareTo(a.strength));
            sb.Append("\n  lights reaching it (strength = intensity x cosine / distance squared, strongest first): " + reaching.Count);
            for (int i = 0; i < reaching.Count && i < maxLights; ++i)
            {
                var (light, strength) = reaching[i];
                var p = light.transform.position;
                bool blocked = Physics.Linecast(origin, p, out var block, ~0, QueryTriggerInteraction.Ignore) && (block.point - p).sqrMagnitude > 0.3f * 0.3f;
                sb.Append("\n    \"" + PathOf(light.transform) + "\" " + light.type + " " + (p - point).magnitude.ToString("0.0") + " m, strength " + strength.ToString("0.###") +
                    (light.shadows == LightShadows.None ? ", casts no shadows in the game" : "") +
                    (SceneLights.IsPlacedLight(light) ? ", left out of the traced scene (placed to fake light)" : ", traced") +
                    (blocked ? ", blocked by the collider \"" + PathOf(block.collider.transform) + "\"" : ", nothing in between"));
            }
            if (SceneLights.Sun.Valid)
            {
                var toSun = SceneLights.Sun.Direction;
                bool facing = Vector3.Dot(normal, toSun) > 0f;
                bool blocked = Physics.Raycast(origin, toSun, out var block, 500f, ~0, QueryTriggerInteraction.Ignore);
                sb.Append("\n  sun: " + (facing ? "faces it" : "faces away from it") + (blocked ? ", blocked by the collider \"" + PathOf(block.collider.transform) + "\" at " + block.distance.ToString("0.0") + " m" : ", open to it"));
            }
        }

        internal static string PathOf(Transform t)
        {
            var parts = new List<string>();
            for (int i = 0; t != null && i < 4; ++i, t = t.parent) parts.Add(t.name);
            parts.Reverse();
            return string.Join("/", parts.ToArray());
        }
    }
}
