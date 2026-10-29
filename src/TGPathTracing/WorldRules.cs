// SPDX-License-Identifier: MIT
// What of the world the tracer traces, as the game draws it (PT-63, PT-61: "make sure WE trace the world, not the game";
// "that sounds like a big problem, if things suddenly disappear when PT is enabled"). The tracer's own camera rays show only
// what is in the traced scene, so an object left out is a hole and another level of detail is another edge. Kept free of the
// game so tests/world_rules_tests.cs checks each rule (tests/WorldRules.Tests.csproj, docs/agents/world.md). Each rule is
// the game's own, read from its code:
//   - Entities Graphics and Drake: Unity.Rendering.SelectLodEnabled;
//   - Medusa: MedusaBrgRenderer.CameraFrustumLodJob;
//   - Leshy: LeshyInstancesCameraCulling.CreateLodMask;
//   - GameObject LOD groups: Unity's LODGroup, with QualitySettings.maximumLODLevel.
using System;
using System.Collections.Generic;
namespace TGPathTracing
{
    internal static class WorldRules
    {
        // What Entities Graphics scales a distance by before it compares it with a LOD range (LODGroupExtensions.
        // CalculateLodDistanceScale): the LOD bias is the one HDRP culls with (the camera's frame settings), not the
        // quality setting read at any other time.
        internal static float LodDistanceScale(float fieldOfViewDegrees, float lodBias) =>
            2f * (float)Math.Tan(fieldOfViewDegrees * 0.5 * Math.PI / 180.0) / Math.Max(lodBias, 0.01f);

        // Whether Entities Graphics draws an entity's level (its LODRange) at a scaled distance: levels finer than the
        // quality's maximum LOD level are never drawn, and the level at that maximum is drawn from distance 0 on.
        internal static bool EntityLevelDrawn(int lodMask, float minDist, float maxDist, float scaledDistance, int maximumLodLevel)
        {
            int maximumMask = 1 << Math.Max(0, Math.Min(maximumLodLevel, 30));
            if (lodMask < maximumMask) return false;
            if (lodMask == maximumMask) minDist = 0f;
            return scaledDistance < maxDist && scaledDistance >= minDist;
        }
        // Whether a level that is not drawn is about to be (the camera within a quarter of its range's nearer end): its
        // meshes are registered ahead, and it may stand in for the drawn one while that one registers.
        internal static bool EntityLevelNear(float minDist, float maxDist, float scaledDistance) =>
            scaledDistance * 1.25f >= minDist && scaledDistance * 0.8f < maxDist;

        // The levels of one LOD group to trace: every drawn level whose meshes are registered (ready; asking queues them).
        // Where a drawn level is not registered yet, the registered level nearest to it stands in, so the object never
        // drops out of the traced scene while a mesh is read (its shadow vanished and came back at each switch). Levels
        // are named by their range's start; several entities of one level share it. near: levels to register ahead.
        // Returns how many drawn levels a stand-in is traced for.
        internal static int TracedLevels(IList<float> levels, IList<bool> drawn, IList<bool> near, Func<float, bool> ready, List<float> traced)
        {
            traced.Clear();
            int standIns = 0;
            if (near != null)
                for (int i = 0; i < levels.Count; i++)
                    if (near[i] && !drawn[i] && FirstOf(levels, i)) ready(levels[i]);
            for (int i = 0; i < levels.Count; i++)
            {
                if (!drawn[i] || !FirstDrawnOf(levels, drawn, i)) continue;
                float want = levels[i];
                if (ready(want)) { if (!traced.Contains(want)) traced.Add(want); continue; }
                float alt = float.NaN;
                for (int j = 0; j < levels.Count; j++)
                {
                    float level = levels[j];
                    if (level == want || IsDrawn(levels, drawn, level) || !ready(level)) continue;
                    if (float.IsNaN(alt) || Math.Abs(level - want) < Math.Abs(alt - want)) alt = level;
                }
                if (float.IsNaN(alt)) continue; // nothing registered yet: traced from the scan after its mesh is read
                ++standIns;
                if (!traced.Contains(alt)) traced.Add(alt);
            }
            return standIns;
        }
        private static bool FirstOf(IList<float> levels, int i)
        {
            for (int j = 0; j < i; j++) if (levels[j] == levels[i]) return false;
            return true;
        }
        private static bool FirstDrawnOf(IList<float> levels, IList<bool> drawn, int i)
        {
            for (int j = 0; j < i; j++) if (drawn[j] && levels[j] == levels[i]) return false;
            return true;
        }
        private static bool IsDrawn(IList<float> levels, IList<bool> drawn, float level)
        {
            for (int j = 0; j < levels.Count; j++) if (drawn[j] && levels[j] == level) return true;
            return false;
        }

