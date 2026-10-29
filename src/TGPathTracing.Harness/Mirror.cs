// SPDX-License-Identifier: MIT
// PT-46's yardstick: "we need our tracer to be able to perfectly render the world in a mirror" (the owner, 2026-10-06).
// A perfect mirror is stood in the frozen world, and for the reference the camera is moved to its own mirror image:
// from there, looking back through the mirror's frame with the mirror taken away, the screen shows directly what the
// mirror shows by rays, with left and right swapped and pixel on pixel. tools/harness/mirror.py holds the two against
// each other. Also the count behind PT-45: what a second copy of the scene's textures would cost in video memory.
using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using UnityEngine;
using UnityEngine.Rendering;
namespace TGPathTracing.Harness
{
    internal static class Mirror
    {
        private static GameObject mirror;
        private static Material material;
        private static Mesh mesh;
        private static Camera held;
        private static Vector3 homePosition, viewPosition, centre, normal;
        private static Quaternion homeRotation, viewRotation;
        private static float homeNear, viewNear;
        private static Vector3[] corners = new Vector3[0];
        private static bool hooked, away;

        internal static bool Stands => mirror != null;

        // The mirror: distance metres along the camera's view, facing the camera, then turned by yaw degrees round the
        // camera's up (so it shows what is beside the camera, not the hero). Returns its corners on screen (x, y as
        // shares of the screen from the top left, four of them) for the comparison.
        internal static float[] Place(float distance, float width, float height, float yaw, out string detail)
        {
            Remove();
            var camera = Camera.main;
            if (camera == null) { detail = "no camera"; return new float[0]; }
            var t = camera.transform;
            Vector3 up = t.up;
            centre = t.position + t.forward * distance;
            normal = (Quaternion.AngleAxis(yaw, up) * -t.forward).normalized;
            Vector3 right = Vector3.Cross(up, -normal).normalized;
            var shader = Shader.Find("HDRP/Lit");
            if (shader == null) { detail = "the game has no HDRP/Lit shader"; return new float[0]; }
            material = new Material(shader) { name = "TGPT harness mirror", hideFlags = HideFlags.DontSave };
            material.SetColor("_BaseColor", Color.white);
            material.SetFloat("_Metallic", 1f);
            material.SetFloat("_Smoothness", 1f);
            HDMaterialValidate(material);
            float w = width * 0.5f, h = height * 0.5f;
            // Seen from the camera's side (the object's -z): bottom left, top left, top right, bottom right, clockwise.
            mesh = new Mesh { name = "TGPT harness mirror", hideFlags = HideFlags.DontSave };
            mesh.vertices = new[] { new Vector3(-w, -h, 0f), new Vector3(-w, h, 0f), new Vector3(w, h, 0f), new Vector3(w, -h, 0f) };
            mesh.normals = new[] { Vector3.back, Vector3.back, Vector3.back, Vector3.back };
            mesh.tangents = new[] { new Vector4(1f, 0f, 0f, -1f), new Vector4(1f, 0f, 0f, -1f), new Vector4(1f, 0f, 0f, -1f), new Vector4(1f, 0f, 0f, -1f) };
            mesh.uv = new[] { new Vector2(0f, 0f), new Vector2(0f, 1f), new Vector2(1f, 1f), new Vector2(1f, 0f) };
            mesh.triangles = new[] { 0, 1, 2, 0, 2, 3 };
            mesh.RecalculateBounds();
            // No HideFlags.DontSave on the object (as Lantern.cs): Object.FindObjectsByType, which the tracer finds the scene's
            // renderers with (SceneCapture), leaves such objects out, so rays passed the mirror and it showed what stood
            // behind it (run 20261008-100208; the owner: "The mirrors do not work at all"). Nothing is saved during a run,
            // and Remove destroys it.
            mirror = new GameObject("TGPT harness mirror");
            mirror.transform.SetPositionAndRotation(centre, Quaternion.LookRotation(-normal, up));
            mirror.AddComponent<MeshFilter>().sharedMesh = mesh;
            var renderer = mirror.AddComponent<MeshRenderer>();
            renderer.sharedMaterial = material;
            renderer.shadowCastingMode = ShadowCastingMode.Off;
            corners = new[] { centre - right * w - up * h, centre - right * w + up * h, centre + right * w + up * h, centre + right * w - up * h };
            var screen = new float[8];
            bool inView = true;
            for (int i = 0; i < 4; i++)
            {
                Vector3 v = camera.WorldToViewportPoint(corners[i]);
                screen[i * 2] = v.x; screen[i * 2 + 1] = 1f - v.y;
                inView &= v.z > 0f && v.x >= 0f && v.x <= 1f && v.y >= 0f && v.y <= 1f;
            }
            // The camera's mirror image: its place and its axes reflected in the mirror's plane. A camera has no
            // left-handed twin, so it looks along the reflected view with the reflected up: the same picture with left and
            // right swapped. Its near plane starts where the mirror's frame does, so nothing behind the mirror is drawn.
            held = camera;
            homePosition = t.position; homeRotation = t.rotation; homeNear = camera.nearClipPlane;
            viewPosition = Reflect(t.position - centre) + centre;
            Vector3 forward = Reflect(t.forward);
            viewRotation = Quaternion.LookRotation(forward, Reflect(up));
            float nearest = float.MaxValue;
            foreach (var corner in corners) nearest = Mathf.Min(nearest, Vector3.Dot(corner - viewPosition, forward));
            viewNear = Mathf.Max(homeNear, nearest - 0.01f);
            away = false;
            detail = "a mirror " + width.ToString("0.0") + " x " + height.ToString("0.0") + " m, " + distance.ToString("0.0") + " m ahead, turned " + yaw.ToString("0") +
                " degrees" + (inView ? "" : " (NOT wholly on screen)") + "; the camera's mirror image stands " + (viewPosition - homePosition).magnitude.ToString("0.0") +
                " m away, its near plane at " + viewNear.ToString("0.00") + " m";
            return screen;
        }

