// SPDX-License-Identifier: MIT
// PT-14: Caustics must use traced physical surfaces and light transport.
using System;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.HighDefinition;
namespace TGPathTracing.Harness
{
    internal static class Lantern
    {
        // The game's own big torch (Light_Torch_Big_01_Lit/PointLight_Ambience in the mineral cave, the state audit of
        // 2026-10-07): 440,872 cd at 2400 K, range 12.1 m.
        private const float Candela = 440872f, Kelvin = 2400f, Range = 12.1f;
        private const float GlassRadius = 0.04f;
        // The shade: a cone over the glass, its tip 7 cm above the glass's middle and its rim 3 cm below it, 25 cm out. No
        // light leaves above 83 degrees from straight down, and the light is a spot of that cone.
        private const float ShadeRim = 0.25f, ShadeHeight = 0.10f, ShadeTip = 0.07f, ShadeAngle = 166f, ShadeInnerPercent = 94f;
        private static GameObject root, glass, shade;
        private static Light light;
        private static HDAdditionalLightData data;
        private static Mesh glassMesh, shadeMesh;
        private static Material glassMaterial, shadeMaterial;
        private static Vector3 at;
        private static float height, ahead;
        private static string water = "";

        internal static bool Stands => root != null;

        // The lantern: over the water seen at the screen's point (across, down, as shares of the screen from its top left), `over`
        // metres above it: the place is picked on a picture of the scene, and the water's level is HDRP's own. Stood once; a
        // later call only puts the shade on or takes it off, so the light and its glass stay the same objects to the tracer.
        // Returns what was done, for the run's log.
        internal static string Place(float across, float down, float over, bool shaded)
        {
            var camera = Camera.main;
            if (root == null)
            {
                if (camera == null) return "no lantern: no camera";
                var shader = Shader.Find("HDRP/Lit");
                if (shader == null) return "no lantern: the game has no HDRP/Lit shader";
                if (!WaterAt(camera.ViewportPointToRay(new Vector3(across, 1f - down, 0f)), out Vector3 on, out water))
                    return "no lantern: no HDRP water surface is seen at " + across.ToString("0.00") + " across, " + down.ToString("0.00") + " down";
                at = on + Vector3.up * over;
                height = over;
                ahead = Vector3.ProjectOnPlane(at - camera.transform.position, Vector3.up).magnitude;
                Build(shader);
            }
            light.type = shaded ? LightType.Spot : LightType.Point;
            root.transform.SetPositionAndRotation(at, shaded ? Quaternion.LookRotation(Vector3.down, Vector3.forward) : Quaternion.identity);
            if (shaded)
            {
                light.spotAngle = ShadeAngle;
                data.innerSpotPercent = ShadeInnerPercent;
            }
            shade.SetActive(shaded);
            string screen = "";
            if (camera != null)
            {
                Vector3 v = camera.WorldToViewportPoint(at);
                screen = v.z > 0f ? ", on screen at " + v.x.ToString("0.00") + " across, " + (1f - v.y).ToString("0.00") + " down" : ", behind the camera";
            }
            return "a lantern " + (shaded ? "under its shade" : "with no shade") + " at (" + at.x.ToString("0.00") + ", " + at.y.ToString("0.00") + ", " + at.z.ToString("0.00") + "), " +
                ahead.ToString("0.0") + " m ahead" + screen + ": " + Candela.ToString("0") + " cd at " + Kelvin.ToString("0") + " K, range " + Range.ToString("0.0") + " m; " +
                height.ToString("0.00") + " m above the water \"" + water + "\" (level " + (at.y - height).ToString("0.00") + ")";
        }

        // Where a ray from the camera meets HDRP's water: the nearest of the water surfaces' levels it crosses over that
        // surface (an endless sea anywhere, a quad within its sides, a surface with meshes of its own within their bounds).
        private static bool WaterAt(Ray ray, out Vector3 point, out string name)
        {
            point = Vector3.zero; name = "";
            if (ray.direction.y >= -1e-4f) return false;
            float nearest = float.MaxValue;
            foreach (var surface in UnityEngine.Object.FindObjectsByType<WaterSurface>(FindObjectsSortMode.None))
            {
                if (surface == null || !surface.isActiveAndEnabled) continue;
                var t = surface.transform;
                if (surface.geometryType == WaterGeometryType.Custom && surface.meshRenderers != null && surface.meshRenderers.Count > 0)
                {
                    foreach (var renderer in surface.meshRenderers)
                    {
                        if (renderer == null) continue;
                        Bounds b = renderer.bounds;
                        float along = (ray.origin.y - b.center.y) / -ray.direction.y;
                        Vector3 p = ray.origin + ray.direction * along;
                        if (along <= 0f || along >= nearest || p.x < b.min.x || p.x > b.max.x || p.z < b.min.z || p.z > b.max.z) continue;
                        nearest = along; point = p; name = surface.name;
                    }
                    continue;
                }
                float level = t.position.y;
                float reach = (ray.origin.y - level) / -ray.direction.y;
                if (reach <= 0f || reach >= nearest) continue;
                Vector3 hit = ray.origin + ray.direction * reach;
                if (surface.geometryType != WaterGeometryType.Infinite)
                {
                    // A quad: one unit a side in its own space, its size the transform's scale.
                    Vector3 local = t.InverseTransformPoint(hit);
                    if (Mathf.Abs(local.x) > 0.5f || Mathf.Abs(local.z) > 0.5f) continue;
                }
                nearest = reach; point = hit; name = surface.name;
            }
            return nearest < float.MaxValue;
        }

