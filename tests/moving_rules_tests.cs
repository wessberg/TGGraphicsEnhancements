// SPDX-License-Identifier: MIT
// TG Path Tracing's rules for the things the game moves (src/TGPathTracing/MovingRules.cs; PT-71 item 4, the owner: "What
// moves is traced as it moves"): what counts as a move, which entities the scan leaves to the frames, a thing's identity from
// frame to frame and where it was the frame before, and the scan's instances the things placed stand in for. Each expectation
// is worked out by hand from the game's code (docs/agents/moving.md section 1), not by calling the rule under test another way.
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
    private static bool Same(float[] a, float[] b) =>
        MovingRules.SameTransform(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11],
                                  b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11]);

    private static int Main()
    {
        // ---- A move. A windmill's blades at 1,000 m from the world's origin, turned by RotateTransformSystem by a ten-thousandth
        // of a radian in a frame: cos changes in the 9th digit, sin by 1e-4. The scan's copy holds the frame it was taken in.
        float angle = 0.3f, step = 1e-4f;
        float[] scanned = { (float)Math.Cos(angle), -(float)Math.Sin(angle), 0f, 1000f, (float)Math.Sin(angle), (float)Math.Cos(angle), 0f, 20f, 0f, 0f, 1f, -350f };
        float[] turned = { (float)Math.Cos(angle + step), -(float)Math.Sin(angle + step), 0f, 1000f, (float)Math.Sin(angle + step), (float)Math.Cos(angle + step), 0f, 20f, 0f, 0f, 1f, -350f };
        Expect(Same(scanned, (float[])scanned.Clone()), "a transform written again with the same numbers is no move");
        Expect(!Same(scanned, turned), "blades turned by a ten-thousandth of a radian have moved");
        // A carried lantern 1,000 m out moved by one step of a float there (6 hundredths of a millimetre): moved.
        float[] nudged = (float[])scanned.Clone();
        nudged[3] = BitConverter.ToSingle(BitConverter.GetBytes(BitConverter.ToInt32(BitConverter.GetBytes(1000f), 0) + 1), 0);
        Expect(nudged[3] > 1000f && nudged[3] - 1000f < 1e-4f, "the next float after 1,000 is 6 hundredths of a millimetre on");
        Expect(!Same(scanned, nudged), "the smallest step a float takes at 1,000 m is a move");
        float[] broken = (float[])scanned.Clone(); broken[7] = float.NaN;
        Expect(!Same(broken, broken), "a transform holding NaN is never the same (it is placed again, and refused natively)");

        // ---- Which ECS chunks the scan leaves to the frames (DrakeRendererManager.Register: a renderer that is not static gets
        // no Static tag and a LinkedTransformComponent, which LinkedTransformSystem writes every frame).
        Expect(MovingRules.PlacedEveryFrame(true, false, false), "a Drake entity without the Static tag (a weapon in a hand, an item, a door) is placed every frame");
        Expect(!MovingRules.PlacedEveryFrame(true, true, false), "a Drake entity tagged Static is listed by the scan");
        Expect(MovingRules.PlacedEveryFrame(true, true, true), "a Static Drake entity found moving (a windmill's blades, a bird, an elevator's chain) is placed every frame");
        Expect(!MovingRules.PlacedEveryFrame(false, false, false), "an Entities Graphics entity of the castle without the Static tag is listed (nothing in the game moves them)");
        Expect(MovingRules.PlacedEveryFrame(false, false, true), "an Entities Graphics entity found moving is placed every frame");

        // ---- Identities. Entity (5, 1) and (5, 2) are two entities (the index reused); a GameObject renderer's instance id is
        // negative for objects made at runtime and must never meet an entity's.
        Expect(MovingRules.EntityIdentity(5, 1) != MovingRules.EntityIdentity(5, 2), "an entity index reused by a new entity is another thing");
        Expect(MovingRules.EntityIdentity(5, 1) != MovingRules.EntityIdentity(6, 1), "two entities are two things");
        long[] renderers = { MovingRules.RendererIdentity(-123456), MovingRules.RendererIdentity(0), MovingRules.RendererIdentity(1), MovingRules.RendererIdentity(int.MaxValue), MovingRules.RendererIdentity(int.MinValue) };
        long[] entities = { MovingRules.EntityIdentity(0, 0), MovingRules.EntityIdentity(int.MaxValue, int.MaxValue), MovingRules.EntityIdentity(1 << 30, 1), MovingRules.EntityIdentity(0, -1) };
        bool apart = true;
        foreach (long r in renderers) foreach (long e in entities) apart &= r != e;
        Expect(apart, "no GameObject renderer has an entity's identity (the largest entity index, a version of -1 included)");
        Expect(MovingRules.RendererIdentity(-1) != MovingRules.RendererIdentity(1), "renderers -1 and 1 are two things");

        // ---- Where it was the frame before.
        var previous = new MovingRules.Previous<int>();
        Expect(previous.Was(1, 10) == 10, "a thing seen for the first time has no motion of its own (its previous transform is its own)");
        previous.EndFrame();
        Expect(previous.Was(1, 11) == 10, "the next frame a thing's previous transform is the one it had the frame before");
        Expect(previous.Was(2, 20) == 20, "a thing appearing beside it has no motion of its own");
        previous.EndFrame();
        Expect(previous.Was(1, 12) == 11 && previous.Was(2, 21) == 20, "each frame takes the frame before's, not the first one's");
        previous.EndFrame();
        // Thing 1 not placed this frame (sheathed, hidden): it comes back without motion, not with the jump since.
        Expect(previous.Was(2, 22) == 21, "a thing still placed keeps its history");
        previous.EndFrame();
        Expect(previous.Was(1, 40) == 40, "a thing hidden for a frame comes back with no motion of its own");
        previous.Clear();
        Expect(previous.Was(2, 23) == 23, "a cleared scene starts every thing without motion");

        // ---- The scan's instances the things placed stand in for: each listed instance once, in the order first met.
        var listed = new Dictionary<string, int> { { "sword blade", 5 }, { "sword hilt", 5 }, { "door", 9 } };
        var standing = MovingRules.StandingIn(new[] { "sword blade", "new torch", "sword hilt", "door" }, listed);
        Expect(standing.Count == 2 && standing[0] == 5 && standing[1] == 9, "the scan's instances of the things placed stand in once each: " + string.Join(",", standing));
        Expect(MovingRules.StandingIn(new[] { "new torch" }, listed).Count == 0, "a thing the scan never listed stands in for nothing");

        Console.WriteLine(failures == 0 ? "moving rules: all passed" : "moving rules: " + failures + " failed");
        return failures == 0 ? 0 : 1;
    }
}
