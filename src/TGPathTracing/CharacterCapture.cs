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
using Unity.Mathematics;
using UnityEngine;
using UnityEngine.Rendering;
namespace TGPathTracing
{
    internal static class CharacterCapture
    {
        // Metres from the camera to a renderer's bounding sphere, the nearest first. At 30 m their shadows popped in; past 45 m
        // characters floated in the sun, where HDRP's cascades used to shadow them. Refitted in between rebuilds (scene.cpp
        // kCharacterRebuild), a full set of 64 costs a fraction of a millisecond, so the far ones come too where the near ones
        // leave room.
        private const float MaxDistance = 100f;
        private const int MaxRenderers = 64;         // the native side's limit (a character is several renderers)
        private const int MaxVertices = 4096 * 128;  // what the native side's texture holds a frame
        private const int MaxSubmeshes = 8;          // TGPT_SKINNED_MATERIALS
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
        internal static string Summary => Traced + " renderers" + (lastProblem.Length > 0 ? " (last problem: " + lastProblem + ")" : "");

        internal static void Reset()
        {
            characters.Clear();
            ++generation;
            Traced = 0;
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
            if (!Session.Settings.Characters.Value) { tracer.SetSkinned(IntPtr.Zero, 0, null, 0); return; }
            try { CaptureCharacters(tracer, camera); }
            catch (Exception ex)
            {
                Problem(ex.GetType().Name + ": " + ex.Message);
                tracer.SetSkinned(IntPtr.Zero, 0, null, 0);
            }
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
                if (distance > MaxDistance || !math.all(math.isfinite(sphere))) continue;
                candidates.Add((r, distance, sphere));
            }
            candidates.Sort((a, b) => a.distance.CompareTo(b.distance));
            instances.Clear();
            int vertices = 0;
            foreach (var c in candidates)
            {
                if (instances.Count >= MaxRenderers) break;
                // One renderer's trouble leaves that renderer out, not the others.
                try
                {
                    if (!manager.TryGetInstanceData(c.renderer, out var data)) continue;
                    var rd = c.renderer.rendererData;
                    var mesh = rd.mesh;
                    if (mesh == null || mesh.vertexCount == 0 || vertices + mesh.vertexCount > MaxVertices) continue;
                    var materials = rd.RenderingMaterials;
                    // The game draws one submesh per material (the fewer of the two); more than eight are left out.
                    int drawn = Math.Min(mesh.submeshes != null ? mesh.submeshes.Length : 1, materials != null ? materials.Length : 0);
                    if (drawn <= 0) continue;
                    int n = Math.Min(drawn, MaxSubmeshes);
                    var leftOut = new bool[n];
                    for (int s = 0; s < n; ++s) leftOut[s] = SceneCapture.CharacterLeftOut(materials[s]);
                    if (hideLayers) HideTransparentHair(materials, n);
                    var character = Find(c.renderer, mesh, leftOut, data.sharedStartVertex);
                    if (character.Handle <= 0) continue;
                    var instance = new NativeSkinnedInstance { Mesh = character.Handle, FirstVertex = data.instanceStartVertex, VertexCount = mesh.vertexCount };
                    SetTransform(ref instance, float4x4.identity);
                    instance.SubmeshCount = (uint)n;
                    // Their textures where the texture coordinates arrived with the triangles, their colors otherwise.
                    uint material = 0;
                    for (int s = 0; s < MaxSubmeshes; ++s)
                    {
                        if (s < n) material = SceneCapture.CharacterMaterial(materials[s], character.Textured);
                        instance.SetMaterial(s, material);
                    }
                    instances.Add(instance);
                    vertices += mesh.vertexCount;
                    CheckSpace(output, data.instanceStartVertex, c.sphere);
                }
                catch (Exception ex) { Problem(c.renderer.name + ": " + ex.GetType().Name + " " + ex.Message); }
            }
            SceneCapture.FlushMaterials();
            var list = instances.ToArray();
            int result = tracer.SetSkinned(outputPointer, (uint)output.stride, list, (uint)list.Length);
            if (result != 0) { Problem("the native side refused this frame's characters (" + result + ")"); return; }
            Traced = list.Length;
        }

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
            character.Pending = true;
            ushort vertexCount = mesh.vertexCount;
            int requested = generation;
            var reads = new Reads { Waiting = 1 };
            void Finish()
            {
                if (--reads.Waiting > 0) return;
                character.Pending = false;
                if (requested != generation) return; // the scene was cleared meanwhile
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