        private static Vector3 Reflect(Vector3 v) => v - 2f * Vector3.Dot(v, normal) * normal;

        // HDRP's own setup of a material's keywords and passes (HDMaterial.ValidateMaterial), where this HDRP has it.
        private static void HDMaterialValidate(Material m)
        {
            try
            {
                var type = Type.GetType("UnityEngine.Rendering.HighDefinition.HDMaterial, Unity.RenderPipelines.HighDefinition.Runtime");
                var method = type != null ? type.GetMethod("ValidateMaterial", new[] { typeof(Material) }) : null;
                if (method != null) method.Invoke(null, new object[] { m });
            }
            catch { }
        }

        // mirrored: the mirror is taken away and the camera stands at its mirror image from the next frame on. Else the
        // camera is back where the scenario put it and the mirror stands.
        internal static void View(bool mirrored)
        {
            if (mirror == null || held == null) return;
            if (mirrored == away) return;
            away = mirrored;
            mirror.SetActive(!mirrored);
            if (mirrored)
            {
                if (!hooked) { RenderPipelineManager.beginContextRendering += BeforeRendering; hooked = true; }
                Hold();
            }
            else
            {
                Unhook();
                held.transform.SetPositionAndRotation(homePosition, homeRotation);
                held.nearClipPlane = homeNear;
            }
        }

        // Each frame, after the game's own camera code and before HDRP culls: the game puts its camera where the hero
        // looks, every frame.
        internal static void Hold()
        {
            if (!away || held == null) return;
            held.transform.SetPositionAndRotation(viewPosition, viewRotation);
            held.nearClipPlane = viewNear;
        }
        private static void BeforeRendering(ScriptableRenderContext context, List<Camera> cameras) { Hold(); }
        private static void Unhook()
        {
            if (hooked) { RenderPipelineManager.beginContextRendering -= BeforeRendering; hooked = false; }
        }

        internal static void Remove()
        {
            Unhook();
            if (away && held != null)
            {
                held.transform.SetPositionAndRotation(homePosition, homeRotation);
                held.nearClipPlane = homeNear;
            }
            away = false; held = null;
            if (mirror != null) UnityEngine.Object.Destroy(mirror);
            if (material != null) UnityEngine.Object.Destroy(material);
            if (mesh != null) UnityEngine.Object.Destroy(mesh);
            mirror = null; material = null; mesh = null;
        }