        private static void Build(Shader shader)
        {
            // The name says what it is: TG Path Tracing keeps a light for a flame or a lamp in its names (and then asks for
            // the glowing thing at it, the glass below).
            // No HideFlags.DontSave on the lantern's objects: Object.FindObjectsByType, which the tracer finds the scene's
            // renderers with, leaves such objects out. The first run's glass was never traced, so no glowing thing stood at
            // the light and the tracer left it out as fill: the lantern lit nothing (run 20261007-103925). Nothing is saved
            // during a run, and Remove destroys them.
            root = new GameObject("TGPT harness lantern");
            root.transform.position = at;
            light = root.AddComponent<Light>();
            data = root.AddComponent<HDAdditionalLightData>();
            light.type = LightType.Point;
            light.lightUnit = LightUnit.Candela;
            light.intensity = Candela;
            light.color = Color.white;
            light.useColorTemperature = true;
            light.colorTemperature = Kelvin;
            light.range = Range;
            light.shadows = LightShadows.Soft;
            data.shapeRadius = GlassRadius;
            // As the game's own torches and lanterns: they light no fog themselves.
            data.volumetricDimmer = 0f;

            // The glass: a ball that glows with the light's own colour, as bright as a ball of its size must be to send out
            // the light's candela (luminance = intensity / the ball's disc, pi r^2), in nits as HDRP's emissive colour is.
            glassMesh = Ball(GlassRadius, 20, 12);
            Color glow = Mathf.CorrelatedColorTemperatureToRGB(Kelvin) * (Candela / (Mathf.PI * GlassRadius * GlassRadius));
            glassMaterial = new Material(shader) { name = "TGPT harness lantern glass", hideFlags = HideFlags.DontSave };
            glassMaterial.SetColor("_BaseColor", new Color(0.9f, 0.85f, 0.7f, 1f));
            glassMaterial.SetFloat("_Smoothness", 0.8f);
            glassMaterial.SetColor("_EmissiveColor", new Color(glow.r, glow.g, glow.b, 1f));
            Validate(glassMaterial);
            // It casts no shadow in HDRP's shadow map of the light inside it.
            glass = Part("TGPT harness lantern glass", glassMesh, glassMaterial, ShadowCastingMode.Off);

            shadeMesh = Cone(ShadeRim, ShadeHeight, ShadeTip, 32);
            shadeMaterial = new Material(shader) { name = "TGPT harness lantern shade", hideFlags = HideFlags.DontSave };
            shadeMaterial.SetColor("_BaseColor", new Color(0.08f, 0.07f, 0.06f, 1f));
            shadeMaterial.SetFloat("_Metallic", 1f);
            shadeMaterial.SetFloat("_Smoothness", 0.35f);
            Validate(shadeMaterial);
            shade = Part("TGPT harness lantern shade", shadeMesh, shadeMaterial, ShadowCastingMode.On);
            shade.SetActive(false);
        }

        // Beside the light, not under it: the light turns to point down under the shade, its parts stay upright.
        private static GameObject Part(string name, Mesh mesh, Material material, ShadowCastingMode shadows)
        {
            var part = new GameObject(name);
            part.transform.position = at;
            part.AddComponent<MeshFilter>().sharedMesh = mesh;
            var renderer = part.AddComponent<MeshRenderer>();
            renderer.sharedMaterial = material;
            renderer.shadowCastingMode = shadows;
            return part;
        }

        // HDRP's own setup of a material's keywords and passes (HDMaterial.ValidateMaterial), where this HDRP has it.
        private static void Validate(Material m)
        {
            try
            {
                var type = Type.GetType("UnityEngine.Rendering.HighDefinition.HDMaterial, Unity.RenderPipelines.HighDefinition.Runtime");
                var method = type != null ? type.GetMethod("ValidateMaterial", new[] { typeof(Material) }) : null;
                if (method != null) method.Invoke(null, new object[] { m });
            }
            catch { }
        }

