// SPDX-License-Identifier: MIT
// Two rules of SceneLights, kept free of the game so tests/light_rules_tests.cs checks them (tests/LightRules.Tests.csproj):
// what stands at a light the names leave out (a glowing thing, or nothing: the game's fill), and the size a traced light is
// given (what emits it, never the artist's radius).
using System;
using System.Collections.Generic;
namespace TGPathTracing
{
    // A glowing thing near the camera: a traced emissive surface (with its instance's key, SceneCapture.OwnSurfaceKey), an
    // effect drawn glowing (particles, a visual effect), or an emissive decal. Its world bounds (centre, half extents), its
    // colour, its name for the log, and whether a light kept by its names or look (a flame's, a lamp's) already stands at it:
    // claimed, its other lights are the game's fill, as a candle's extra lights beside the one traced are.
    internal struct GlowThing
    {
        internal float X, Y, Z, HalfX, HalfY, HalfZ, R, G, B;
        internal int Kind;
        internal long Key;
        internal bool Claimed;
        internal string Name;
    }
    // In the order a light's names are read in: the most specific first (a torch's "VFX_Fire" is the torch's flame).
    internal enum EmitterKind { Unknown, Candle, Torch, Lamp, Brazier, Fire, Lava, Ember, Flame, Effect, Glow, FlameByLook }
    internal static class LightRules
    {
        // VFX Graph's dedicated distortion output only warps the background; a graph named Fire does not make that
        // output a flame. Its culling bounds must never move or size the light in place of an actual emission output.
        internal static bool IsDistortionOutput(string shader)
        {
            if (string.IsNullOrEmpty(shader)) return false;
            int slash = shader.LastIndexOf('/');
            return shader.Substring(slash + 1).StartsWith("Distortion", StringComparison.OrdinalIgnoreCase);
        }
        internal const int GlowSurface = 0, GlowEffect = 1, GlowDecal = 2;
        internal static readonly string[] KindNames = { "surface", "effect", "decal" };
        // A light this near a glowing thing's bounds stands for it.
        internal const float GlowReach = 1f;
        // Bounds wider than this are a wall, a floor or a volume of air (dust, fog), not a thing a light stands for.
        internal const float MaxThingSize = 6f;
        internal static float Distance(float x, float y, float z, in GlowThing t)
        {
            float dx = Math.Max(Math.Abs(x - t.X) - t.HalfX, 0f), dy = Math.Max(Math.Abs(y - t.Y) - t.HalfY, 0f), dz = Math.Max(Math.Abs(z - t.Z) - t.HalfZ, 0f);
            return (float)Math.Sqrt(dx * dx + dy * dy + dz * dz);
        }
        internal static bool IsThing(in GlowThing t)
        {
            float largest = 2f * Math.Max(t.HalfX, Math.Max(t.HalfY, t.HalfZ));
            return !float.IsNaN(largest) && !float.IsInfinity(largest) && largest <= MaxThingSize;
        }
        // The glowing thing a light at (x, y, z) stands at: the nearest one within GlowReach of its bounds that is a thing and
        // that no flame's or lamp's light claimed; -1 when there is none (the light is the game's fill, fake area or bounce
        // light). claimedNear: a claimed one was within reach (for the log).
        internal static int GlowAt(float x, float y, float z, IList<GlowThing> things, out float distance, out bool claimedNear)
        {
            int best = -1; distance = float.MaxValue; claimedNear = false;
            for (int i = 0; i < things.Count; i++)
            {
                var t = things[i];
                if (!IsThing(t)) continue;
                float d = Distance(x, y, z, t);
                if (d > GlowReach) continue;
                if (t.Claimed) { claimedNear = true; continue; }
                if (d < distance) { distance = d; best = i; }
            }
            return best;
        }
        // Marks the things a light kept by its names or look stands at as claimed (a candle's flame, a lantern's glass, a
        // brazier's embers).
        internal static void Claim(float x, float y, float z, IList<GlowThing> things)
        {
            for (int i = 0; i < things.Count; i++)
            {
                var t = things[i];
                if (t.Claimed || !IsThing(t) || Distance(x, y, z, t) > GlowReach) continue;
                t.Claimed = true; things[i] = t;
            }
        }