        // PT-45's number: every texture the scene's renderers draw with (each once), and what copies of them would take
        // in video memory at their own size and capped at 2048, 1024, 512 and 256 a side, as the game holds them
        // (compressed) and as plain 8-bit colour. Within reach metres of the camera, and for the whole scene.
        internal static int WriteTextures(string path, float reach)
        {
            var camera = Camera.main;
            Vector3 eye = camera != null ? camera.transform.position : Vector3.zero;
            var near = new Dictionary<int, Texture>();
            var all = new Dictionary<int, Texture>();
            var ids = new List<int>();
            int renderers = 0, nearRenderers = 0;
            foreach (var renderer in UnityEngine.Object.FindObjectsByType<Renderer>(FindObjectsSortMode.None))
            {
                if (renderer == null || !renderer.enabled || !renderer.gameObject.activeInHierarchy) continue;
                ++renderers;
                bool close = renderer.bounds.SqrDistance(eye) <= reach * reach;
                if (close) ++nearRenderers;
                foreach (var m in renderer.sharedMaterials)
                {
                    if (m == null) continue;
                    ids.Clear();
                    m.GetTexturePropertyNameIDs(ids);
                    foreach (int id in ids)
                    {
                        var texture = m.GetTexture(id);
                        if (texture == null || !(texture is Texture2D)) continue;
                        all[texture.GetInstanceID()] = texture;
                        if (close) near[texture.GetInstanceID()] = texture;
                    }
                }
            }
            // Most of the world is drawn by the game's own renderers (Medusa, Kandra: no Unity Renderer of their own, the first
            // mirror run counted 2 textures within 40 m), so the whole set is every 2D texture loaded, whoever draws it.
            int loadedCount = 0;
            foreach (var texture in Resources.FindObjectsOfTypeAll<Texture2D>())
            {
                if (texture == null || texture.width < 8 || texture.height < 8) continue;
                all[texture.GetInstanceID()] = texture; ++loadedCount;
            }
            var text = new StringBuilder();
            text.AppendLine("What copies of the scene's textures would take in video memory (PT-45). Each texture once, whatever the");
            text.AppendLine("slot (colour, normal, mask, detail); \"loaded\" includes the menus' and UI's.");
            text.AppendLine();
            Totals(text, "named by Unity renderers within " + reach.ToString("0") + " m of the camera (" + nearRenderers + " renderers)", near.Values);
            Totals(text, "every 2D texture loaded (" + loadedCount + "; " + renderers + " Unity renderers in the scene)", all.Values);
            var largest = new List<Texture>(all.Values);
            largest.Sort((a, b) => Bytes(b, 0, false).CompareTo(Bytes(a, 0, false)));
            text.AppendLine("The 40 largest loaded:");
            for (int i = 0; i < largest.Count && i < 40; i++)
            {
                var texture = (Texture2D)largest[i];
                text.AppendLine("  " + texture.width + "x" + texture.height + " " + texture.format + " mips " + texture.mipmapCount + "  " +
                    (Bytes(texture, 0, false) / 1048576.0).ToString("0.0") + " MB  " + texture.name);
            }
            Describe(text, all.Values);
            File.WriteAllText(path, text.ToString(), new UTF8Encoding(false));
            return near.Count;
        }

        // PT-45's cheap way: the tracer's device opening the game's own textures. Whether Direct3D lets a texture be shared
        // depends on how it was made (its usage, flags, format and mips), so: how the game made the textures within reach,
        // read from each one's ID3D11Texture2D::GetDesc (vtable slot 10; GetType is 7), and Unity's mip streaming.
        [StructLayout(LayoutKind.Sequential)]
        private struct D3D11Texture2DDesc
        {
            public uint Width, Height, MipLevels, ArraySize, Format, SampleCount, SampleQuality, Usage, BindFlags, CPUAccessFlags, MiscFlags;
        }
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate void GetTypeFn(IntPtr self, out int dimension);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate void GetDescFn(IntPtr self, out D3D11Texture2DDesc desc);

