// SPDX-License-Identifier: MIT
// TG Path Tracing's rules for what of the world is traced (src/TGPathTracing/WorldRules.cs; PT-63, PT-61: "make sure WE
// trace the world, not the game"): the level of detail the game draws, by each of its renderers' own rule, and statically
// batched renderers in their combined mesh. Each expectation is worked out from the game's code by hand
// (docs/agents/world.md says which class each rule was read from), not by calling the rule under test another way.
using System;
using System.Collections.Generic;
using TGPathTracing;

internal static class Program
{
    private static int failures;
    private static void Expect(bool condition, string what)
    {
        if (!condition) { Console.WriteLine("FAIL: " + what); failures++; }
    }
    private static string Join(List<float> values) => string.Join(",", values.ConvertAll(v => v.ToString("0.#")).ToArray());

    private static int Main()
    {
        // ---- Entities Graphics and Drake (SelectLodEnabled). Three levels of one object: 0-20, 20-60, 60-200 (scaled metres).
        // At a field of view of 60 degrees and a LOD bias of 1 a metre counts 2 tan(30 degrees) = 1.1547.
        float scale = WorldRules.LodDistanceScale(60f, 1f);
        Expect(Math.Abs(scale - 1.1547f) < 1e-3f, "a distance is scaled by 2 tan(fov / 2) / bias: 1.1547 at 60 degrees and bias 1, got " + scale);
        Expect(Math.Abs(WorldRules.LodDistanceScale(60f, 0.4f) - 2.8868f) < 1e-3f, "the game's lowest Draw Distance (bias 0.4) scales a metre by 2.887");
        // 15 m away: scaled 17.3, the finest level. Until 2026-10-07 the finest was traced within 30 m whatever was drawn.
        Expect(WorldRules.EntityLevelDrawn(1, 0f, 20f, 15f * scale, 0), "at 15 m the finest level (0 to 20) is drawn");
        Expect(!WorldRules.EntityLevelDrawn(2, 20f, 60f, 15f * scale, 0), "at 15 m the second level is not drawn");
        // 25 m away: scaled 28.9, the SECOND level, though the camera is within 30 m: the traced level is the drawn one.
        Expect(!WorldRules.EntityLevelDrawn(1, 0f, 20f, 25f * scale, 0), "at 25 m the finest level is no longer drawn (scaled 28.9 is past 20)");
        Expect(WorldRules.EntityLevelDrawn(2, 20f, 60f, 25f * scale, 0), "at 25 m the second level (20 to 60) is drawn");
        // The ends: a range holds its start and not its end.
        Expect(WorldRules.EntityLevelDrawn(2, 20f, 60f, 20f, 0) && !WorldRules.EntityLevelDrawn(2, 20f, 60f, 60f, 0), "a LOD range holds its start and not its end");
        // Past the last range nothing is drawn.
        Expect(!WorldRules.EntityLevelDrawn(4, 60f, 200f, 250f, 0), "past the last level's range nothing is drawn");
        // A quality whose maximum LOD level is 1 never draws level 0, and draws level 1 from distance 0.
        Expect(!WorldRules.EntityLevelDrawn(1, 0f, 20f, 5f, 1), "with a maximum LOD level of 1 the finest level is never drawn");
        Expect(WorldRules.EntityLevelDrawn(2, 20f, 60f, 5f, 1), "with a maximum LOD level of 1 the second level is drawn from distance 0");
        Expect(WorldRules.EntityLevelDrawn(4, 60f, 200f, 100f, 1) && !WorldRules.EntityLevelDrawn(4, 60f, 200f, 30f, 1), "levels past the maximum keep their own range");
        // A level about to be drawn: within a quarter of its nearer end.
        Expect(WorldRules.EntityLevelNear(20f, 60f, 17f) && !WorldRules.EntityLevelNear(20f, 60f, 10f), "the second level is near at 17 (17 x 1.25 reaches 20) and not at 10");
        Expect(WorldRules.EntityLevelNear(0f, 20f, 24f) && !WorldRules.EntityLevelNear(0f, 20f, 30f), "the finest level is near at 24 (24 x 0.8 is under 20) and not at 30");

        // ---- Which levels are traced. Levels by their range's start; two entities share level 20 (two renderers).
        var levels = new List<float> { 0f, 20f, 20f, 60f };
        var traced = new List<float>();
        var registered = new HashSet<float> { 0f, 20f, 60f };
        var asked = new List<float>();
        Func<float, bool> ready = l => { asked.Add(l); return registered.Contains(l); };
        int standIns = WorldRules.TracedLevels(levels, new List<bool> { false, true, true, false }, new List<bool> { true, false, false, false }, ready, traced);
        Expect(standIns == 0 && Join(traced) == "20", "the drawn level is traced, once, though two entities have it: got " + Join(traced) + " with " + standIns + " stand-ins");
        Expect(asked.Contains(0f), "a level about to be drawn is asked for ahead (its meshes are registered before it is wanted)");
        Expect(!asked.Contains(60f), "a level neither drawn nor near is never asked for");
        // The drawn level's meshes are not registered yet: the nearest registered level stands in, and is counted.
        registered = new HashSet<float> { 0f, 60f };
        standIns = WorldRules.TracedLevels(levels, new List<bool> { false, true, true, false }, new List<bool> { true, false, false, true }, ready, traced);
        Expect(standIns == 1 && Join(traced) == "0", "while level 20 registers the nearest registered level (0, 20 away, against 60, 40 away) stands in: got " + Join(traced));
        // Nothing registered: nothing traced yet, no stand-in counted.
        registered = new HashSet<float>();
        standIns = WorldRules.TracedLevels(levels, new List<bool> { false, true, true, false }, null, ready, traced);
        Expect(standIns == 0 && traced.Count == 0, "with nothing registered nothing is traced yet");
        // No level drawn (past the last range, or the game has not loaded it): nothing traced, whatever is registered.
        registered = new HashSet<float> { 0f, 20f, 60f };
        standIns = WorldRules.TracedLevels(levels, new List<bool> { false, false, false, false }, new List<bool> { false, false, false, true }, ready, traced);
        Expect(standIns == 0 && traced.Count == 0, "an object the game draws no level of is not traced (it was traced at its coarsest level 'for light')");
        // Two levels drawn at once (overlapping ranges): both traced, as Entities Graphics draws both.
        standIns = WorldRules.TracedLevels(levels, new List<bool> { true, true, true, false }, null, ready, traced);
        Expect(standIns == 0 && Join(traced) == "0,20", "two levels drawn at once are both traced: got " + Join(traced));

        // ---- GameObject LOD groups (Unity's LODGroup). A 2 m object with levels at 0.5, 0.2 and 0.05 of the screen's height.
        var heights = new List<float> { 0.5f, 0.2f, 0.05f };
        // At 60 degrees and bias 1 the object's height on screen is 2 / (2 tan(30 degrees) x distance) = 1.732 / distance.
        Expect(WorldRules.GroupLevel(heights, 2f, 3f, 60f, 1f, 0) == 0, "at 3 m (0.577 of the screen) the finest level is drawn");
        Expect(WorldRules.GroupLevel(heights, 2f, 5f, 60f, 1f, 0) == 1, "at 5 m (0.346 of the screen) the second level is drawn");
        Expect(WorldRules.GroupLevel(heights, 2f, 20f, 60f, 1f, 0) == 2, "at 20 m (0.087 of the screen) the third level is drawn");
        Expect(WorldRules.GroupLevel(heights, 2f, 40f, 60f, 1f, 0) == -1, "at 40 m (0.043 of the screen) Unity culls the object: nothing is traced");
        Expect(WorldRules.GroupLevel(heights, 2f, 40f, 60f, 2f, 0) == 2, "a LOD bias of 2 doubles its height on screen: the third level at 40 m");
        Expect(WorldRules.GroupLevel(heights, 2f, 3f, 60f, 1f, 1) == 1, "with a maximum LOD level of 1 the second level stands for the finest");
        Expect(WorldRules.GroupLevel(heights, 2f, 40f, 60f, 1f, 1) == -1, "a maximum LOD level does not bring a culled object back");
        Expect(WorldRules.GroupLevel(new List<float> { 0.1f }, 2f, 30f, 60f, 1f, 0) == -1, "a group of one level is culled past it too (0.058 of the screen)");
        Expect(WorldRules.GroupLevel(new List<float>(), 2f, 3f, 60f, 1f, 0) == -1, "a group with no levels draws nothing");

        // ---- Medusa (CameraFrustumLodJob): the lowest level whose squared distance is above the scaled squared distance.
        var medusa = new[] { 400f, 3600f, 40000f, float.NegativeInfinity, 0f, 0f, 0f, 0f };
        Expect(WorldRules.MedusaLevel(100f, l => medusa[l]) == 0, "Medusa: 10 scaled metres is level 0 (under 20)");
        Expect(WorldRules.MedusaLevel(400f, l => medusa[l]) == 1, "Medusa: exactly 20 scaled metres is level 1 (not under 20)");
        Expect(WorldRules.MedusaLevel(10000f, l => medusa[l]) == 2, "Medusa: 100 scaled metres is level 2");
        Expect(WorldRules.MedusaLevel(90000f, l => medusa[l]) == 0, "Medusa: past every level its shift wraps to level 0");

        // ---- Leshy (CreateLodMask): level i for a squared distance in [distance of i - 1, distance of i), plain metres.
        float inf = float.PositiveInfinity;
        var grass = new[] { 15f * 15f, 40f * 40f, 80f * 80f, inf, inf, inf, inf, inf };
        Expect(WorldRules.LeshyLevel(grass, 10f * 10f) == 0, "Leshy: 10 m is level 0");
        Expect(WorldRules.LeshyLevel(grass, 15f * 15f) == 1, "Leshy: exactly 15 m is level 1");
        Expect(WorldRules.LeshyLevel(grass, 79f * 79f) == 2, "Leshy: 79 m is level 2");
        // Past the last distance the first unused level takes the instance; no renderer has that level, so nothing is drawn.
        Expect(WorldRules.LeshyLevel(grass, 100f * 100f) == 3, "Leshy: past the last distance the unused level 3 is chosen (which no renderer has)");
        // A tree with a billboard: its level 3 ends at the billboard distance; past it nothing.
        var tree = new[] { 30f * 30f, 60f * 60f, 120f * 120f, 400f * 400f, inf, inf, inf, inf };
        Expect(WorldRules.LeshyLevel(tree, 200f * 200f) == 3, "Leshy: a tree 200 m away is at its billboard level 3");
        Expect(WorldRules.LeshyLevel(tree, 500f * 500f) == 4, "Leshy: a tree past its billboard distance is at an unused level");
        Expect(WorldRules.LeshyLevel(new[] { float.NaN, 100f }, 50f) == -1, "Leshy: a NaN distance ends the list (no comparison with it holds)");
        // The mesh traced where the game draws the card: the coarsest level any mesh renderer has, below the card's.
        Expect(WorldRules.CoarsestMeshLevel(new List<byte> { 1, 2, 4, 4 }, 3) == 2, "a tree's coarsest mesh level under a billboard at level 3 is 2");
        Expect(WorldRules.CoarsestMeshLevel(new List<byte> { 3, 8 }, 3) == 1, "a renderer drawn at levels 0 and 1 and one at the card's own level: 1");
        Expect(WorldRules.CoarsestMeshLevel(new List<byte>(), 3) == -1, "a tree with no mesh renderer has nothing to trace for its card");

        // ---- Statically batched renderers: a combined mesh of 6 submeshes, three members.
        var list = new uint[6]; // 0: invisible to rays
        Expect(WorldRules.PlaceBatchMember(list, 0, new List<uint> { 7 }), "a member with one material names one submesh");
        Expect(WorldRules.PlaceBatchMember(list, 3, new List<uint> { 9, 11 }), "a member with two materials names two submeshes");
        Expect(list[0] == 7 && list[1] == 0 && list[2] == 0 && list[3] == 9 && list[4] == 11 && list[5] == 0,
            "each drawn member's materials land on its own submeshes; the submeshes of members not drawn (1, 2, 5) stay invisible: got " + string.Join(",", list));
        Expect(WorldRules.PlaceBatchMember(list, 5, new List<uint> { 13, 15 }) && list[5] == 13, "materials past the mesh's last submesh are dropped, the rest land");
        Expect(!WorldRules.PlaceBatchMember(list, 6, new List<uint> { 17 }), "a member that names no submesh of the mesh is not placed");
        Expect(!WorldRules.PlaceBatchMember(list, -1, new List<uint> { 17 }) && !WorldRules.PlaceBatchMember(list, 0, new List<uint>()), "nor one with no start or no materials");

        // ---- When the levels are chosen again: after a metre of camera movement, or when the scale changed by 1 %.
        Expect(WorldRules.LevelsMayHaveChanged(1.1f * 1.1f, 1.1547f, 1.1547f, 1f), "1.1 m of camera movement may have changed a level");
        Expect(!WorldRules.LevelsMayHaveChanged(0.5f * 0.5f, 1.1547f, 1.1547f, 1f), "half a metre has not, by this rule");
        Expect(WorldRules.LevelsMayHaveChanged(0f, 1.1547f, 0.7f, 1f), "a zoom (another field of view) may have changed every level");
        Expect(!WorldRules.LevelsMayHaveChanged(0f, 1.1547f, 1.1550f, 1f), "a scale within 1 % is the same scale");

        Console.WriteLine(failures == 0 ? "world rules: all checks passed" : "world rules: " + failures + " FAILED");
        return failures == 0 ? 0 : 1;
    }
}