        // The emitter a light kept by its own or a parent's name stands for: the most specific kind any of those names says
        // (Light_Torch_Big/VFX_Fire/PointLight_Fire is a torch, a candelabra's fire a candle's; MostSpecific over the names).
        private static readonly (EmitterKind kind, string[] words)[] KindWords =
        {
            (EmitterKind.Candle, new[] { "Candle", "Candela", "Chandelier" }),
            (EmitterKind.Torch, new[] { "Torch", "Sconce" }),
            (EmitterKind.Lamp, new[] { "Lantern", "Lamp" }),
            (EmitterKind.Brazier, new[] { "Brazier", "Hearth", "Forge", "Furnace" }),
            (EmitterKind.Effect, new[] { "Firefl" }), // fireflies are no fire
            (EmitterKind.Fire, new[] { "Bonfire", "Bonefire", "Campfire", "Fire" }),
            (EmitterKind.Lava, new[] { "Lava" }),
            (EmitterKind.Ember, new[] { "Ember" }),
            (EmitterKind.Flame, new[] { "Flame" }),
            (EmitterKind.Effect, new[] { "VFX", "Magic", "Spell", "Projectile", "Portal", "Explosion" }),
        };
        internal static EmitterKind KindOfName(string name)
        {
            if (string.IsNullOrEmpty(name)) return EmitterKind.Unknown;
            foreach (var (kind, words) in KindWords)
                foreach (var w in words)
                    if (name.IndexOf(w, StringComparison.OrdinalIgnoreCase) >= 0) return kind;
            return EmitterKind.Unknown;
        }
        internal static EmitterKind MostSpecific(EmitterKind a, EmitterKind b) => a == EmitterKind.Unknown ? b : b == EmitterKind.Unknown ? a : (EmitterKind)Math.Min((int)a, (int)b);
        // Each kind's radius (m) when nothing measures it, and the range a measured one is held to: a candle's flame is about
        // 1 cm across, a torch's 5 to 10 cm, a lantern its glass, a brazier's or a hearth's fire some 30 cm, a bonfire half a
        // metre; an effect or a glowing thing its own size.
        internal static float DefaultRadius(EmitterKind kind)
        {
            switch (kind)
            {
                case EmitterKind.Candle: return 0.006f;
                case EmitterKind.Torch: return 0.04f;
                case EmitterKind.Lamp: return 0.05f;
                case EmitterKind.Ember: return 0.02f;
                case EmitterKind.Brazier: return 0.15f;
                case EmitterKind.Fire: return 0.25f;
                case EmitterKind.Lava: return 0.5f;
                case EmitterKind.Flame: case EmitterKind.FlameByLook: return 0.03f;
                case EmitterKind.Effect: case EmitterKind.Glow: return 0.1f;
                default: return 0.05f;
            }
        }
        internal static float MinRadius(EmitterKind kind)
        {
            switch (kind)
            {
                case EmitterKind.Candle: return 0.004f;
                case EmitterKind.Torch: case EmitterKind.Lamp: return 0.02f;
                case EmitterKind.Brazier: return 0.08f;
                case EmitterKind.Fire: return 0.1f;
                case EmitterKind.Lava: return 0.1f;
                default: return 0.005f;
            }
        }
        internal static float MaxRadius(EmitterKind kind)
        {
            switch (kind)
            {
                case EmitterKind.Candle: return 0.015f;
                case EmitterKind.Torch: case EmitterKind.FlameByLook: return 0.08f;
                case EmitterKind.Lamp: return 0.15f;
                case EmitterKind.Ember: case EmitterKind.Flame: return 0.2f;
                case EmitterKind.Brazier: return 0.5f;
                case EmitterKind.Fire: return 0.6f;
                default: return 1f;
            }
        }
        // The radius of what emits a light from its bounds' half extents: the middle one (a flame is taller than it is wide, a
        // crystal cluster on a wall thinner than it is wide), held to the kind's range.
        internal static float MeasuredRadius(EmitterKind kind, float halfX, float halfY, float halfZ)
        {
            float a = Math.Abs(halfX), b = Math.Abs(halfY), c = Math.Abs(halfZ);
            float middle = Math.Max(Math.Min(a, b), Math.Min(Math.Max(a, b), c));
            if (float.IsNaN(middle) || middle <= 0f) return DefaultRadius(kind);
            return Math.Min(Math.Max(middle, MinRadius(kind)), MaxRadius(kind));
        }
        // How far from a light what emits it is looked for: a flame's own effect or glass sits within half a metre of its light,
        // a fire's within a metre; a glowing thing is the one the light was kept for.
        internal static float SearchReach(EmitterKind kind) => kind == EmitterKind.Brazier || kind == EmitterKind.Fire || kind == EmitterKind.Lava ? 1f : 0.5f;

        // A name-matched light must have a flame, lamp or emissive surface nearby. Return the nearest glowing
        // object within GlowReach, even if another light already claims it, or -1. Names alone otherwise retain
        // artist fill lights.
        internal static int EmitterAt(float x, float y, float z, IList<GlowThing> things, out float distance)
        {
            int best = -1; distance = float.MaxValue;
            for (int i = 0; i < things.Count; i++)
            {
                if (!IsThing(things[i])) continue;
                float d = Distance(x, y, z, things[i]);
                if (d <= GlowReach && d < distance) { distance = d; best = i; }
            }
            return best;
        }
        // A flame's or a lamp's light is traced where its flame or glass is: the centre of the measured thing, where it is
        // compact enough to be the flame (at most half the search reach from its centre to any side: a candle's flame effect, a
        // torch's, a lantern's glass; not a bonfire's smoke column or a set of candles under one light). HDRP places the game's
        // lights by eye: a tavern candle's sits in its wax 10 cm under the flame, where the tracer lit the wax from inside. An
        // effect's or a glowing thing's light keeps HDRP's place (a crystal's centre is inside the crystal).
        internal static bool MovesToEmitter(EmitterKind kind)
        {
            switch (kind)
            {
                case EmitterKind.Candle: case EmitterKind.Torch: case EmitterKind.Lamp: case EmitterKind.Brazier: case EmitterKind.Fire:
                case EmitterKind.Ember: case EmitterKind.Flame: case EmitterKind.FlameByLook: return true;
                default: return false;
            }
        }
        internal static bool FlameCentre(EmitterKind kind, in GlowThing t)
        {
            if (!MovesToEmitter(kind)) return false;
            float largest = Math.Max(Math.Abs(t.HalfX), Math.Max(Math.Abs(t.HalfY), Math.Abs(t.HalfZ)));
            return !float.IsNaN(largest) && largest > 0f && largest <= 0.5f * SearchReach(kind);
        }
        // The radius a light had before it took its emitter's size (the hidden "Lights sized as their emitters" key off, to
        // compare): HDRP's, held to 2 cm - 30 cm, a fire's to 1 m.
        internal static float HdrpRadius(float shapeRadius, bool fire) => Math.Min(Math.Max(shapeRadius, 0.02f), fire ? 1f : 0.3f);
    }
}