        // The level Unity draws of a GameObject LOD group, or -1 where it draws none (smaller on screen than its last
        // level's height: culled). heights: each level's screenRelativeTransitionHeight; worldSize: the group's size times
        // its largest scale; levels finer than the quality's maximum LOD level are replaced by the level at it.
        internal static int GroupLevel(IList<float> heights, float worldSize, float distance, float fieldOfViewDegrees, float lodBias, int maximumLodLevel)
        {
            if (heights == null || heights.Count == 0) return -1;
            float relative = worldSize * Math.Max(lodBias, 0.01f) / (2f * (float)Math.Tan(fieldOfViewDegrees * 0.5 * Math.PI / 180.0) * Math.Max(distance, 0.01f));
            int chosen = -1;
            for (int i = 0; i < heights.Count; i++) if (relative >= heights[i]) { chosen = i; break; }
            if (chosen < 0) return -1;
            return Math.Max(chosen, Math.Min(Math.Max(maximumLodLevel, 0), heights.Count - 1));
        }

        // Medusa's level for an instance: the lowest whose squared distance the scaled squared camera distance is under;
        // none: level 0 (its bit shift wraps). A renderer is drawn there when its LOD mask has the level's bit.
        internal static int MedusaLevel(float scaledDistanceSq, Func<int, float> lodDistanceSq)
        {
            for (int l = 0; l < 8; l++) if (scaledDistanceSq < lodDistanceSq(l)) return l;
            return 0;
        }

        // Leshy's level for an instance at a squared distance: level i where the distance lies in [distance of i - 1,
        // distance of i), in metres squared, no LOD bias; -1 past the last. An unused level's distance is infinite (it
        // takes every farther instance, and no renderer has its bit: nothing is drawn there); a NaN ends the list.
        internal static int LeshyLevel(IList<float> lodDistancesSq, float distanceSq)
        {
            float from = 0f;
            for (int i = 0; i < lodDistancesSq.Count; i++)
            {
                float to = lodDistancesSq[i];
                if (from <= distanceSq && to > distanceSq) return i;
                from = to;
            }
            return -1;
        }
        // The coarsest level any of a tree's mesh renderers is drawn at (its LOD masks without the billboard's), below the
        // billboard's level; -1 for none. Leshy draws a tree past its last mesh as a card that turns to the camera: no ray
        // from anywhere else can meet that, so the tracer traces the tree's coarsest mesh there instead.
        internal static int CoarsestMeshLevel(IList<byte> meshLodMasks, int billboardLevel)
        {
            int coarsest = -1;
            foreach (byte mask in meshLodMasks)
                for (int l = 0; l < 8 && l < billboardLevel; l++)
                    if ((mask & (1 << l)) != 0 && l > coarsest) coarsest = l;
            return coarsest;
        }

        // A statically batched renderer in its combined mesh: Unity bakes the members of a static batch into one mesh, each
        // member drawing its own run of submeshes (from Renderer.subMeshStartIndex, one per material). The combined mesh is
        // traced once, with each drawn member's materials on its submeshes; submeshes no drawn member names stay invisible
        // to rays (list is filled with that before the first member). False when the member names no submesh of the mesh.
        internal static bool PlaceBatchMember(uint[] list, int subMeshStart, IList<uint> memberMaterials)
        {
            if (list == null || memberMaterials == null || subMeshStart < 0) return false;
            bool placed = false;
            for (int k = 0; k < memberMaterials.Count; k++)
            {
                int s = subMeshStart + k;
                if (s >= list.Length) break; // more materials than submeshes: Unity draws the last submesh again with them
                list[s] = memberMaterials[k];
                placed = true;
            }
            return placed;
        }

        // Whether the levels chosen at the last scan may have changed: the camera moved (any step crosses some instance's
        // switch distance when thousands are in reach), or what distances are scaled by did (a zoom, the Draw Distance row).
        internal static bool LevelsMayHaveChanged(float movedSq, float scaleThen, float scaleNow, float movedDistance) =>
            movedSq > movedDistance * movedDistance || Math.Abs(scaleNow - scaleThen) > 0.01f * Math.Max(Math.Abs(scaleThen), 1e-6f);
    }
}
