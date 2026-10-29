// SPDX-License-Identifier: MIT
// TG Path Tracing's light rules (src/TGPathTracing/LightRules.cs): a light the names leave out is kept where a glowing thing
// stands at it (a crystal's light, a spell's), and stays out where nothing glows at it (the game's fill light), where the thing
// is a wall or a volume of air, or where a flame's own light already stands for it (a candle's extra lights); each light is
// traced at the size of what emits it, never the artist's radius.
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
    private static GlowThing Thing(float x, float y, float z, float half, int kind = LightRules.GlowSurface) =>
        new GlowThing { X = x, Y = y, Z = z, HalfX = half, HalfY = half, HalfZ = half, R = 0.2f, G = 0.3f, B = 0.5f, Kind = kind, Name = "thing" };
    private static int Main()
    {
        Expect(LightRules.IsDistortionOutput("Hidden/VFX/VFX_Fire_EmberGen/Distortion (1)/Distortion"), "fire's distortion output is not an emitter");
        Expect(!LightRules.IsDistortionOutput("Hidden/VFX/VFX_CandleFlame_Single/Flame/Output Particle|Shader Graph Quad - Unlit") &&
               !LightRules.IsDistortionOutput(null), "a real flame output remains eligible");
        // The mineral cave: a crystal cluster 0.6 m across on the wall, its blue light 0.4 m in front of it; a fill light 4 m off.
        var cave = new List<GlowThing> { Thing(10f, 2f, 0f, 0.3f) };
        int at = LightRules.GlowAt(10f, 2f, 0.7f, cave, out float d, out bool claimed);
        Expect(at == 0 && Math.Abs(d - 0.4f) < 1e-4f && !claimed, "a light 0.4 m from a glowing crystal is that crystal's light");
        Expect(LightRules.GlowAt(10f, 2f, 4.3f, cave, out _, out _) < 0, "a fill light 4 m from anything glowing stays out");
        Expect(LightRules.GlowAt(0f, 0f, 0f, new List<GlowThing>(), out _, out _) < 0, "a lone fill light with nothing glowing in the scene stays out");
        // Just within and just past the reach.
        Expect(LightRules.GlowAt(10f, 2f, 0.3f + LightRules.GlowReach - 0.01f, cave, out _, out _) == 0, "within the reach: kept");
        Expect(LightRules.GlowAt(10f, 2f, 0.3f + LightRules.GlowReach + 0.01f, cave, out _, out _) < 0, "past the reach: out");
        // The nearest of two glowing things.
        var two = new List<GlowThing> { Thing(0f, 0f, 0f, 0.1f), Thing(0.9f, 0f, 0f, 0.1f, LightRules.GlowEffect) };
        Expect(LightRules.GlowAt(0.6f, 0f, 0f, two, out _, out _) == 1, "the nearest glowing thing is the one a light stands for");
        // A glowing wall or a volume of dust 20 m across is no thing a light stands for.
        var wall = new List<GlowThing> { Thing(0f, 0f, 0f, 10f) };
        Expect(LightRules.GlowAt(0f, 0f, 10.5f, wall, out _, out _) < 0, "a light beside a 20 m glowing wall stays out");
        // A candle's flame its own light stands at (claimed): a fill light beside it stays out; another flame nearby still counts.
        var candle = new List<GlowThing> { Thing(0f, 1f, 0f, 0.01f, LightRules.GlowEffect), Thing(5f, 1f, 0f, 0.01f, LightRules.GlowEffect) };
        LightRules.Claim(0f, 1.02f, 0f, candle);
        Expect(candle[0].Claimed && !candle[1].Claimed, "a flame's own light claims the flame, not one 5 m away");
        Expect(LightRules.GlowAt(0.3f, 1.2f, 0f, candle, out _, out bool byFlame) < 0 && byFlame, "a fill light beside a candle whose own light is traced stays out");
        Expect(LightRules.GlowAt(5.3f, 1.2f, 0f, candle, out _, out _) == 1, "a light at an unclaimed flame is kept");
        // Each light's size: what emits it, never the artist's radius.
        Expect(LightRules.KindOfName("Light_Int_Candle_01_Lit") == EmitterKind.Candle && LightRules.KindOfName("Light_Torch_Big_01_Lit_02") == EmitterKind.Torch &&
               LightRules.KindOfName("Light_Int_Bonefire_Sticks_02") == EmitterKind.Fire && LightRules.KindOfName("Light_Int_Brazier_01_Lit") == EmitterKind.Brazier &&
               LightRules.KindOfName("Lantern_Hanging") == EmitterKind.Lamp && LightRules.KindOfName("VFX_Explosion_Aard") == EmitterKind.Effect &&
               LightRules.KindOfName("Fireflies_Swarm") == EmitterKind.Effect && LightRules.KindOfName("Point Light_17B") == EmitterKind.Unknown, "kinds by name");
        Expect(LightRules.MostSpecific(LightRules.KindOfName("PointLight_Fire"), LightRules.KindOfName("Light_Torch_Big_01_Lit")) == EmitterKind.Torch,
               "a torch's VFX_Fire/PointLight_Fire is the torch's flame, not a bonfire");
        Expect(Math.Abs(2f * LightRules.DefaultRadius(EmitterKind.Candle) - 0.012f) < 1e-6f, "a candle's flame is traced 1.2 cm across");
        float torch = 2f * LightRules.DefaultRadius(EmitterKind.Torch), fire = 2f * LightRules.DefaultRadius(EmitterKind.Fire);
        Expect(torch >= 0.05f && torch <= 0.1f && fire > torch, "a torch's flame 5 to 10 cm across, a bonfire larger");
        // Measured: a candle flame's particles 1 x 1 x 3 cm; a crystal cluster 0.6 x 0.6 x 0.2 m; smoke 2 m wide held to a torch's range.
        Expect(Math.Abs(LightRules.MeasuredRadius(EmitterKind.Candle, 0.005f, 0.015f, 0.005f) - 0.005f) < 1e-6f, "a candle measured by its flame");
        Expect(Math.Abs(LightRules.MeasuredRadius(EmitterKind.Glow, 0.3f, 0.3f, 0.1f) - 0.3f) < 1e-6f, "a crystal's light the crystal's size");
        Expect(LightRules.MeasuredRadius(EmitterKind.Torch, 1f, 1f, 1f) == LightRules.MaxRadius(EmitterKind.Torch), "a torch's smoke doesn't make its flame a metre wide");
        Expect(LightRules.MeasuredRadius(EmitterKind.Candle, 0f, 0f, 0f) == LightRules.DefaultRadius(EmitterKind.Candle), "nothing measured: the kind's size");
        // Lights round 2. A light kept by a flame's name needs its flame at it: the mineral cave's torch_06, its torch's flame at
        // (-0.58, 9.3, 71.8), its own two lights 0.1 and 0.4 m from it, the group's six warm fill lights 3 to 8 m off.
        var torch06 = new List<GlowThing> { new GlowThing { X = -0.58f, Y = 9.3f, Z = 71.85f, HalfX = 0.06f, HalfY = 0.15f, HalfZ = 0.06f, Kind = LightRules.GlowEffect, Name = "VFX_Fire" } };
        LightRules.Claim(-0.59f, 8.96f, 71.88f, torch06); // its PointLight_Cookies claims the flame
        Expect(LightRules.EmitterAt(-0.59f, 8.96f, 71.88f, torch06, out float dCookies) == 0 && dCookies < 0.2f, "the torch's own light stands at its flame, claimed or not");
        Expect(LightRules.EmitterAt(-0.58f, 9.44f, 71.8f, torch06, out _) == 0, "the torch's second light 0.4 m above the flame stands at it too");
        Expect(LightRules.EmitterAt(2.37f, 9.26f, 72.22f, torch06, out _) < 0 && LightRules.EmitterAt(-4.94f, 9.26f, 64.95f, torch06, out _) < 0,
               "the group's fill lights 3 and 8 m from the torch stand at nothing: left out");
        Expect(LightRules.EmitterAt(0f, 0f, 0f, new List<GlowThing> { Thing(0f, 0f, 0f, 10f) }, out _) < 0, "a glowing wall is no flame");
        Expect(LightRules.EmitterAt(-0.58f, 9.3f + 0.15f + LightRules.GlowReach + 0.01f, 71.85f, torch06, out _) < 0 &&
               LightRules.EmitterAt(-0.58f, 9.3f + 0.15f + LightRules.GlowReach - 0.01f, 71.85f, torch06, out _) == 0, "the flame within the glow reach of the light");
        // A tavern candle: HDRP's light 10 cm under the flame, in the wax; the flame's effect 2 x 4 x 2 cm. Its light is traced at
        // the flame's centre; a bonfire's smoke 4 m tall, or an effect's or a crystal's light, stays where HDRP has it.
        var flame = new GlowThing { X = 3.05f, Y = 501.59f, Z = -1.85f, HalfX = 0.01f, HalfY = 0.02f, HalfZ = 0.01f, Kind = LightRules.GlowEffect };
        var smoke = new GlowThing { X = 0f, Y = 2f, Z = 0f, HalfX = 0.5f, HalfY = 2f, HalfZ = 0.5f, Kind = LightRules.GlowEffect };
        Expect(LightRules.FlameCentre(EmitterKind.Candle, flame) && LightRules.FlameCentre(EmitterKind.Torch, flame) && LightRules.FlameCentre(EmitterKind.FlameByLook, flame),
               "a candle's, a torch's light at its measured flame");
        Expect(!LightRules.FlameCentre(EmitterKind.Fire, smoke) && !LightRules.FlameCentre(EmitterKind.Candle, Thing(0f, 0f, 0f, 0.3f)),
               "not at a smoke column's centre, nor at a set of candles 60 cm across");
        Expect(!LightRules.FlameCentre(EmitterKind.Effect, flame) && !LightRules.FlameCentre(EmitterKind.Glow, flame) && !LightRules.FlameCentre(EmitterKind.Lava, flame),
               "an effect's, a glowing thing's and lava's light stay where the game has them");
        // The hidden key off: HDRP's radius as before (2 cm - 30 cm, a fire's up to 1 m).
        Expect(LightRules.HdrpRadius(1f, false) == 0.3f && LightRules.HdrpRadius(1f, true) == 1f && LightRules.HdrpRadius(0.001f, false) == 0.02f &&
               LightRules.HdrpRadius(0.1f, false) == 0.1f, "HDRP's radii, held as before");
        Console.WriteLine(failures == 0 ? "light rules: all passed" : "light rules: " + failures + " failed");
        return failures == 0 ? 0 : 1;
    }
}
