// SPDX-License-Identifier: MIT
// The rules of the things the game moves (PT-71 item 4; the owner: "What moves is traced as it moves"), kept free of the game
// so tests/moving_rules_tests.cs checks each (tests/MovingRules.Tests.csproj, docs/agents/moving.md). SceneCapture.Follow places
// every frame, where HDRP draws it then: every GameObject mesh renderer that is not statically batched, every Drake entity the
// game made movable (no Static tag), and every chunk the scan listed whose entities moved since; each with where it was the
// frame before, for its own motion.
using System.Collections.Generic;
namespace TGPathTracing
{
    internal static class MovingRules
    {
        // Two object-to-world transforms (row-major 3x4) are the same when every number is: a system that writes the value an
        // entity already had has not moved it, and any other write has (a windmill turns by less than a thousandth of a radian
        // a frame; no tolerance would tell that from noise). NaN is never the same.
        internal static bool SameTransform(float a00, float a01, float a02, float a03, float a10, float a11, float a12, float a13, float a20, float a21, float a22, float a23,
                                           float b00, float b01, float b02, float b03, float b10, float b11, float b12, float b13, float b20, float b21, float b22, float b23) =>
            a00 == b00 && a01 == b01 && a02 == b02 && a03 == b03 && a10 == b10 && a11 == b11 && a12 == b12 && a13 == b13 &&
            a20 == b20 && a21 == b21 && a22 == b22 && a23 == b23;

        // Whether the scan leaves an ECS chunk to the frames (placed every frame, never listed): Drake's movable entities (no
        // Static tag: its LinkedTransformSystem copies their GameObject's matrix every frame), and any chunk found moving since a
        // scan listed it. Entities Graphics' own (non-Drake) entities only once found moving: the game has no system that moves
        // them, and the castle is made of them.
        internal static bool PlacedEveryFrame(bool drake, bool hasStaticTag, bool foundMoving) => (drake && !hasStaticTag) || foundMoving;

        // A thing's identity from frame to frame: a Drake or Entities Graphics renderer by the lowest of its entities (index and
        // version: never negative, an entity's index being one), a GameObject renderer by its instance (any sign) as a negative
        // number, which no entity's is.
        internal static long EntityIdentity(int index, int version) => ((long)index << 32) | (uint)version;
        internal static long RendererIdentity(int instanceId) => long.MinValue | (uint)instanceId;

        // Where each thing placed was the frame before, by its identity: Was hands back the transform it had then (none: the one
        // it has now, so a thing that appears has no motion of its own) and keeps the one it has now for the next frame; EndFrame
        // closes the frame (a thing not placed in it, hidden or gone, starts without motion when it comes back).
        internal sealed class Previous<T> where T : struct
        {
            private Dictionary<long, T> before = new Dictionary<long, T>(), now = new Dictionary<long, T>();
            internal T Was(long identity, T current)
            {
                now[identity] = current;
                return before.TryGetValue(identity, out var was) ? was : current;
            }
            internal void EndFrame() { var b = before; before = now; now = b; now.Clear(); }
            internal void Clear() { before.Clear(); now.Clear(); }
            internal int Count => before.Count;
        }

        // The scan's instances the things placed this frame stand in for (not traced this frame): every listed instance one of
        // the placed chunks' entities is in, once each, in the order first met.
        internal static List<int> StandingIn<TEntity>(IEnumerable<TEntity> entities, IDictionary<TEntity, int> listed)
        {
            var seen = new HashSet<int>();
            var result = new List<int>();
            foreach (var e in entities) if (listed.TryGetValue(e, out int at) && seen.Add(at)) result.Add(at);
            return result;
        }
    }
}
