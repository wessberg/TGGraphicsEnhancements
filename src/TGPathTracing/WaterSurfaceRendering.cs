// SPDX-License-Identifier: MIT
using System;
using HarmonyLib;
using UnityEngine.Rendering.HighDefinition;

namespace TGPathTracing
{
    // Keep HDRP's simulation running; the tracer reads its waves. Stop only the gameplay camera's draw and lighting of
    // the water the tracer draws itself (a surface with a slot: WaterCaustics.Traced). Water past the slots stays HDRP's
    // to draw and light: it must not disappear. These hooks do not replace a shader or reuse HDRP's shaded surface.
    internal static class WaterSurfaceRendering
    {
        internal static void Patch(Harmony harmony)
        {
            var system = AccessTools.TypeByName("UnityEngine.Rendering.HighDefinition.WaterSystem");
            var surface = AccessTools.Method(system, "ShouldRenderSurface");
            var lighting = AccessTools.Method(system, "RenderWaterLighting");
            if (surface == null || lighting == null)
                throw new MissingMethodException("HDRP water surface/lighting hooks are missing");
            harmony.Patch(surface, prefix: new HarmonyMethod(typeof(WaterSurfaceRendering), nameof(Surface)));
            harmony.Patch(lighting, prefix: new HarmonyMethod(typeof(WaterSurfaceRendering), nameof(Lighting)));
            // ShouldRenderSurface=false leaves waterGBuffer.valid=false. HDRP then omits its water lighting
            // and underwater fog variant while keeping atmospheric fog. Simulation constants update separately.
        }
        private static bool Active(HDCamera hdCamera) => Session.WantsFrames && HdrpPasses.IsGameplayCamera(hdCamera);
        // HDRP's WaterSystem.ShouldRenderSurface(HDCamera hdCamera, WaterSurface currentWater, out bool debugRequired).
        private static bool Surface(HDCamera hdCamera, WaterSurface currentWater, ref bool __result)
        {
            // The map's water while the player is in an interior is nobody's to draw (WaterCaustics.LeftOutside, PT-114).
            if (Active(hdCamera) && WaterCaustics.OutsideAnInterior(currentWater)) { __result = false; return false; }
            if (!Active(hdCamera) || !WaterCaustics.Traced(currentWater)) return true;
            __result = false;
            return false;
        }
        private static bool Lighting(HDCamera hdCamera) => !Active(hdCamera) || WaterCaustics.AnyUntraced;
    }
}