        private static void Describe(StringBuilder text, ICollection<Texture> textures)
        {
            text.AppendLine();
            text.AppendLine("How the game made them in Direct3D 11 (" + SystemInfo.graphicsDeviceType + "), every loaded one. Mip streaming: " +
                (QualitySettings.streamingMipmapsActive ? "on, budget " + QualitySettings.streamingMipmapsMemoryBudget.ToString("0") + " MB" : "off") + ".");
            var kinds = new Dictionary<string, int>();
            int streamed = 0, partial = 0, unread = 0;
            foreach (var texture in textures)
            {
                var t = texture as Texture2D;
                if (t == null) continue;
                if (t.streamingMipmaps) { ++streamed; if (t.loadedMipmapLevel > 0) ++partial; }
                IntPtr native = IntPtr.Zero;
                try { native = t.GetNativeTexturePtr(); } catch { }
                if (native == IntPtr.Zero || SystemInfo.graphicsDeviceType != GraphicsDeviceType.Direct3D11) { ++unread; continue; }
                try
                {
                    IntPtr vtable = Marshal.ReadIntPtr(native);
                    var getType = (GetTypeFn)Marshal.GetDelegateForFunctionPointer(Marshal.ReadIntPtr(vtable, 7 * IntPtr.Size), typeof(GetTypeFn));
                    getType(native, out int dimension);
                    if (dimension != 3) { ++unread; continue; }   // D3D11_RESOURCE_DIMENSION_TEXTURE2D
                    var getDesc = (GetDescFn)Marshal.GetDelegateForFunctionPointer(Marshal.ReadIntPtr(vtable, 10 * IntPtr.Size), typeof(GetDescFn));
                    getDesc(native, out var d);
                    bool fullMips = d.MipLevels >= 1 + (uint)Mathf.FloorToInt(Mathf.Log(Mathf.Max(d.Width, d.Height), 2f));
                    string kind = "usage " + UsageName(d.Usage) + ", bind 0x" + d.BindFlags.ToString("X") + ", cpu 0x" + d.CPUAccessFlags.ToString("X") + ", misc 0x" +
                        d.MiscFlags.ToString("X") + ", format " + d.Format + (d.ArraySize > 1 ? ", array" : "") + (d.MipLevels > 1 ? (fullMips ? ", all mips" : ", some mips") : ", one mip");
                    kinds[kind] = kinds.TryGetValue(kind, out int n) ? n + 1 : 1;
                }
                catch { ++unread; }
            }
            var sorted = new List<KeyValuePair<string, int>>(kinds);
            sorted.Sort((a, b) => b.Value.CompareTo(a.Value));
            foreach (var pair in sorted) text.AppendLine("  " + pair.Value.ToString().PadLeft(5) + "  " + pair.Key);
            text.AppendLine("  (DXGI formats: 71/72 BC1, 77/78 BC3, 80 BC4, 83 BC5, 95 BC6H, 98/99 BC7, 28/29 RGBA8; misc 0x800 shared NT handle, 0x2 shared, 0x1 generate mips)");
            text.AppendLine("  " + streamed + " stream their mips, " + partial + " of them without their top mip loaded now; " + unread + " not read.");
        }
        private static string UsageName(uint usage) => usage == 0 ? "default" : usage == 1 ? "immutable" : usage == 2 ? "dynamic" : usage == 3 ? "staging" : usage.ToString();

        private static void Totals(StringBuilder text, string what, ICollection<Texture> textures)
        {
            text.AppendLine(what + ": " + textures.Count + " textures");
            foreach (int cap in new[] { 0, 2048, 1024, 512, 256, 64 })
            {
                long held = 0, plain = 0;
                foreach (var texture in textures) { held += Bytes(texture, cap, false); plain += Bytes(texture, cap, true); }
                text.AppendLine("  " + (cap == 0 ? "at their own size" : "capped at " + cap).PadRight(18) + " as the game holds them " + (held / 1048576.0).ToString("0").PadLeft(6) +
                    " MB   as plain 8-bit colour " + (plain / 1048576.0).ToString("0").PadLeft(6) + " MB");
            }
            text.AppendLine();
        }

        // A texture with its mips, no larger than cap a side (0: its own size): in its own format, or 4 bytes a texel.
        private static long Bytes(Texture texture, int cap, bool plain)
        {
            var t = texture as Texture2D;
            if (t == null) return 0;
            int w = t.width, h = t.height;
            while (cap > 0 && (w > cap || h > cap) && w > 1 && h > 1) { w >>= 1; h >>= 1; }
            double bits = plain ? 32.0 : BitsPerTexel(t.format);
            double texels = (double)w * h * (t.mipmapCount > 1 ? 4.0 / 3.0 : 1.0);
            return (long)(texels * bits / 8.0);
        }
        private static double BitsPerTexel(TextureFormat format)
        {
            switch (format)
            {
                case TextureFormat.DXT1: case TextureFormat.DXT1Crunched: case TextureFormat.BC4: return 4.0;
                case TextureFormat.DXT5: case TextureFormat.DXT5Crunched: case TextureFormat.BC5: case TextureFormat.BC7: case TextureFormat.BC6H: return 8.0;
                case TextureFormat.Alpha8: case TextureFormat.R8: return 8.0;
                case TextureFormat.RG16: case TextureFormat.R16: case TextureFormat.RGB565: case TextureFormat.RHalf: return 16.0;
                case TextureFormat.RGB24: return 24.0;
                case TextureFormat.RGBAHalf: return 64.0;
                case TextureFormat.RGBAFloat: return 128.0;
                default: return 32.0;
            }
        }
    }
}