        // A ball round the origin: rings from the top down, each going round as seen from above with +x first. Its triangles
        // are clockwise seen from outside, as Unity's front faces are.
        private static Mesh Ball(float radius, int around, int down)
        {
            int ring = around + 1;
            var vertices = new Vector3[ring * (down + 1)];
            var normals = new Vector3[vertices.Length];
            var tangents = new Vector4[vertices.Length];
            var uv = new Vector2[vertices.Length];
            for (int i = 0; i <= down; i++)
            {
                float theta = Mathf.PI * i / down;
                for (int j = 0; j <= around; j++)
                {
                    float phi = 2f * Mathf.PI * j / around;
                    var n = new Vector3(Mathf.Sin(theta) * Mathf.Cos(phi), Mathf.Cos(theta), Mathf.Sin(theta) * Mathf.Sin(phi));
                    int k = i * ring + j;
                    vertices[k] = n * radius; normals[k] = n;
                    tangents[k] = new Vector4(-Mathf.Sin(phi), 0f, Mathf.Cos(phi), -1f);
                    uv[k] = new Vector2((float)j / around, 1f - (float)i / down);
                }
            }
            var triangles = new int[around * down * 6];
            int o = 0;
            for (int i = 0; i < down; i++)
                for (int j = 0; j < around; j++)
                {
                    int a = i * ring + j, b = a + 1, c = a + ring, d = c + 1;
                    triangles[o++] = a; triangles[o++] = b; triangles[o++] = d;
                    triangles[o++] = a; triangles[o++] = d; triangles[o++] = c;
                }
            var mesh = new Mesh { name = "TGPT harness lantern glass", hideFlags = HideFlags.DontSave };
            mesh.vertices = vertices; mesh.normals = normals; mesh.tangents = tangents; mesh.uv = uv; mesh.triangles = triangles;
            mesh.RecalculateBounds();
            return mesh;
        }

        // The shade: a cone open at the bottom, its tip `tip` above the origin, with both of its sides (the outside seen from
        // above, the inside from the water).
        private static Mesh Cone(float rim, float height, float tip, int around)
        {
            int ring = around + 1;
            var vertices = new Vector3[ring * 4];
            var normals = new Vector3[vertices.Length];
            var tangents = new Vector4[vertices.Length];
            var uv = new Vector2[vertices.Length];
            for (int j = 0; j <= around; j++)
            {
                float phi = 2f * Mathf.PI * j / around;
                float c = Mathf.Cos(phi), s = Mathf.Sin(phi);
                var top = new Vector3(0f, tip, 0f);
                var bottom = new Vector3(rim * c, tip - height, rim * s);
                var outward = new Vector3(height * c, rim, height * s).normalized;
                var tangent = new Vector4(-s, 0f, c, -1f);
                // Outside: its tip, then its rim. Inside: the same places with the normal turned round.
                for (int side = 0; side < 2; side++)
                {
                    int k = side * 2 * ring + j;
                    vertices[k] = top; vertices[k + ring] = bottom;
                    normals[k] = normals[k + ring] = side == 0 ? outward : -outward;
                    tangents[k] = tangents[k + ring] = tangent;
                    uv[k] = new Vector2((float)j / around, 1f); uv[k + ring] = new Vector2((float)j / around, 0f);
                }
            }
            var triangles = new int[around * 6];
            int o = 0;
            for (int j = 0; j < around; j++)
            {
                // Seen from outside the next place round is to the right: tip, rim to the right, rim is clockwise.
                triangles[o++] = j; triangles[o++] = ring + j + 1; triangles[o++] = ring + j;
                int inner = 2 * ring;
                triangles[o++] = inner + j; triangles[o++] = inner + ring + j; triangles[o++] = inner + ring + j + 1;
            }
            var mesh = new Mesh { name = "TGPT harness lantern shade", hideFlags = HideFlags.DontSave };
            mesh.vertices = vertices; mesh.normals = normals; mesh.tangents = tangents; mesh.uv = uv; mesh.triangles = triangles;
            mesh.RecalculateBounds();
            return mesh;
        }

        internal static void Remove()
        {
            foreach (var o in new UnityEngine.Object[] { root, glass, shade, glassMesh, shadeMesh, glassMaterial, shadeMaterial })
                if (o != null) UnityEngine.Object.Destroy(o);
            root = null; glass = null; shade = null; light = null; data = null;
            glassMesh = null; shadeMesh = null; glassMaterial = null; shadeMaterial = null;
        }
    }
}
